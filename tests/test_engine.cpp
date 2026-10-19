//==============================================================================
// Glitch Nasty - offline engine tests (no JUCE, no DAW).
//
// The main trick: feed in a "counter" signal where every sample's value is its
// own sample number (0, 1, 2, 3 ...). Then every output sample tells us exactly
// which input sample it came from, so we can check loop points land on the
// grid to within one sample, and that nothing ever plays audio from the future.
//
// Build & run:  cmake --build build --target GlitchNastyTests && ./build/GlitchNastyTests
//==============================================================================

#include "GlitchCore.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <functional>
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

using T = gn::StutterType;
using Buffers = std::vector<std::vector<float>>;

struct AbsMidi { size_t pos; uint8_t status, d1, d2; };

struct RunConfig
{
    double sampleRate = 48000.0;
    double bpm = 120.0;
    bool hostPlaying = true;
    bool useHost = true;
    int channels = 2;
    int fixedBlock = 0;   // 0 = random block sizes 1..1024
    std::vector<AbsMidi> midi;
};

// Runs the core over 'input' (mono, copied to every channel). Returns every channel.
static Buffers runAll (const std::vector<float>& input, const gn::CoreParams& params, const gn::PatternBank& bank,
                       const RunConfig& cfg, const std::vector<double>* ppqOverride = nullptr)
{
    gn::GlitchCore core;
    core.prepare (cfg.sampleRate, cfg.channels);

    Buffers bufs ((size_t) cfg.channels, input);
    std::vector<gn::MidiEvent> midiBlock (cfg.midi.size() + 1);

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

        int nm = 0;
        for (const auto& m : cfg.midi)
            if (m.pos >= pos && m.pos < pos + (size_t) n)
                midiBlock[(size_t) nm++] = { (int) (m.pos - pos), m.status, m.d1, m.d2 };

        for (int c = 0; c < cfg.channels; ++c)
            ptrs[c] = bufs[(size_t) c].data() + pos;

        core.process (ptrs, cfg.channels, n, params, ht, bank, midiBlock.data(), nm, -1);
        pos += (size_t) n;
    }
    gWatchAllocs = false;
    return bufs;
}

static std::vector<float> run (const std::vector<float>& input, const gn::CoreParams& params, const gn::PatternBank& bank,
                               const RunConfig& cfg = {}, const std::vector<double>* ppqOverride = nullptr)
{
    return runAll (input, params, bank, cfg, ppqOverride)[0];
}

static std::vector<float> counterSignal (size_t n)
{
    std::vector<float> v (n);
    for (size_t i = 0; i < n; ++i) v[i] = (float) i; // exact up to 16.7M samples
    return v;
}

static std::vector<float> sineSignal (size_t n, double hz, double sr = 48000.0)
{
    std::vector<float> v (n);
    for (size_t i = 0; i < n; ++i) v[i] = (float) std::sin (2.0 * M_PI * hz * (double) i / sr);
    return v;
}

static gn::PatternBank bankWith (const std::function<void (gn::Pattern&)>& fill)
{
    gn::PatternBank b;
    fill (b.patterns[0]);
    return b;
}

static gn::PatternBank allSteps (T type, int division, float p1 = -1.0f, float p2 = -1.0f)
{
    return bankWith ([&] (gn::Pattern& pat)
    {
        for (auto& s : pat)
        {
            s = gn::makeStep (type, division);
            if (p1 >= 0.0f) s.p1 = p1;
            if (p2 >= 0.0f) s.p2 = p2;
        }
    });
}

static double maxJump (const std::vector<float>& v, size_t from = 1)
{
    double m = 0.0;
    for (size_t n = std::max<size_t> (from, 1); n < v.size(); ++n) m = std::max (m, (double) std::abs (v[n] - v[n - 1]));
    return m;
}

static double rms (const std::vector<float>& v, size_t a, size_t b)
{
    double s = 0.0;
    for (size_t n = a; n < b; ++n) s += (double) v[n] * v[n];
    return std::sqrt (s / (double) (b - a));
}

