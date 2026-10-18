//==============================================================================
// Glitch Nasty - offline engine tests (no JUCE, no DAW).
//
// The main trick: feed in a "counter" signal where every sample's value is its
// own sample number (0, 1, 2, 3 ...). Then every output sample tells us exactly
// which input sample it came from, so we can check the loop points land on the
// grid to within one sample.
//
// Build & run:  cmake --build build --target GlitchNastyTests && ./build/GlitchNastyTests
//==============================================================================

#include "GlitchCore.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <random>
#include <vector>

//------------------------------------------------------------------------------
// Allocation spy: counts heap allocations while the audio path is running.
static std::atomic<bool> gWatchAllocs { false };
static std::atomic<int> gAllocCount { 0 };

void* operator new (std::size_t n)
{
    if (gWatchAllocs.load())
        ++gAllocCount;
    if (void* p = std::malloc (n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

//------------------------------------------------------------------------------
static int gFailures = 0;
#define CHECK(cond, ...)                                                 \
    do {                                                                 \
        if (! (cond)) { ++gFailures; std::printf ("  FAIL: " __VA_ARGS__); std::printf ("\n"); } \
    } while (0)

struct RunConfig
{
    double sampleRate = 48000.0;
    double bpm = 120.0;
    bool hostPlaying = true;
    bool useHost = true;
    int channels = 2;
    int fixedBlock = 0;   // 0 = random block sizes 1..1024
    uint32_t seed = 1;
};

// Runs the core over 'input' (mono, copied to every channel). Returns channel 0.
static std::vector<float> run (const std::vector<float>& input, const gn::CoreParams& params,
                               const RunConfig& cfg, std::vector<double>* ppqOverride = nullptr)
{
    gn::GlitchCore core;
    core.prepare (cfg.sampleRate, cfg.channels);
    core.setRandomSeed (cfg.seed);

    std::vector<std::vector<float>> bufs ((size_t) cfg.channels, std::vector<float> (input.size()));
    for (auto& b : bufs) b = input;

    std::mt19937 rng (1234);
    std::uniform_int_distribution<int> blockDist (1, 1024);
    const double spq = cfg.sampleRate * 60.0 / cfg.bpm;

    gWatchAllocs = true;
    size_t pos = 0;
    float* ptrs[2] {};
    while (pos < input.size())
    {
        int n = cfg.fixedBlock > 0 ? cfg.fixedBlock : blockDist (rng);
        n = (int) std::min<size_t> ((size_t) n, input.size() - pos);

        gn::HostTime ht;
        ht.hasPpq = ht.hasBpm = cfg.useHost;
        ht.playing = cfg.hostPlaying;
        ht.bpm = cfg.bpm;
        ht.ppq = ppqOverride ? (*ppqOverride)[pos] : (double) pos / spq;

        for (int c = 0; c < cfg.channels; ++c)
            ptrs[c] = bufs[(size_t) c].data() + pos;

        core.process (ptrs, cfg.channels, n, params, ht);
        pos += (size_t) n;
    }
    gWatchAllocs = false;
    return bufs[0];
}

static std::vector<float> counterSignal (size_t n)
{
    std::vector<float> v (n);
    for (size_t i = 0; i < n; ++i) v[i] = (float) i; // exact up to 16.7M samples
    return v;
}

//------------------------------------------------------------------------------
// 1) Loop points and step boundaries land on the true grid (within 1 sample).
static void testGridAccuracy (double sr, double bpm, int grid, int division, const char* label)
{
    std::printf ("Grid accuracy: %s\n", label);

    gn::CoreParams p;
    p.gridIndex = grid;
    p.numSteps = 16;
    p.crossfadeMs = 0.0f; // hard cuts so jumps are visible
    const bool pattern[16] = { 1,0,1,1, 0,0,1,0, 1,1,1,0, 0,1,0,1 };
    for (int i = 0; i < 16; ++i)
        p.steps[(size_t) i] = { pattern[i], 1.0f, division };

    RunConfig cfg;
    cfg.sampleRate = sr;
    cfg.bpm = bpm;

    const double spq = sr * 60.0 / bpm;
    const double stepLen = spq * gn::kGridPpq[grid];
    const double slice = spq * gn::kDivisionPpq[division];
    const int totalSteps = 48;
    const size_t total = (size_t) std::ceil (stepLen * totalSteps);

    gAllocCount = 0;
    const auto out = run (counterSignal (total), p, cfg);
    CHECK (gAllocCount == 0, "%d heap allocations on the audio path", gAllocCount.load());

    // Where the output jumps (source sample isn't previous + 1).
    std::vector<size_t> found;
    for (size_t n = 1; n < out.size(); ++n)
        if ((int64_t) out[n] != (int64_t) out[n - 1] + 1)
            found.push_back (n);

    // Where it should jump, from the ideal (real-valued) grid.
    struct Jump { double t; double src; bool live; };
    std::vector<Jump> ideal;
    for (int k = 0; k < totalSteps; ++k)
    {
        if (! pattern[k % 16]) continue;
        const double t0 = k * stepLen, t1 = (k + 1) * stepLen;
        int wraps = 0;
        for (int j = 1; t0 + j * slice < t1 - 0.5; ++j, ++wraps)
            ideal.push_back ({ t0 + j * slice, t0, false });
        if (wraps > 0 && t1 < (double) total - 1)
            ideal.push_back ({ t1, t1, true }); // back to live (or a fresh slice) at the step end
    }

    CHECK (found.size() == ideal.size(), "found %zu jumps, expected %zu", found.size(), ideal.size());

    double worst = 0.0;
    for (size_t i = 0; i < std::min (found.size(), ideal.size()); ++i)
    {
        const double err = std::abs ((double) found[i] - ideal[i].t);
        worst = std::max (worst, err);
        const double srcErr = std::abs ((double) out[found[i]] - (ideal[i].live ? (double) found[i] : ideal[i].src));
        CHECK (err <= 1.0, "jump %zu at sample %zu, ideal %.2f", i, found[i], ideal[i].t);
        CHECK (srcErr <= 1.0, "jump %zu plays from %.0f, expected %.2f", i, (double) out[found[i]], ideal[i].src);
    }
    std::printf ("  %zu loop/return points, worst timing error %.2f samples\n", found.size(), worst);
}

// 2) Output doesn't depend on how the host chops up the blocks.
static void testBlockSizeIndependence()
{
    std::printf ("Block-size independence\n");
    gn::CoreParams p;
    for (int i = 0; i < 16; ++i)
        p.steps[(size_t) i] = { (i % 3) != 1, 1.0f, (i * 3) % gn::kNumDivisions, i % gn::kNumStutterTypes };
    p.gridIndex = 5; // 1/16T

    RunConfig a; a.sampleRate = 44100.0; a.bpm = 137.0;
    RunConfig b = a; b.fixedBlock = 1;
    RunConfig c = a; c.fixedBlock = 4096;

    std::vector<float> in (44100 * 6);
    for (size_t n = 0; n < in.size(); ++n) in[n] = (float) std::sin (0.031 * (double) n) * 0.5f;

    const auto oa = run (in, p, a), ob = run (in, p, b), oc = run (in, p, c);
    double maxDiff = 0.0;
    for (size_t n = 0; n < in.size(); ++n)
        maxDiff = std::max ({ maxDiff, (double) std::abs (oa[n] - ob[n]), (double) std::abs (oa[n] - oc[n]) });
    CHECK (maxDiff == 0.0, "outputs differ by %g between block sizes", maxDiff);
    std::printf ("  random vs 1-sample vs 4096-sample blocks: max difference %g\n", maxDiff);
}

// 3) Crossfades remove the clicks at loop points.
static double maxJump (const std::vector<float>& v)
{
    double m = 0.0;
    for (size_t n = 1; n < v.size(); ++n) m = std::max (m, (double) std::abs (v[n] - v[n - 1]));
    return m;
}

// 3b) Every stutter type does what it says (counter input decodes the playback position).
static std::vector<float> runAllSteps (gn::StutterType type, int grid, int division,
                                       const std::vector<float>& in, float xfadeMs = 0.0f)
{
    gn::CoreParams p;
    p.gridIndex = grid;
    p.crossfadeMs = xfadeMs;
    for (auto& s : p.steps) s = { true, 1.0f, division, (int) type };
    return run (in, p, RunConfig {});
}

static void testTypes()
{
    using T = gn::StutterType;
    std::printf ("Stutter types (48k, 120 BPM)\n");
    const auto counter = counterSignal (48000 * 4);
    const int G16 = 2, G4 = 0, D16 = 5, D32 = 7;

    // Max |output - expected| over the repeats of every 1/16 step (6000 samples, slice 3000).
    auto check16 = [&] (const char* name, T type, auto expectedOffset)
    {
        const auto out = runAllSteps (type, G16, D32, counter);
        double worst = 0.0;
        for (size_t n = 6000; n + 1 < out.size(); ++n)
        {
            const double S = (double) (n / 6000 * 6000), e = (double) n - S;
            const double expect = e < 3000 ? (double) n : S + expectedOffset (e - 3000);
            worst = std::max (worst, std::abs ((double) out[n] - expect));
        }
        CHECK (worst < 0.1, "%s: playback off by %.2f samples", name, worst);
        std::printf ("  %-10s plays the right samples (worst error %.3f)\n", name, worst);
    };
    check16 ("Reverse",   T::reverse,   [] (double r) { return 2999.0 - r; });
    check16 ("Pitch +12", T::pitchUp,   [] (double r) { return 2.0 * std::fmod (r, 1500.0); });
    check16 ("Pitch -12", T::pitchDown, [] (double r) { return 0.5 * r; });

    // Roll on a 1/4 grid from 1/16: 6000 live, then repeats of 3000, 1500, 750, 750 ... (1/128 floor)
    {
        const auto out = runAllSteps (T::roll, G4, D16, counter);
        double worst = 0.0;
        for (size_t n = 24000; n < out.size(); ++n)
        {
            const double S = (double) (n / 24000 * 24000), e = (double) n - S;
            double expect = (double) n, t = 6000.0, len = 3000.0;
            if (e >= 6000.0)
            {
                while (e >= t + len) { t += len; len = std::max (750.0, len * 0.5); }
                expect = S + (e - t);
            }
            worst = std::max (worst, std::abs ((double) out[n] - expect));
        }
        CHECK (worst < 0.1, "Roll: playback off by %.2f samples", worst);
        std::printf ("  %-10s halves each repeat down to 1/128 (worst error %.3f)\n", "Roll", worst);
    }

    // Tape Stop over a whole 1/16 step: position = integral of a speed falling 1 -> 0.
    {
        const auto out = runAllSteps (T::tapeStop, G16, D16, counter);
        double worst = 0.0, tail = 0.0;
        for (size_t n = 6000; n < out.size(); ++n)
        {
            const double S = (double) (n / 6000 * 6000), e = (double) n - S;
            const double pos = S + e - e * (e - 1.0) / (2.0 * 6000.0);
            const double rate = 1.0 - e / 6000.0;
            const double gain = std::min (1.0, rate * 4.0);
            if (e < 4400) worst = std::max (worst, std::abs ((double) out[n] - pos));
            else          tail = std::max (tail, std::abs ((double) out[n] - pos * gain));
        }
        CHECK (worst < 0.1 && tail < 0.1, "Tape Stop: off by %.3f / %.3f", worst, tail);
        std::printf ("  %-10s decelerates to a stop (worst error %.3f)\n", "Tape Stop", std::max (worst, tail));
    }

    // Gate: open for the first half of every 1/32, silent for the second half.
    {
        std::vector<float> sine (48000 * 2);
        for (size_t n = 0; n < sine.size(); ++n) sine[n] = (float) std::sin (0.05 * (double) n);
        const auto out = runAllSteps (T::gate, G16, D32, sine);
        double worst = 0.0;
        for (size_t n = 0; n < out.size(); ++n)
        {
            const bool open = (n % 3000) < 1500;
            worst = std::max (worst, (double) std::abs (out[n] - (open ? sine[n] : 0.0f)));
        }
        CHECK (worst < 1.0e-6, "Gate: off by %g", worst);
        std::printf ("  %-10s open/closed exactly on the 1/64 grid\n", "Gate");
    }

    // Fade: each repeat 0.6x the one before.
    {
        std::vector<float> sine (48000 * 2);
        for (size_t n = 0; n < sine.size(); ++n) sine[n] = (float) std::sin (2.0 * M_PI * 1000.0 * (double) n / 48000.0);
        const auto out = runAllSteps (T::fade, G4, D16, sine);
        auto rms = [&] (size_t a, size_t b) { double s = 0; for (size_t n = a; n < b; ++n) s += (double) out[n] * out[n]; return std::sqrt (s / (double) (b - a)); };
        const double r0 = rms (24000, 30000), r1 = rms (30000, 36000), r2 = rms (36000, 42000), r3 = rms (42000, 48000);
        const bool ok = std::abs (r1 / r0 - 0.6) < 0.01 && std::abs (r2 / r0 - 0.36) < 0.01 && std::abs (r3 / r0 - 0.216) < 0.01;
        CHECK (ok, "Fade levels %.3f %.3f %.3f", r1 / r0, r2 / r0, r3 / r0);
        std::printf ("  %-10s repeats at %.2f, %.2f, %.2f of the first pass\n", "Fade", r1 / r0, r2 / r0, r3 / r0);
    }

    // No clicks: with the default 3 ms crossfade every type stays smooth on a sine.
    {
        std::vector<float> sine (48000 * 3);
        for (size_t n = 0; n < sine.size(); ++n) sine[n] = (float) std::sin (2.0 * M_PI * 440.0 * (double) n / 48000.0);
        const double clean = maxJump (sine);
        bool allSmooth = true;
        std::printf ("  biggest sample step with 3 ms fades (input %.3f):", clean);
        for (int t = 0; t < gn::kNumStutterTypes; ++t)
        {
            const double speed = (T) t == T::pitchUp ? 2.0 : 1.0;
            const double j = maxJump (runAllSteps ((T) t, G16, 8, sine, 3.0f)); // 1/32T: never lines up with the sine
            std::printf (" %s %.3f", gn::kTypeShortNames[t], j);
            if (j > clean * speed * 1.6) { allSmooth = false; std::printf ("(!)"); }
        }
        std::printf ("\n");
        CHECK (allSmooth, "a stutter type clicks with a 3 ms crossfade");
    }
}

static void testCrossfade()
{
    std::printf ("Crossfades (440 Hz sine, every step stuttering at 1/32T)\n");
    gn::CoreParams p;
    for (auto& s : p.steps) s = { true, 1.0f, 8 }; // 1/32T loops: never land on a zero crossing

    RunConfig cfg;
    std::vector<float> in (48000 * 4);
    for (size_t n = 0; n < in.size(); ++n) in[n] = (float) std::sin (2.0 * M_PI * 440.0 * (double) n / 48000.0);

    const double cleanSlope = maxJump (in);
    p.crossfadeMs = 0.0f; const double hard = maxJump (run (in, p, cfg));
    p.crossfadeMs = 3.0f; const double soft = maxJump (run (in, p, cfg));

    std::printf ("  biggest sample-to-sample step: input %.3f, 0 ms fade %.3f, 3 ms fade %.3f\n", cleanSlope, hard, soft);
    CHECK (hard > 0.3, "expected audible clicks with no crossfade (got %.3f)", hard);
    CHECK (soft < cleanSlope * 2.0, "3 ms crossfade still clicks (%.3f)", soft);
}

// 4) Probability does what it says.
static void testProbability()
{
    std::printf ("Probability\n");
    gn::CoreParams p;
    p.syncMode = 1; // internal clock
    const double spq = 48000.0 * 60.0 / 120.0;
    const int stepLen = (int) (spq * 0.25); // 1/16 @ 120 = 6000 samples, exact

    auto countFires = [&] (float prob, int steps)
    {
        for (auto& s : p.steps) s = { true, prob, 7 };
        gn::GlitchCore core;
        core.prepare (48000.0, 1);
        core.setRandomSeed (99);
        std::vector<float> buf ((size_t) stepLen, 0.0f);
        float* ptr = buf.data();
        int fires = 0;
        for (int i = 0; i < steps; ++i)
        {
            core.process (&ptr, 1, stepLen, p, gn::HostTime {});
            if (core.getFiringStep() >= 0) ++fires;
        }
        return fires;
    };

    const int n = 4000;
    const int f0 = countFires (0.0f, n), f50 = countFires (0.5f, n), f100 = countFires (1.0f, n);
    std::printf ("  0%%: %d/%d   50%%: %d/%d   100%%: %d/%d\n", f0, n, f50, n, f100, n);
    CHECK (f0 == 0, "0%% fired %d times", f0);
    CHECK (f100 == n, "100%% fired only %d times", f100);
    CHECK (f50 > n * 0.45 && f50 < n * 0.55, "50%% fired %d times", f50);
}

// 5) Stopped transport and Mix 0 are true bypasses (bit-identical).
static void testPassThrough()
{
    std::printf ("Pass-through\n");
    gn::CoreParams p;
    for (auto& s : p.steps) s = { true, 1.0f, 9 };
    std::vector<float> in (48000 * 2);
    for (size_t n = 0; n < in.size(); ++n) in[n] = (float) std::sin (0.01 * (double) n);

    RunConfig stopped; stopped.hostPlaying = false;
    CHECK (run (in, p, stopped) == in, "stopped transport changed the audio");

    p.mix = 0.0f;
    CHECK (run (in, p, RunConfig {}) == in, "Mix 0 changed the audio");
    std::printf ("  stopped transport and Mix 0: bit-identical\n");
}

// 6) DAW cycle/loop: position jumps back, grid re-locks on the loop start.
static void testLoopJump()
{
    std::printf ("DAW loop (2-bar cycle)\n");
    gn::CoreParams p;
    p.crossfadeMs = 0.0f;
    for (auto& s : p.steps) s = { false, 1.0f, 7 };
    p.steps[0] = { true, 1.0f, 7 }; // step 1: 1/32 repeats

    RunConfig cfg; cfg.fixedBlock = 512;
    const double spq = 48000.0 * 60.0 / 120.0;
    const size_t loopLen = (size_t) (8.0 * spq);       // 2 bars = 192000 samples
    const size_t total = loopLen * 3 + 7000;
    std::vector<double> ppq (total);
    for (size_t n = 0; n < total; ++n) ppq[n] = (double) (n % loopLen) / spq;

    const auto out = run (counterSignal (total), p, cfg, &ppq);

    const size_t slice = (size_t) (0.125 * spq); // 3000
    bool ok = true;
    for (size_t L = loopLen; L < total; L += loopLen)
    {
        // At loop start the first step fires: one slice later we should hear the loop start again.
        if (L + slice < total && (size_t) out[L + slice] != L)
        {
            ok = false;
            std::printf ("  after loop at %zu: expected source %zu, got %.0f\n", L + slice, L, (double) out[L + slice]);
        }
    }
    for (size_t n = 0; n < total; ++n)
        if (out[n] > (float) n) { ok = false; break; } // never reads the future
    CHECK (ok, "grid didn't re-lock after the loop jump");
    std::printf ("  re-locks on every loop start: %s\n", ok ? "yes" : "NO");
}

int main()
{
    testGridAccuracy (48000.0, 120.0, 2, 7, "48k, 120 BPM, grid 1/16, repeat 1/32");
    testGridAccuracy (44100.0, 133.0, 5, 9, "44.1k, 133 BPM, grid 1/16T, repeat 1/64");
    testGridAccuracy (96000.0, 97.3, 1, 4, "96k, 97.3 BPM, grid 1/8, repeat 1/8T");
    testGridAccuracy (88200.0, 174.0, 3, 9, "88.2k, 174 BPM, grid 1/32, repeat 1/64");
    testGridAccuracy (48000.0, 90.0, 0, 3, "48k, 90 BPM, grid 1/4, repeat 1/16D");
    testTypes();
    testBlockSizeIndependence();
    testCrossfade();
    testProbability();
    testPassThrough();
    testLoopJump();

    std::printf (gFailures == 0 ? "\nALL TESTS PASSED\n" : "\n%d FAILURE(S)\n", gFailures);
    return gFailures == 0 ? 0 : 1;
}