//------------------------------------------------------------------------------
// 1) Loop points and step boundaries land on the true (swung) grid, within 1 sample.
static void testGridAccuracy (double sr, double bpm, int grid, int division, double swingPct, const char* label)
{
    std::printf ("Grid accuracy: %s\n", label);

    gn::CoreParams p;
    p.gridIndex = grid;
    p.crossfadeMs = 0.0f; // hard cuts so jumps are visible
    p.swing = (float) swingPct;
    const bool pattern[16] = { 1,0,1,1, 0,0,1,0, 1,1,1,0, 0,1,0,1 };
    const auto bank = bankWith ([&] (gn::Pattern& pat)
    {
        for (int i = 0; i < 16; ++i)
            if (pattern[i]) pat[(size_t) i] = gn::makeStep (T::repeat, division);
    });

    RunConfig cfg;
    cfg.sampleRate = sr;
    cfg.bpm = bpm;

    const double spq = sr * 60.0 / bpm;
    const double stepLen = spq * gn::kGridPpq[grid];
    const double slice = spq * gn::kDivisionPpq[division];
    const double sw = (swingPct - 0.5) * 2.0;
    const int totalSteps = 48;
    const size_t total = (size_t) std::ceil (stepLen * totalSteps);

    gAllocCount = 0;
    const auto out = run (counterSignal (total), p, bank, cfg);
    CHECK (gAllocCount == 0, "%d heap allocations on the audio path", gAllocCount.load());

    std::vector<size_t> found;
    for (size_t n = 1; n < out.size(); ++n)
        if ((int64_t) out[n] != (int64_t) out[n - 1] + 1)
            found.push_back (n);

    struct Jump { double t; double src; bool live; };
    std::vector<Jump> ideal;
    for (int k = 0; k < totalSteps; ++k)
    {
        if (! pattern[k % 16]) continue;
        const double t0 = gn::GridScheduler::stepStart (k, sw) * stepLen;
        const double t1 = gn::GridScheduler::stepStart (k + 1, sw) * stepLen;
        int wraps = 0;
        for (int j = 1; t0 + j * slice < t1 - 0.5; ++j, ++wraps)
            ideal.push_back ({ t0 + j * slice, t0, false });
        if (wraps > 0 && t1 < (double) total - 1)
            ideal.push_back ({ t1, t1, true });
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

//------------------------------------------------------------------------------
// 2) Every original type with its default controls plays the right samples.
static void testTypeDefaults()
{
    std::printf ("Stutter types, default controls (48k, 120 BPM)\n");
    const auto counter = counterSignal (48000 * 4);
    gn::CoreParams p;
    p.crossfadeMs = 0.0f;

    auto check16 = [&] (const char* name, T type, auto expectedOffset)
    {
        p.gridIndex = 2;
        const auto out = run (counter, p, allSteps (type, gn::kDiv32));
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
    check16 ("Repeat",    T::repeat,  [] (double r) { return r; });
    check16 ("Reverse",   T::reverse, [] (double r) { return 2999.0 - r; });
    check16 ("Pitch +12", T::pitch,   [] (double r) { return 2.0 * std::fmod (r, 1500.0); });

    // Roll on a 1/4 grid from 1/16: 6000 live, then 3000, 1500, 750, 750 ... (1/128 floor)
    {
        p.gridIndex = 0;
        const auto out = run (counter, p, allSteps (T::roll, gn::kDiv16));
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

    // Tape Stop over a whole 1/16 step.
    {
        p.gridIndex = 2;
        const auto out = run (counter, p, allSteps (T::tapeStop, gn::kDiv16));
        double worst = 0.0;
        for (size_t n = 6000; n < out.size(); ++n)
        {
            const double S = (double) (n / 6000 * 6000), e = (double) n - S;
            const double pos = S + e - e * (e - 1.0) / (2.0 * 6000.0);
            const double gain = std::min (1.0, (1.0 - e / 6000.0) * 4.0);
            worst = std::max (worst, std::abs ((double) out[n] - pos * gain));
        }
        CHECK (worst < 0.1, "Tape Stop: off by %.3f", worst);
        std::printf ("  %-10s decelerates to a stop (worst error %.3f)\n", "Tape Stop", worst);
    }

    // Gate (50 % duty, hard): open first half of every 1/32.
    {
        p.gridIndex = 2;
        const auto sine = sineSignal (48000 * 2, 382.0);
        const auto out = run (sine, p, allSteps (T::gate, gn::kDiv32));
        double worst = 0.0;
        for (size_t n = 0; n < out.size(); ++n)
            worst = std::max (worst, (double) std::abs (out[n] - ((n % 3000) < 1500 ? sine[n] : 0.0f)));
        CHECK (worst < 1.0e-6, "Gate: off by %g", worst);
        std::printf ("  %-10s open/closed exactly on the grid\n", "Gate");
    }

    // Fade: each repeat 0.6x the one before.
    {
        p.gridIndex = 0;
        const auto out = run (sineSignal (48000 * 2, 1000.0), p, allSteps (T::fade, gn::kDiv16));
        const double r0 = rms (out, 24000, 30000), r1 = rms (out, 30000, 36000), r2 = rms (out, 36000, 42000), r3 = rms (out, 42000, 48000);
        const bool ok = std::abs (r1 / r0 - 0.6) < 0.01 && std::abs (r2 / r0 - 0.36) < 0.01 && std::abs (r3 / r0 - 0.216) < 0.01;
        CHECK (ok, "Fade levels %.3f %.3f %.3f", r1 / r0, r2 / r0, r3 / r0);
        std::printf ("  %-10s repeats at %.2f, %.2f, %.2f of the first pass\n", "Fade", r1 / r0, r2 / r0, r3 / r0);
    }
}

//------------------------------------------------------------------------------
// 3) The type-specific controls.
static void testTypeControls()
{
    std::printf ("Type controls\n");
    const auto counter = counterSignal (48000 * 3);
    gn::CoreParams p;
    p.crossfadeMs = 0.0f;

    // Repeat Offset: 1/8 steps, 1/32 slices (4 per step), offset slice 2 -> live for half the step, then loop slice 2.
    {
        p.gridIndex = 1;
        const auto out = run (counter, p, allSteps (T::repeat, gn::kDiv32, 1.0f / 3.0f));
        bool ok = true;
        for (size_t n = 12000; n < 24000; ++n)
        {
            const size_t e = n - 12000;
            const double expect = e < 6000 ? (double) n : 15000.0 + (double) ((e - 6000) % 3000);
            ok &= std::abs ((double) out[n] - expect) < 0.01;
        }
        CHECK (ok, "Repeat Offset loops the wrong slice");
        std::printf ("  Repeat Offset loops the chosen slice of the step\n");
    }

    // Repeat Variation: repeats start up to 1/4 slice early, never late.
    {
        p.gridIndex = 2;
        const auto out = run (counter, p, allSteps (T::repeat, gn::kDiv64, 0.0f, 1.0f)); // 1500-sample slices
        bool ok = true, varied = false;
        for (size_t step = 1; step < 20; ++step)
            for (size_t r = 1; r < 4; ++r)
            {
                const size_t n = step * 6000 + r * 1500;
                const double start = (double) out[n], S = (double) step * 6000.0;
                ok &= start <= S + 0.01 && start >= S - 375.0 - 0.01;
                varied |= start < S - 1.0;
            }
        CHECK (ok && varied, "Repeat Variation out of range (ok=%d varied=%d)", (int) ok, (int) varied);
        std::printf ("  Repeat Variation nudges each repeat up to 1/4 slice earlier\n");
    }

    // Reverse ping-pong: backwards, forwards, backwards ...
    {
        p.gridIndex = 0;
        const auto out = run (counter, p, allSteps (T::reverse, gn::kDiv16, 1.0f));
        bool ok = true;
        for (size_t n = 24000; n < 48000; ++n)
        {
            const double e = (double) (n - 24000);
            const int k = (int) (e / 6000.0);
            const double u = e - k * 6000.0;
            const double expect = k == 0 ? (double) n : (u < 3000.0 ? 24000.0 + 3000.0 - u : 24000.0 + (u - 3000.0));
            ok &= std::abs ((double) out[n] - expect) < 0.01;
        }
        CHECK (ok, "Reverse ping-pong");
        std::printf ("  Reverse Ping-Pong goes back then forward inside every repeat (no jump at the turn)\n");
    }

    // Gate 25 % duty.
    {
        p.gridIndex = 2;
        const auto sine = sineSignal (48000, 382.0);
        const auto out = run (sine, p, allSteps (T::gate, gn::kDiv32, (0.25f - 0.05f) / 0.9f));
        double worst = 0.0;
        for (size_t n = 0; n < out.size(); ++n)
            if (n % 3000 != 750) // the edge sample itself sits on a float rounding boundary
                worst = std::max (worst, (double) std::abs (out[n] - ((n % 3000) < 750 ? sine[n] : 0.0f)));
        CHECK (worst < 1.0e-5, "Gate duty 25%%: off by %g", worst);
        std::printf ("  Gate Duty 25%% opens for exactly a quarter of each cycle\n");
    }

    // Gate Pump: ducks at each cycle start, swells back by the duty point.
    {
        p.gridIndex = 2;
        p.crossfadeMs = 3.0f;
        const auto ones = std::vector<float> (48000, 1.0f);
        const auto out = run (ones, p, allSteps (T::gate, gn::kDiv32, 0.5f, 1.0f));
        p.crossfadeMs = 0.0f;
        const double atStart = out[6000 + 3000 + 200], atQuarter = out[6000 + 3000 + 750], atHalf = out[6000 + 3000 + 1600];
        const bool ok = atStart < 0.05 && std::abs (atQuarter - 0.25) < 0.02 && atHalf > 0.99;
        CHECK (ok, "Gate pump curve %.3f %.3f %.3f", atStart, atQuarter, atHalf);
        std::printf ("  Gate Pump: %.2f just after the beat, %.2f a quarter in, %.2f by the duty point\n", atStart, atQuarter, atHalf);
    }

    // Roll slow down, curve 0 (factor 0.9): each repeat 1/0.9 longer.
    {
        p.gridIndex = 0;
        const auto out = run (counter, p, allSteps (T::roll, gn::kDiv64, 1.0f, 0.0f)); // 1500-sample slices
        std::vector<double> expectWraps;
        double t = 1500.0, len = 1500.0;
        while (t < 24000.0 - 0.5) { expectWraps.push_back (t); len /= 0.9; t += len; }
        std::vector<size_t> found;
        for (size_t n = 24001; n < 48000; ++n)
            if ((int64_t) out[n] != (int64_t) out[n - 1] + 1) found.push_back (n - 24000);
        bool ok = found.size() == expectWraps.size();
        for (size_t i = 0; ok && i < found.size(); ++i) ok &= std::abs ((double) found[i] - expectWraps[i]) <= 1.0;
        CHECK (ok, "Roll slow-down: %zu wraps, expected %zu", found.size(), expectWraps.size());
        std::printf ("  Roll Slow Down: each repeat 1/0.9 longer (%zu repeats, on time)\n", found.size());
    }

    // Pitch +7 semitones (any interval, not just octaves).
    {
        p.gridIndex = 2;
        const auto out = run (counter, p, allSteps (T::pitch, gn::kDiv32, 0.5f + 7.0f / 48.0f, 0.5f));
        const double r = std::exp2 (7.0 / 12.0);
        double worst = 0.0, pos = 6000.0;
        for (size_t n = 9000; n < 12000; ++n)
        {
            if (n > 9000) { pos += r; if (pos >= 9000.0) pos -= 3000.0; }
            worst = std::max (worst, std::abs ((double) out[n] - pos));
        }
        CHECK (worst < 0.05, "Pitch +7: off by %.3f", worst);
        std::printf ("  Pitch +7 st plays at exactly 2^(7/12) speed (worst error %.3f)\n", worst);
    }

    // Pitch glide +12 across a 1/4 step from 0 st: speed = 2^(t / step).
    {
        p.gridIndex = 0;
        const auto out = run (counter, p, allSteps (T::pitch, gn::kDiv16, 0.5f, 0.75f));
        double worst = 0.0;
        for (double e : { 6100.0, 12010.0, 18020.0 })
        {
            const size_t n = 24000 + (size_t) e;
            worst = std::max (worst, std::abs ((double) (out[n + 1] - out[n]) - std::exp2 (e / 24000.0)));
        }
        CHECK (worst < 0.01, "Pitch glide speed off by %.4f", worst);
        std::printf ("  Pitch Glide bends smoothly across the step (speed error %.4f)\n", worst);
    }

    // Tape Start: spins up from a standstill (position = integral of t/len).
    {
        p.gridIndex = 2;
        const auto out = run (counter, p, allSteps (T::tapeStop, gn::kDiv16, 1.0f, 0.0f));
        double worst = 0.0;
        for (size_t n = 6000; n < 12000; ++n)
        {
            const double e = (double) (n - 6000);
            const double pos = 6000.0 + e * (e - 1.0) / (2.0 * 6000.0);
            const double gain = std::min (1.0, (e / 6000.0) * 4.0);
            worst = std::max (worst, std::abs ((double) out[n] - pos * gain));
        }
        CHECK (worst < 0.1, "Tape Start: off by %.3f", worst);
        std::printf ("  Tape Start spins up from nothing (worst error %.3f)\n", worst);
    }

    // Fade In: each repeat louder, ending at full level.
    {
        p.gridIndex = 0;
        const auto out = run (sineSignal (48000 * 2, 1000.0), p, allSteps (T::fade, gn::kDiv16, 3.0f / 7.0f, 1.0f));
        const double r0 = rms (out, 24000, 30000), r3 = rms (out, 42000, 48000), in = 1.0 / std::sqrt (2.0);
        const bool ok = std::abs (r0 / in - 0.216) < 0.01 && std::abs (r3 / in - 1.0) < 0.01;
        CHECK (ok, "Fade In levels %.3f -> %.3f", r0 / in, r3 / in);
        std::printf ("  Fade In swells %.2f -> %.2f\n", r0 / in, r3 / in);
    }
}

//------------------------------------------------------------------------------
// 4) The new types.
static void testNewTypes()
{
    std::printf ("New types\n");
    const auto counter = counterSignal (48000 * 3);
    gn::CoreParams p;
    p.crossfadeMs = 0.0f;

    // Shuffle: every slice is either live or an earlier, slice-aligned slice (maybe reversed).
    {
        p.gridIndex = 0; // 1/4 steps, 1/16 slices: 4 per step
        const auto out = run (counter, p, allSteps (T::shuffle, gn::kDiv16, 1.0f, 0.5f));
        bool ok = true;
        int swapped = 0, reversed = 0;
        for (size_t step = 1; step < 5; ++step)
            for (int k = 1; k < 4; ++k)
            {
                const double S = (double) step * 24000.0, base = S + k * 6000.0;
                const double first = out[(size_t) base], second = out[(size_t) base + 1];
                if (first == base) continue; // live
                ++swapped;
                const bool rev = second < first;
                reversed += rev ? 1 : 0;
                const double sliceStart = rev ? first - 5999.0 : first;
                const double j = (sliceStart - S) / 6000.0;
                ok &= std::abs (j - std::round (j)) < 1.0e-6 && j < k && j >= k - 3;
            }
        CHECK (ok && swapped > 3, "Shuffle slices wrong (ok=%d swapped=%d)", (int) ok, swapped);
        std::printf ("  Shuffle: %d of 12 slices swapped for earlier ones (%d reversed), all on the slice grid\n", swapped, reversed);
    }

    // Freeze: smooth, never silent, never the future.
    {
        p.gridIndex = 0;
        p.crossfadeMs = 3.0f;
        const auto sine = sineSignal (48000 * 2, 440.0);
        const auto out = run (sine, p, allSteps (T::freeze, gn::kDiv16));
        p.crossfadeMs = 0.0f;
        const double level = rms (out, 26000, 46000), jump = maxJump (out, 24000);
        CHECK (level > 0.2 && jump < 0.09, "Freeze level %.3f, biggest step %.3f", level, jump);
        std::printf ("  Freeze holds a drone (level %.2f, biggest sample step %.3f)\n", level, jump);
    }

    // Scratch: swings between S - depth and S.
    {
        p.gridIndex = 2;
        const auto out = run (counter, p, allSteps (T::scratch, gn::kDiv32));
        const double A = gn::scratchDepth (0.5f) * 3000.0;
        bool ok = true;
        double lo = 1e9;
        for (size_t n = 6000; n < 12000; ++n)
        {
            ok &= out[n] <= 6000.0f + 0.01f && out[n] >= 6000.0 - A - 0.01;
            lo = std::min (lo, (double) out[n]);
        }
        CHECK (ok && lo < 6000.0 - A + 1.0, "Scratch range");
        std::printf ("  Scratch drags back %.0f samples and returns, never past 'now'\n", 6000.0 - lo);
    }

    // Ping-Pong: repeats alternate hard left / hard right.
    {
        p.gridIndex = 0;
        const auto both = runAll (sineSignal (48000, 1000.0), p, allSteps (T::pingPong, gn::kDiv16), RunConfig {});
        const double l1 = rms (both[0], 30000, 36000), r1 = rms (both[1], 30000, 36000);
        const double l2 = rms (both[0], 36000, 42000), r2 = rms (both[1], 36000, 42000);
        const bool ok = l1 > 0.7 && r1 < 1.0e-6 && l2 < 1.0e-6 && r2 > 0.7;
        CHECK (ok, "Ping-pong L/R %.3f/%.3f then %.3f/%.3f", l1, r1, l2, r2);
        std::printf ("  Ping-Pong: repeat 1 left only, repeat 2 right only\n");
    }
}

//------------------------------------------------------------------------------
// 5) Per-step level / pan / filter, ties.
static void testStepShaping()
{
    std::printf ("Level, pan, filter, ties\n");
    gn::CoreParams p;
    p.crossfadeMs = 0.0f;

    // Level -6 dB, pan +0.5 (balance law: left drops to half, right unchanged).
    {
        auto bank = allSteps (T::repeat, gn::kDiv32);
        for (auto& s : bank.patterns[0]) { s.levelDb = -6.0f; s.pan = 0.5f; }
        const auto in = sineSignal (24000, 300.0);
        const auto both = runAll (in, p, bank, RunConfig {});
        const float g = std::pow (10.0f, -6.0f / 20.0f);
        double worst = 0.0;
        for (size_t n = 6000; n < 9000; ++n)
            worst = std::max ({ worst, (double) std::abs (both[0][n] - in[n] * g * 0.5f), (double) std::abs (both[1][n] - in[n] * g) });
        CHECK (worst < 1.0e-6, "Level/pan off by %g", worst);
        std::printf ("  Level -6 dB + Pan R50 exact on both sides\n");
    }

    // Filter: LP (-0.8, ~500 Hz) kills a 5 kHz tone; HP (+0.8, ~1.4 kHz) kills 100 Hz.
    {
        auto lp = allSteps (T::repeat, gn::kDiv32);
        for (auto& s : lp.patterns[0]) s.filter = -0.8f;
        const auto hiTone = sineSignal (48000, 5000.0);
        const double lpRatio = rms (run (hiTone, p, lp), 24000, 48000) / rms (hiTone, 24000, 48000);

        auto hp = allSteps (T::repeat, gn::kDiv32);
        for (auto& s : hp.patterns[0]) s.filter = 0.8f;
        const auto loTone = sineSignal (48000, 100.0);
        const double hpRatio = rms (run (loTone, p, hp), 24000, 48000) / rms (loTone, 24000, 48000);

        CHECK (lpRatio < 0.08 && hpRatio < 0.12, "Filter ratios LP %.3f HP %.3f", lpRatio, hpRatio);
        std::printf ("  Filter: LP leaves %.1f%% of 5 kHz, HP leaves %.1f%% of 100 Hz\n", lpRatio * 100.0, hpRatio * 100.0);

        // After the filtered glitch ends, the dry signal comes back bit-identical.
        auto bank = bankWith ([] (gn::Pattern& pat)
        {
            for (int i = 0; i < 4; ++i) { pat[(size_t) i] = gn::makeStep (T::repeat, gn::kDiv32); pat[(size_t) i].filter = -0.5f; }
        });
        const auto noise = sineSignal (96000, 777.0);
        const auto out = run (noise, p, bank);
        bool identical = true;
        for (size_t n = 30000; n < 96000; ++n) identical &= out[n] == noise[n];
        CHECK (identical, "dry signal not restored after a filtered glitch");
        std::printf ("  ... and the dry signal is bit-identical again once the glitch ends\n");
    }

    // Ties: step 1 = Repeat 1/32 for 4 steps; steps 2-4 (Reverse) are ignored.
    {
        const auto bank = bankWith ([] (gn::Pattern& pat)
        {
            pat[0] = gn::makeStep (T::repeat, gn::kDiv32, 1.0f, 4);
            for (int i = 1; i < 4; ++i) { pat[(size_t) i] = gn::makeStep (T::reverse, gn::kDiv16); pat[(size_t) i].on = false; }
        });
        const auto out = run (counterSignal (120000), p, bank);
        bool ok = true;
        for (size_t n = 0; n < 120000; ++n)
        {
            const size_t bar = n / 96000 * 96000, e = n - bar;
            const double expect = (e >= 3000 && e < 24000) ? (double) (bar + e % 3000) : (double) n;
            ok &= std::abs ((double) out[n] - expect) < 0.01;
        }
        CHECK (ok, "Ties");
        std::printf ("  Ties: one glitch runs across 4 steps; covered steps that are off are skipped\n");
    }
}

//------------------------------------------------------------------------------
// 6) Density, seed, chance.
static void testRandomness()
{
    std::printf ("Chance, density, seed\n");

    // Chance distribution (internal clock, 1 block = 1 step).
    {
        gn::CoreParams p;
        p.syncMode = 1;
        p.seed = 99;
        const int stepLen = 6000;
        auto countFires = [&] (float prob, int steps)
        {
            gn::PatternBank bank;
            for (auto& s : bank.patterns[0]) s = gn::makeStep (T::repeat, gn::kDiv32, prob);
            gn::GlitchCore core;
            core.prepare (48000.0, 1);
            std::vector<float> buf ((size_t) stepLen, 0.0f);
            float* ptr = buf.data();
            int fires = 0;
            for (int i = 0; i < steps; ++i)
            {
                core.process (&ptr, 1, stepLen, p, gn::HostTime {}, bank);
                if (core.getFiringStep() >= 0) ++fires;
            }
            return fires;
        };
        const int n = 4000;
        const int f0 = countFires (0.0f, n), f50 = countFires (0.5f, n), f100 = countFires (1.0f, n);
        std::printf ("  chance 0%%: %d/%d   50%%: %d/%d   100%%: %d/%d\n", f0, n, f50, n, f100, n);
        CHECK (f0 == 0, "0%% fired %d times", f0);
        CHECK (f100 == n, "100%% fired only %d times", f100);
        CHECK (f50 > n * 0.45 && f50 < n * 0.55, "50%% fired %d times", f50);
    }

    const auto in = sineSignal (96000 * 2, 333.0);

    // Density -100%: nothing fires. +100%: steps at 0% chance all fire.
    {
        gn::CoreParams p;
        p.density = -1.0f;
        CHECK (run (in, p, allSteps (T::repeat, gn::kDiv32)) == in, "Density -100%% still fired");
        p.density = 1.0f;
        auto zero = allSteps (T::repeat, gn::kDiv32);
        for (auto& s : zero.patterns[0]) s.chance = 0.0f;
        const auto out = run (in, p, zero);
        int changed = 0;
        for (size_t step = 0; step < 32; ++step) changed += out[step * 6000 + 4000] != in[step * 6000 + 4000] ? 1 : 0;
        CHECK (changed == 32, "Density +100%%: only %d of 32 steps fired", changed);
        std::printf ("  Density -100%% = nothing fires; +100%% = every on-step fires, even at 0%% chance\n");
    }

    // Seed: same seed, same result; different seed, different result.
    {
        gn::CoreParams p;
        auto half = allSteps (T::repeat, gn::kDiv32);
        for (auto& s : half.patterns[0]) s.chance = 0.5f;
        p.seed = 7;
        const auto a = run (in, p, half), b = run (in, p, half);
        p.seed = 8;
        const auto c = run (in, p, half);
        CHECK (a == b && a != c, "Seed determinism");
        std::printf ("  Seed: same seed gives the identical bounce, a new seed gives new decisions\n");
    }
}

//------------------------------------------------------------------------------
// 7) Live MIDI notes, quantise, pattern switching, footswitch engage.
static void testLive()
{
    std::printf ("Live / MIDI\n");
    const auto counter = counterSignal (210000);
    gn::PatternBank empty;
    gn::CoreParams p;
    p.crossfadeMs = 0.0f;
    p.liveDivision = gn::kDiv32;

    auto expectRange = [&] (const std::vector<float>& out, size_t a, size_t b, const std::function<double (size_t)>& f)
    {
        bool ok = true;
        for (size_t n = a; n < b; ++n) ok &= std::abs ((double) out[n] - f (n)) < 0.01;
        return ok;
    };

    // Unquantised: C3 held 10000..30000 -> Repeat 1/32 starting exactly at 10000.
    {
        p.liveQuant = 0;
        RunConfig cfg;
        cfg.midi = { { 10000, 0x90, 60, 100 }, { 30000, 0x80, 60, 0 } };
        const auto out = run (counter, p, empty, cfg);
        const bool ok = expectRange (out, 0, 13000, [] (size_t n) { return (double) n; })
                     && expectRange (out, 13000, 30000, [] (size_t n) { return 10000.0 + (double) ((n - 10000) % 3000); })
                     && expectRange (out, 30000, 60000, [] (size_t n) { return (double) n; });
        CHECK (ok, "Live note, no quantise");
        std::printf ("  Note on/off (quantise off): glitch starts and stops on the exact sample\n");
    }

    // Quantised to 1/16: note at 10000 starts at the next grid line (12000).
    {
        p.liveQuant = 1;
        RunConfig cfg;
        cfg.midi = { { 10000, 0x90, 60, 100 }, { 30000, 0x80, 60, 0 } };
        const auto out = run (counter, p, empty, cfg);
        const bool ok = expectRange (out, 0, 15000, [] (size_t n) { return (double) n; })
                     && expectRange (out, 15000, 30000, [] (size_t n) { return 12000.0 + (double) ((n - 12000) % 3000); });
        CHECK (ok, "Live note, quantise 1/16");
        std::printf ("  Quantise 1/16: a note played between lines waits for the next one\n");
    }

    // Pattern switch: C#2 (49) selects pattern B from the next step.
    {
        gn::PatternBank bank;
        for (auto& s : bank.patterns[1]) s = gn::makeStep (T::repeat, gn::kDiv32);
        RunConfig cfg;
        cfg.midi = { { 50000, 0x90, 49, 100 } };
        const auto out = run (counter, p, bank, cfg);
        const bool ok = expectRange (out, 0, 57000, [] (size_t n) { return (double) n; })
                     && expectRange (out, 57000, 60000, [] (size_t n) { return 54000.0 + (double) (n - 57000); });
        CHECK (ok, "MIDI pattern switch");
        std::printf ("  MIDI C#2 switches to pattern B on the next step\n");
    }

    // Footswitch: sustain pedal toggles Engage on the next bar line.
    {
        const auto bank = allSteps (T::repeat, gn::kDiv32);
        RunConfig cfg;
        cfg.midi = { { 20000, 0xB0, 64, 127 }, { 21000, 0xB0, 64, 0 }, { 120000, 0xB0, 64, 127 }, { 121000, 0xB0, 64, 0 } };
        const auto out = run (counter, p, bank, cfg);
        const bool stillOn  = expectRange (out, 93000, 96000, [] (size_t n) { return 90000.0 + (double) (n - 93000); });
        const bool off      = expectRange (out, 96000, 192000, [] (size_t n) { return (double) n; });
        const bool backOn   = expectRange (out, 195000, 198000, [] (size_t n) { return 192000.0 + (double) (n - 195000); });
        CHECK (stillOn && off && backOn, "Footswitch engage (%d %d %d)", (int) stillOn, (int) off, (int) backOn);
        std::printf ("  Footswitch: off at the next bar, back on at the bar after the next press\n");
    }
}

//------------------------------------------------------------------------------
// 8) Safety: nothing ever reads the future; every type is click-free at 3 ms;
//    output doesn't depend on block size; true bypasses.
static void testSafety()
{
    std::printf ("Safety\n");

    // Never the future, across every type and a spread of settings, with swing and ties.
    {
        gn::CoreParams p;
        p.crossfadeMs = 0.0f;
        p.swing = 0.6f;
        const auto counter = counterSignal (60000);
        int runs = 0, bad = 0;
        gAllocCount = 0;
        for (int t = 0; t < gn::kNumStutterTypes; ++t)
            for (auto pp : { std::pair<float, float> { 0.0f, 0.0f }, { 0.37f, 1.0f }, { 1.0f, 0.5f } })
                for (int div : { gn::kDiv16, gn::kDiv32T })
                    for (int len : { 1, 3 })
                    {
                        auto bank = bankWith ([&] (gn::Pattern& pat)
                        {
                            for (int i = 0; i < 16; i += 2)
                            {
                                pat[(size_t) i] = gn::makeStep ((T) t, div, 1.0f, len);
                                pat[(size_t) i].p1 = pp.first;
                                pat[(size_t) i].p2 = pp.second;
                                // the next step is on too: inside a tie it becomes a layer on top
                                pat[(size_t) i + 1] = gn::makeStep ((T) ((t + 3) % gn::kNumStutterTypes), gn::kDiv32);
                                pat[(size_t) i + 1].p1 = pp.second;
                                pat[(size_t) i + 1].p2 = pp.first;
                            }
                        });
                        const auto out = run (counter, p, bank);
                        ++runs;
                        for (size_t n = 0; n < out.size(); ++n)
                            if (! std::isfinite (out[n]) || out[n] > (float) n + 0.01f) { ++bad; break; }
                    }
        CHECK (bad == 0, "%d of %d runs read audio from the future", bad, runs);
        CHECK (gAllocCount == 0, "%d heap allocations on the audio path", gAllocCount.load());
        std::printf ("  %d runs (every type x settings x ties x layers x swing): never plays the future, no allocations\n", runs);
    }

    // No clicks with the default 3 ms crossfade (440 Hz sine, 1/32T: never lines up with the sine).
    {
        gn::CoreParams p;
        p.crossfadeMs = 3.0f;
        const auto sine = sineSignal (48000 * 2, 440.0);
        const double clean = maxJump (sine);
        bool allSmooth = true;
        std::printf ("  biggest sample step with 3 ms fades (input %.3f):\n   ", clean);
        for (int t = 0; t < gn::kNumStutterTypes; ++t)
        {
            const double speed = (T) t == T::pitch ? 2.0 : ((T) t == T::scratch ? 1.7 : 1.0);
            const double j = maxJump (run (sine, p, allSteps ((T) t, gn::kDiv32T)));
            std::printf (" %s %.3f", gn::kTypeShortNames[t], j);
            if (j > clean * speed * 1.6) { allSmooth = false; std::printf ("(!)"); }
        }
        std::printf ("\n");
        CHECK (allSmooth, "a stutter type clicks with a 3 ms crossfade");

        p.crossfadeMs = 0.0f;
        const double hard = maxJump (run (sine, p, allSteps (T::repeat, gn::kDiv32T)));
        CHECK (hard > 0.3, "expected audible clicks with no crossfade (got %.3f)", hard);
    }

    // Block-size independence with everything switched on.
    {
        gn::CoreParams p;
        p.swing = 0.62f;
        p.gridIndex = 5; // 1/16T
        p.density = 0.2f;
        auto bank = bankWith ([] (gn::Pattern& pat)
        {
            for (int i = 0; i < 16; ++i)
            {
                auto& s = pat[(size_t) i];
                s = gn::makeStep ((T) (i % gn::kNumStutterTypes), (i * 3) % gn::kNumDivisions, i % 4 == 1 ? 0.5f : 1.0f, i % 5 == 0 ? 2 : 1);
                s.on = (i % 7) != 3;
                s.levelDb = (float) (i % 3) * -3.0f;
                s.pan = (i % 2 == 0) ? 0.3f : -0.3f;
                s.filter = (i % 3 == 0) ? -0.5f : ((i % 3 == 1) ? 0.4f : 0.0f);
            }
        });
        std::vector<float> in (44100 * 6);
        for (size_t n = 0; n < in.size(); ++n) in[n] = (float) std::sin (0.031 * (double) n) * 0.5f;

        RunConfig a; a.sampleRate = 44100.0; a.bpm = 137.0;
        a.midi = { { 50000, 0x90, 62, 100 }, { 70000, 0x80, 62, 0 }, { 90000, 0x90, 50, 100 }, { 150000, 0x90, 68, 90 }, { 170000, 0x80, 68, 0 } };
        RunConfig b = a; b.fixedBlock = 1;
        RunConfig c = a; c.fixedBlock = 4096;

        const auto oa = run (in, p, bank, a), ob = run (in, p, bank, b), oc = run (in, p, bank, c);
        double maxDiff = 0.0;
        for (size_t n = 0; n < in.size(); ++n)
            maxDiff = std::max ({ maxDiff, (double) std::abs (oa[n] - ob[n]), (double) std::abs (oa[n] - oc[n]) });
        CHECK (maxDiff == 0.0, "outputs differ by %g between block sizes", maxDiff);
        std::printf ("  random vs 1-sample vs 4096-sample blocks (all features + MIDI): max difference %g\n", maxDiff);
    }

    // True bypasses.
    {
        gn::CoreParams p;
        const auto bank = allSteps (T::pitch, gn::kDiv64);
        const auto in = sineSignal (48000 * 2, 123.0);
        RunConfig stopped; stopped.hostPlaying = false;
        CHECK (run (in, p, bank, stopped) == in, "stopped transport changed the audio");
        p.engage = false;
        CHECK (run (in, p, bank) == in, "Engage off changed the audio");
        p.engage = true;
        p.mix = 0.0f;
        CHECK (run (in, p, bank) == in, "Mix 0 changed the audio");
        std::printf ("  stopped transport, Engage off and Mix 0: bit-identical\n");
    }

    // DAW cycle: the grid re-locks on the loop start.
    {
        gn::CoreParams p;
        p.crossfadeMs = 0.0f;
        const auto bank = bankWith ([] (gn::Pattern& pat) { pat[0] = gn::makeStep (T::repeat, gn::kDiv32); });
        RunConfig cfg; cfg.fixedBlock = 512;
        const double spq = 24000.0;
        const size_t loopLen = (size_t) (8.0 * spq);
        const size_t total = loopLen * 3 + 7000;
        std::vector<double> ppq (total);
        for (size_t n = 0; n < total; ++n) ppq[n] = (double) (n % loopLen) / spq;

        const auto out = run (counterSignal (total), p, bank, cfg, &ppq);
        bool ok = true;
        for (size_t L = loopLen; L < total; L += loopLen)
            if (L + 3000 < total && (size_t) out[L + 3000] != L) ok = false;
        CHECK (ok, "grid didn't re-lock after the loop jump");
        std::printf ("  DAW loop: re-locks on every loop start\n");
    }
}

//------------------------------------------------------------------------------
// 9) Live pads use their own settings; knob moves apply to a glitch already playing.
static void testLiveControls()
{
    std::printf ("Live pad settings / controls while playing\n");
    gn::CoreParams p;
    p.crossfadeMs = 0.0f;
    p.liveQuant = 0;
    p.liveDivision = gn::kDiv32;

    // Pad Pitch set to -12 st: the live glitch follows the pad, not the factory default (+12).
    {
        gn::PatternBank bank;
        bank.live[(size_t) T::pitch] = gn::makeStep (T::pitch, gn::kDiv32);
        bank.live[(size_t) T::pitch].p1 = 0.25f; // -12 st
        RunConfig cfg;
        cfg.midi = { { 12000, 0x90, (uint8_t) (60 + (int) T::pitch), 100 }, { 30000, 0x80, (uint8_t) (60 + (int) T::pitch), 0 } };
        const auto out = run (counterSignal (40000), p, bank, cfg);
        double worst = 0.0;
        for (size_t n = 15000; n < 18000; ++n) // first repeat: half speed from 12000
            worst = std::max (worst, std::abs ((double) out[n] - (12000.0 + 0.5 * (double) (n - 15000))));
        CHECK (worst < 0.01, "live pad ignored its own Pitch setting (off by %.3f)", worst);
        std::printf ("  Live pad plays with its own settings (Pitch -12 st on the pad)\n");
    }

    // Level changed halfway through a long tied glitch: it applies straight away (with a glide).
    {
        gn::PatternBank bank;
        bank.patterns[0][0] = gn::makeStep (T::repeat, gn::kDiv16, 1.0f, 16); // one bar-long glitch
        gn::GlitchCore core;
        core.prepare (48000.0, 1);
        p.crossfadeMs = 3.0f;
        std::vector<float> buf (96000, 1.0f);
        gn::HostTime ht; ht.hasPpq = ht.hasBpm = ht.playing = true; ht.bpm = 120.0;
        for (size_t pos = 0; pos < buf.size(); pos += 500)
        {
            if (pos == 48000) bank.patterns[0][0].levelDb = -12.0f; // the "knob move"
            ht.ppq = (double) pos / 24000.0;
            float* ptr = buf.data() + pos;
            core.process (&ptr, 1, 500, p, ht, bank);
        }
        const double before = buf[47000], after = buf[49000];
        CHECK (std::abs (before - 1.0) < 1e-4 && std::abs (after - std::pow (10.0, -12.0 / 20.0)) < 1e-3,
               "level change mid-glitch: %.3f -> %.3f", before, after);
        std::printf ("  Level moved mid-glitch: %.2f -> %.2f within 3 ms, no retrigger needed\n", before, after);
        p.crossfadeMs = 0.0f;
    }
}

//------------------------------------------------------------------------------
// 10) Crusher, delay, reverb.
static void testFx()
{
    std::printf ("Crusher / delay / reverb\n");

    // Per-step crush. Rate /8: every value held for 8 samples. Bits: quiet material keeps its level
    // (the quantiser follows the signal), and more crush = more distortion.
    {
        gn::CoreParams p;
        p.crossfadeMs = 0.0f;
        auto rateOnly = allSteps (T::repeat, gn::kDiv32);
        for (auto& s : rateOnly.patterns[0]) s.rate = 0.5f; // /8
        const auto outR = run (sineSignal (48000, 220.0), p, rateOnly);
        int changes = 0;
        for (size_t n = 24001; n < 30000; ++n) changes += outR[n] != outR[n - 1] ? 1 : 0;
        CHECK (changes <= 6000 / 8 + 2, "rate /8: %d value changes in 6000 samples", changes);

        const auto quiet = [] { auto v = sineSignal (96000, 196.0); for (auto& x : v) x *= 0.0316f; return v; }(); // -30 dBFS
        const auto clean = run (quiet, p, allSteps (T::repeat, gn::kDiv32));
        double lastSnr = 1e9;
        bool levelsOk = true, snrFalls = true;
        std::printf ("  Step crush on a -30 dBFS tone (level change / signal-to-crush-noise):\n   ");
        for (float amt : { 0.25f, 0.5f, 0.75f, 0.9f })
        {
            auto bank = allSteps (T::repeat, gn::kDiv32);
            for (auto& s : bank.patterns[0]) s.bits = amt;
            const auto out = run (quiet, p, bank);
            double sig = 0, err = 0;
            for (size_t n = 24000; n < 96000; ++n) { sig += (double) clean[n] * clean[n]; err += (double) (out[n] - clean[n]) * (out[n] - clean[n]); }
            const double levelDb = 20.0 * std::log10 (rms (out, 24000, 96000) / rms (clean, 24000, 96000));
            const double snr = 10.0 * std::log10 (sig / std::max (err, 1e-20));
            std::printf (" %.1f bit: %+.1f dB / %.0f dB  ", gn::crushBits (amt), levelDb, snr);
            levelsOk &= std::abs (levelDb) < 3.0;
            snrFalls &= snr < lastSnr;
            lastSnr = snr;
        }
        std::printf ("\n");
        CHECK (levelsOk && snrFalls, "step crush: level drop or non-monotonic crush (levels ok %d, snr falls %d)", (int) levelsOk, (int) snrFalls);
        std::printf ("  Step crush: no drop-outs on quiet material, more Bits = more crunch, Rate holds each value\n");
    }

    // Delay: an impulse comes back exactly one 1/8D later, then again at the feedback level; ping-pong alternates.
    {
        gn::CoreParams p;
        p.fxSource = 1;       // everything
        p.delayMix = 1.0f;
        p.delayFeedback = 0.5f;
        p.delayTone = 0.0f;
        p.delayPingPong = false;
        gn::PatternBank empty;
        std::vector<float> imp (60000, 0.0f);
        imp[1000] = 1.0f;
        RunConfig cfg; cfg.fixedBlock = 480; // mix ramps settle before the impulse
        auto both = runAll (imp, p, empty, cfg);
        const size_t d = 18000; // 1/8D at 120 BPM
        const bool ok = std::abs (both[0][1000 + d] - 1.0f) < 1e-3 && std::abs (both[0][1000 + 2 * d] - 0.5f / 1.125f) < 2e-2
                        && std::abs (both[0][1000 + d - 1]) < 1e-6;
        CHECK (ok, "delay echoes %.3f / %.3f", both[0][1000 + d], both[0][1000 + 2 * d]);

        p.delayPingPong = true;
        both = runAll (imp, p, empty, cfg);
        const bool pp = both[0][1000 + d] > 0.4f && std::abs (both[1][1000 + d]) < 1e-6
                        && both[1][1000 + 2 * d] > 0.1f && std::abs (both[0][1000 + 2 * d]) < 1e-6;
        CHECK (pp, "ping-pong L/R %.3f/%.3f then %.3f/%.3f", both[0][1000 + d], both[1][1000 + d], both[0][1000 + 2 * d], both[1][1000 + 2 * d]);
        std::printf ("  Delay: echo lands exactly on the 1/8D, repeats at the feedback level, ping-pong alternates L/R\n");
    }

    // Glitch-only source: no glitch, no echoes. A glitch: echoes of the glitch.
    {
        gn::CoreParams p;
        p.delayMix = 1.0f;
        const auto in = sineSignal (96000, 300.0);
        gn::PatternBank empty;
        CHECK (run (in, p, empty) == in, "glitch-only delay echoed the dry signal");
        const auto out = run (in, p, allSteps (T::repeat, gn::kDiv32));
        CHECK (out != in, "glitch-only delay didn't echo the glitch");
        std::printf ("  Glitch-only send: the dry signal never reaches the delay, glitches do\n");
    }

    // Reverb: rings, decays, stays finite and stereo.
    {
        gn::CoreParams p;
        p.fxSource = 1;
        p.reverbMix = 1.0f;
        p.reverbSize = 0.5f;
        gn::PatternBank empty;
        std::vector<float> imp (48000 * 4, 0.0f);
        imp[2000] = 1.0f;
        const auto both = runAll (imp, p, empty, RunConfig {});
        bool finite = true;
        for (const auto& ch : both) for (float v : ch) finite &= std::isfinite (v);
        const double early = rms (both[0], 4000, 28000), late = rms (both[0], 150000, 190000);
        const double right = rms (both[1], 4000, 28000);
        const double decayDb = 20.0 * std::log10 (late / early);
        const double rt60 = 0.35 + 9.0 * std::pow (0.5, 1.6);
        const double expectDb = -60.0 * (3.5 - 0.33) / rt60; // between the two windows
        CHECK (finite && right > 1e-4 && late < early && std::abs (decayDb - expectDb) < 12.0,
               "reverb decay %.1f dB (expected about %.1f)", decayDb, expectDb);
        std::printf ("  Reverb: stereo, stable, decays %.0f dB over 3.2 s (design: %.0f dB)\n", decayDb, expectDb);
    }

    // Global crusher on everything keeps quiet material's level; FX at 0 is a true bypass.
    {
        gn::CoreParams p;
        p.crushBits = 0.9f; // ~1.4 bits
        p.crushMix = 1.0f;
        gn::PatternBank empty;
        auto quiet = sineSignal (48000, 200.0);
        for (auto& x : quiet) x *= 0.0316f;
        const auto out = run (quiet, p, empty, RunConfig { 48000.0, 120.0, true, true, 2, 480, {} });
        const double levelDb = 20.0 * std::log10 (rms (out, 4800, 48000) / rms (quiet, 4800, 48000));
        CHECK (std::abs (levelDb) < 3.0 && out != quiet, "global crusher level %.1f dB", levelDb);

        gn::CoreParams off;
        off.delayFeedback = 0.9f; off.reverbSize = 1.0f; off.fxSource = 1; off.crushBits = 1.0f; // all mixes at 0
        const auto in = sineSignal (48000, 200.0);
        CHECK (run (in, off, empty) == in, "FX with mix 0 changed the audio");
        std::printf ("  Global crusher at ~1.4 bit on a -30 dBFS tone: level %+.1f dB (no drop-out); FX at mix 0 = bit-identical\n", levelDb);
    }

    // Everything on at once: no allocations, block-size independent.
    {
        gn::CoreParams p;
        p.delayMix = 0.6f; p.reverbMix = 0.5f; p.crushBits = 0.3f; p.crushRate = 0.2f; p.crushMix = 0.5f;
        auto bank = allSteps (T::shuffle, gn::kDiv32);
        for (int i = 0; i < 16; ++i) { auto& s = bank.patterns[0][(size_t) i]; s.bits = (float) (i % 4) * 0.2f; s.rate = (float) (i % 3) * 0.25f; s.send = (float) (i % 2); }
        const auto in = sineSignal (48000 * 3, 410.0);
        RunConfig a; RunConfig b = a; b.fixedBlock = 1; RunConfig c = a; c.fixedBlock = 4096;
        gAllocCount = 0;
        const auto oa = run (in, p, bank, a), ob = run (in, p, bank, b), oc = run (in, p, bank, c);
        CHECK (gAllocCount == 0, "%d allocations with FX on", gAllocCount.load());
        double maxDiff = 0.0;
        for (size_t n = 0; n < in.size(); ++n) maxDiff = std::max ({ maxDiff, (double) std::abs (oa[n] - ob[n]), (double) std::abs (oa[n] - oc[n]) });
        CHECK (maxDiff == 0.0, "FX outputs differ by %g between block sizes", maxDiff);
        std::printf ("  All FX on: no allocations, identical output for any block size\n");
    }
}

//------------------------------------------------------------------------------
// 11) Layers: a step inside a tie that's on stacks its glitch on top of the tied one.
static void testLayers()
{
    std::printf ("Layers (stacked glitches inside a tie)\n");
    gn::CoreParams p;
    p.crossfadeMs = 0.0f;

    // Step 1 = Pitch -12, tied over 4 steps (repeats of 1/16). Step 3 (inside the tie) = Repeat 1/32:
    // it records what the Pitch glitch is playing and loops it - a roll of the pitched sound.
    const auto bank = bankWith ([] (gn::Pattern& pat)
    {
        pat[0] = gn::makeStep (T::pitch, gn::kDiv16, 1.0f, 4);
        pat[0].p1 = 0.25f; // -12 st
        pat[2] = gn::makeStep (T::repeat, gn::kDiv32);
    });
    const auto out = run (counterSignal (30000), p, bank);
    auto pitched = [] (size_t n) { return n < 6000 ? (double) n : 0.5 * (double) ((n - 6000) % 6000); };
    bool ok = true;
    for (size_t n = 0; n < 24000; ++n)
    {
        double expect = pitched (n);
        if (n >= 15000 && n < 18000) expect = pitched (n - 3000); // the layer's repeat of the pitched audio
        ok &= std::abs ((double) out[n] - expect) < 0.01;
    }
    CHECK (ok, "layer didn't stack on the tied glitch");
    std::printf ("  Repeat inside a Pitch tie loops the pitched sound, then the tie carries on underneath\n");
}

int main()
{
    testGridAccuracy (48000.0, 120.0, 2, gn::kDiv32, 0.5,  "48k, 120 BPM, grid 1/16, repeat 1/32");
    testGridAccuracy (44100.0, 133.0, 5, gn::kDiv64, 0.5,  "44.1k, 133 BPM, grid 1/16T, repeat 1/64");
    testGridAccuracy (96000.0, 97.3, 1, 4, 0.5,            "96k, 97.3 BPM, grid 1/8, repeat 1/8T");
    testGridAccuracy (88200.0, 174.0, 3, gn::kDiv64, 0.5,  "88.2k, 174 BPM, grid 1/32, repeat 1/64");
    testGridAccuracy (48000.0, 90.0, 0, gn::kDiv16D, 0.5,  "48k, 90 BPM, grid 1/4, repeat 1/16D");
    testGridAccuracy (48000.0, 120.0, 2, gn::kDiv64, 0.66, "48k, 120 BPM, grid 1/16, swing 66%");
    testGridAccuracy (44100.0, 101.0, 1, gn::kDiv32, 0.75, "44.1k, 101 BPM, grid 1/8, swing 75%");
    testTypeDefaults();
    testTypeControls();
    testNewTypes();
    testStepShaping();
    testRandomness();
    testLive();
    testSafety();
    testLiveControls();
    testFx();
    testLayers();

    std::printf (gFailures == 0 ? "\nALL TESTS PASSED\n" : "\n%d FAILURE(S)\n", gFailures);
    return gFailures == 0 ? 0 : 1;
}
