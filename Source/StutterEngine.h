#pragma once
//==============================================================================
// Glitch Nasty - StutterEngine
//
// Think of this as a tape loop that is ALWAYS recording, with a playback head
// that normally sits right on top of the record head (so you hear the input
// with zero delay). When a stutter starts, we remember where the record head
// was ("sliceStart"). For most types the first pass just rides along with the
// record head, so you still hear live audio - that's why there's no latency.
// At each loop point the playback head jumps somewhere in the audio that has
// already been recorded, and the TYPE decides where, how fast and how loud:
//
//   Repeat     the same slice again (Offset: which slice, Variation: humanize)
//   Reverse    the slice backwards (or ping-pong back/forth)
//   Gate       no jumps: the live signal is chopped on/off (duty, shape)
//   Roll       each repeat shorter (speed up) or longer (slow down)
//   Pitch      repeats played faster/slower (+-24 st), with optional glide
//   Tape Stop  no jumps: playback slows to a halt (or spins up from nothing)
//   Fade       each repeat quieter (or louder: fade in)
//   Shuffle    each slice replaced by an earlier slice, maybe reversed
//   Freeze     two overlapping windowed grains hold a moment as a drone
//   Scratch    the head is dragged back and forth like vinyl
//   Ping-Pong  repeats alternate left/right
//
// Every jump is covered by a short crossfade (an angled tape splice, not a
// cut). After the type, each step's level, pan and filter shape the wet sound.
//
// Real-time safety: all memory is allocated in prepare(). process() never
// allocates, locks or does I/O. No JUCE dependency, so the offline tests can
// compile it on their own.
//==============================================================================

#include "Effects.h"
#include "Pattern.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace gn
{

// Small, fast, deterministic hash -> random numbers that depend only on the
// seed and on where we are, so a bounce always sounds the same.
inline uint32_t hash32 (uint32_t x) noexcept
{
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

inline float rand01 (uint32_t seed, uint32_t a, uint32_t b = 0) noexcept
{
    const uint32_t h = hash32 (seed ^ hash32 (a * 0x9E3779B9u + hash32 (b + 0x632BE5ABu)));
    return (float) (h >> 8) * (1.0f / 16777216.0f);
}

struct StutterSettings
{
    StutterType type = StutterType::repeat;
    double sliceLen = 0.0;   // repeat length, in samples (may be fractional)
    double stepLen = 0.0;    // how long this glitch lasts (incl. ties), in samples
    double lateBy = 0.0;     // the true grid line was this far (0..1 samples) before this sample
    double minPeriod = 32.0; // shortest repeat a Roll may reach, in samples
    int fadeSamples = 0;     // crossfade length at every jump
    float p1 = 0.0f, p2 = 0.0f; // the type's two controls (0..1)
    float level = 1.0f;      // linear gain
    float pan = 0.0f;        // -1..+1
    float filter = 0.0f;     // -1 (LP) .. +1 (HP)
    float bits = 0.0f;       // crusher bit reduction (0 = off)
    float rate = 0.0f;       // crusher rate reduction (0 = off)
    float send = 1.0f;       // delay/reverb send (0..1)
    uint32_t seed = 0;       // for Variation / Shuffle / Freeze texture
};

// The controls that can change while a glitch is already playing (knob moves,
// automation): they glide, they don't wait for the next trigger.
struct LiveControls
{
    float p1 = 0.0f, p2 = 0.0f, level = 1.0f, pan = 0.0f, filter = 0.0f, bits = 0.0f, rate = 0.0f, send = 1.0f;
};

class StutterEngine
{
public:
    static constexpr int kMaxChannels = 2;

    // Call from prepareToPlay (allocates). bufferSeconds = longest audio we can reach back for.
    void prepare (int numChannels, double sampleRate, double bufferSeconds)
    {
        channels = std::clamp (numChannels, 1, kMaxChannels);
        sr = sampleRate;

        // Ring buffer sized to a power of two so wrapping is a cheap bit-mask.
        const auto needed = (int64_t) std::ceil (sampleRate * bufferSeconds) + kSafetySamples;
        int64_t cap = 1;
        while (cap < needed)
            cap <<= 1;

        capacity = cap;
        mask = cap - 1;

        for (auto& b : buffers)
            b.assign ((size_t) capacity, 0.0f);

        mixSmoothingSamples = std::max (1, (int) (0.02 * sampleRate)); // 20 ms
        filterSmooth = (float) std::exp (-1.0 / (0.005 * sampleRate));  // 5 ms
        crusher.prepare (sampleRate);
        reset();
    }

    void reset()
    {
        for (auto& b : buffers)
            std::fill (b.begin(), b.end(), 0.0f);

        writeCount = 0;
        head = Head {};
        fadeHead = Head {};
        fadeLen = fadeRemaining = 0;
        fadeLinear = false;
        stuttering = false;
        type = StutterType::repeat;
        sliceStart = 0;
        sliceLen = stepLen = gridPhase = 0.0;
        regionStart = oldestNeeded = 0.0;
        elapsed = 0;
        repeatIndex = 0;
        nextWrapT = 0.0;
        nextWrap = std::numeric_limits<int64_t>::max();
        gateGain = 1.0f;
        gateUp = gateDown = 1.0f;
        lpAmt = hpAmt = lpAmtT = hpAmtT = 0.0f;
        lpLog = lpLogT = std::log (20000.0f);
        hpLog = hpLogT = std::log (20.0f);
        for (auto& f : lpState) f = {};
        for (auto& f : hpState) f = {};
        shape = shapeTarget = { 1.0f, 1.0f };
        shapeStep = { 0.0f, 0.0f };
        shapeRamp = 0;
        sendAmt = sendTarget = sendStep = 0.0f;
        sendRamp = 0;
        actAmt = actTarget = actStep = 0.0f;
        actRamp = 0;
        crushAmt = crushAmtT = 0.0f;
        crushBitsNow = 24.0;
        crushFactorNow = 1.0;
        crusher.reset();
        repeatStartElapsed = 0;
        mixCurrent = mixTarget;
        mixStep = 0.0f;
        mixRampLeft = 0;
        mixInitialised = false;
    }

    // Wet amount while glitching (0..1). Smoothed internally over 20 ms.
    void setMix (float newMix)
    {
        newMix = std::clamp (newMix, 0.0f, 1.0f);

        if (! mixInitialised) // first value after a reset: jump straight there, no ramp
        {
            mixInitialised = true;
            mixTarget = mixCurrent = newMix;
            mixRampLeft = 0;
            return;
        }

        if (std::abs (newMix - mixTarget) < 1.0e-6f)
            return;

        mixTarget = newMix;
        mixRampLeft = mixSmoothingSamples;
        mixStep = (mixTarget - mixCurrent) / (float) mixRampLeft;
    }

    // Start a new glitch at the next sample process() handles.
    void startStutter (const StutterSettings& s)
    {
        type = s.type;
        p1 = s.p1;
        p2 = s.p2;
        seed = s.seed;
        level = std::max (0.0f, s.level);
        pan = std::clamp (s.pan, -1.0f, 1.0f);
        sliceLen = std::clamp (s.sliceLen, 16.0, maxSliceSamples());
        stepLen = s.stepLen;
        gridPhase = s.lateBy;
        fadeSamples = std::max (s.fadeSamples, 0);
        minPeriod = std::max (16.0, s.minPeriod);
        slicesPerStep = slicesIn (stepLen, sliceLen);

        const double S = (double) writeCount;
        sliceStart = writeCount;
        regionStart = S;
        oldestNeeded = S;
        elapsed = 0;
        repeatIndex = 0;
        nextWrapT = sliceLen;
        bool loops = true;

        Head h = liveHead (S);

        switch (type)
        {
            case StutterType::repeat:
            case StutterType::reverse:
            {
                const int o = sliceOffset (type == StutterType::repeat ? p1 : p2, slicesPerStep);
                regionStart = S + (double) o * sliceLen;
                nextWrapT = (double) (o + 1) * sliceLen;
                oldestNeeded = S - (type == StutterType::repeat ? 0.25 * sliceLen : 0.0);
                break;
            }
            case StutterType::gate:
                loops = false;
                break;
            case StutterType::tapeStop:
                loops = false;
                if (choiceIndex (p1, 2) == 1)
                    h.rate = 0.0; // tape start: spins up from a standstill
                break;
            case StutterType::fade:
                if (choiceIndex (p2, 2) == 1) // fade in: the first pass is the quietest
                    h.amp = (float) std::pow (fadeFactor (p1), (double) (slicesPerStep - 1));
                break;
            case StutterType::shuffle:
                oldestNeeded = S - (double) (slicesPerStep - 1) * sliceLen;
                break;
            case StutterType::freeze:
            {
                loops = false;
                const double G = std::clamp (freezeGrainMs (p1) * 0.001 * sr, 16.0, sr);
                h.mode = HeadMode::freeze;
                h.len = G;
                h.base = S - G;          // the grain is the moment just before the step
                h.t = 0.0;
                h.texture = clamp01 (p2);
                h.seed = seed;
                oldestNeeded = S - 2.0 * G;
                break;
            }
            case StutterType::scratch:
            {
                loops = false;
                const double A = std::max (16.0, scratchDepth (p1) * sliceLen);
                h.mode = HeadMode::scratch;
                h.len = A;
                h.base = S - A;          // swings between S - A and S (never the future)
                h.period = sliceLen / scratchSpeed (p2);
                h.t = 0.5 * h.period;    // start at the top of the swing, where pos == S
                h.pos = scratchPos (h);
                oldestNeeded = S - A;
                break;
            }
            case StutterType::roll:
            case StutterType::pitch:
            case StutterType::pingPong:
                break;
        }

        // Shuffle can reach back several slices; never further than the buffer holds.
        maxLookbackSlices = std::max (0, (int) ((double) (capacity - 2 * kSafetySamples) / sliceLen) - 2);

        const bool alreadyThere = ! stuttering && fadeRemaining == 0 && isPlainLive (head) && isPlainLive (h)
                                  && std::abs (h.pos - head.pos) < 0.5;
        if (alreadyThere)
            head = h;
        else
            beginJump (h, fadeSamples);

        stuttering = true;
        scheduleWrap (loops);

        // Gate edges
        if (type == StutterType::gate)
        {
            const double duty = gateDuty (p1);
            const double shortest = sliceLen * std::min (duty, 1.0 - duty);
            switch (choiceIndex (p2, 3))
            {
                case 0:  gateUp = gateDown = rampStep (std::min ((double) fadeSamples, shortest * 0.5)); break; // hard
                case 1:  gateUp = gateDown = rampStep (shortest * 0.5); break;                                 // soft
                default: gateUp = 1.0f; gateDown = rampStep (std::min ((double) fadeSamples, sliceLen * 0.05)); break; // pump
            }
        }
        else
        {
            gateUp = gateDown = rampStep ((double) fadeSamples);
        }

        setFilterTarget (s.filter);
        LiveControls lc { p1, p2, level, pan, s.filter, s.bits, s.rate, s.send };
        setShaping (lc);
        setActivityTarget (1.0f);
    }

    // Knob moves / automation while the glitch plays.
    void updateControls (const LiveControls& c) noexcept
    {
        if (! stuttering)
            return;
        p1 = c.p1;
        p2 = c.p2;
        setFilterTarget (c.filter);
        setShaping (c);
    }

    // Return to the live input at the next sample (crossfaded).
    void stopStutter (int fadeSamplesForReturn)
    {
        if (! stuttering)
            return;

        stuttering = false;
        beginJump (liveHead ((double) writeCount), fadeSamplesForReturn);
        gateUp = gateDown = rampStep ((double) std::max (fadeSamplesForReturn, 0));
        setFilterTarget (0.0f);
        fadeSamples = std::max (fadeSamplesForReturn, 0);
        setShaping (LiveControls { p1, p2, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f });
        setActivityTarget (0.0f);
    }

    bool isStuttering() const noexcept { return stuttering; }
    int64_t getCapacity() const noexcept { return capacity; }
    double maxSliceSamples() const noexcept { return (double) (capacity - 2 * kSafetySamples) * 0.25; }

    // Processes io[ch][start .. start+num) in place. If 'send' is given, the
    // glitch audio (times each step's Send) is written there for the delay/reverb.
    // 'activity' (optional, mono) gets how much this engine is glitching, 0..1, per sample.
    void process (float* const* io, int numCh, int start, int num, float* const* send = nullptr, float* activity = nullptr) noexcept
    {
        numCh = std::min (numCh, channels);
        constexpr double halfPi = 1.57079632679489661923;

        for (int i = start; i < start + num; ++i)
        {
            const int64_t w = writeCount;

            // 1) Record (the "tape" is always running).
            for (int c = 0; c < numCh; ++c)
                buffers[(size_t) c][(size_t) (w & mask)] = io[c][i];

            if (mixRampLeft > 0)
            {
                mixCurrent += mixStep;
                if (--mixRampLeft == 0)
                    mixCurrent = mixTarget;
            }

            // 2) Never play audio the tape has already recorded over: very long holds return to live.
            if (stuttering && (double) w - oldestNeeded > (double) (capacity - 2 * kSafetySamples))
                stopStutter (fadeSamples);

            // 3) Gate level (slews toward open/closed).
            float gateTarget = 1.0f;
            if (stuttering && type == StutterType::gate)
            {
                const double phase = std::fmod ((double) elapsed + gridPhase, sliceLen) / sliceLen;
                const double duty = gateDuty (p1);
                if (choiceIndex (p2, 3) == 2)
                {
                    const double x = std::min (1.0, phase / duty);
                    gateTarget = (float) (x * x);
                }
                else
                {
                    gateTarget = phase < duty ? 1.0f : 0.0f;
                }
            }
            if (gateGain < gateTarget)      gateGain = std::min (gateTarget, gateGain + gateUp);
            else if (gateGain > gateTarget) gateGain = std::max (gateTarget, gateGain - gateDown);

            // 4) Filter amounts and cutoffs glide (5 ms) so step changes never click.
            const bool filterActive = updateFilterSmoothing();

            const bool crushing = updateCrushSmoothing(); // always advance (never short-circuited)
            const bool shaping = crushing || shapeRamp > 0 || ! exactly (shape[0], 1.0f) || ! exactly (shape[1], 1.0f)
                                 || sendRamp > 0 || ! exactly (sendAmt, 0.0f) || actRamp > 0 || ! exactly (actAmt, 0.0f);

            // 5) Fast path: nothing going on -> output == input, untouched (bit-identical).
            if (! stuttering && fadeRemaining == 0 && gateGain >= 1.0f && ! filterActive && ! shaping)
            {
                if (send != nullptr)
                    for (int c = 0; c < numCh; ++c) send[c][i] = 0.0f;
                if (activity != nullptr)
                    activity[i] = 0.0f;
                ++writeCount;
                head = liveHead ((double) writeCount);
                continue;
            }

            if (shapeRamp > 0)
            {
                shape[0] += shapeStep[0];
                shape[1] += shapeStep[1];
                if (--shapeRamp == 0) shape = shapeTarget;
            }
            if (sendRamp > 0)
            {
                sendAmt += sendStep;
                if (--sendRamp == 0) sendAmt = sendTarget;
            }
            if (actRamp > 0)
            {
                actAmt += actStep;
                if (--actRamp == 0) actAmt = actTarget;
            }
            if (activity != nullptr)
                activity[i] = actAmt;

            // 6) Loop point reached? Start the next repeat.
            if (stuttering && elapsed >= nextWrap)
                startNextRepeat();

            // 7) Types that move continuously.
            if (stuttering)
                updateContinuous();

            // 8) Play back (with crossfade if one is running).
            float gIn = 1.0f, gOut = 0.0f;
            const bool fading = fadeRemaining > 0;
            if (fading)
            {
                const double t = ((double) (fadeLen - fadeRemaining) + 0.5) / (double) fadeLen;
                if (fadeLinear) { gIn = (float) t; gOut = (float) (1.0 - t); }       // same audio, new gain
                else            { gIn = (float) std::sin (t * halfPi); gOut = (float) std::cos (t * halfPi); }
            }

            float wet[kMaxChannels] {};
            for (int c = 0; c < numCh; ++c)
            {
                float v = readHead (c, head) * chanGain (head, c);
                if (fading)
                    v = v * gIn + readHead (c, fadeHead) * chanGain (fadeHead, c) * gOut;
                v *= gateGain * shape[(size_t) c];
                if (filterActive)
                    v = applyFilter (c, v);
                wet[c] = v;
            }

            if (crushAmt > 0.0f)
            {
                float crushed[kMaxChannels] { wet[0], wet[1] };
                crusher.processFrame (crushed, numCh, crushFactorNow, crushBitsNow);
                for (int c = 0; c < numCh; ++c)
                    wet[c] = blend (wet[c], crushed[c], crushAmt);
            }

            for (int c = 0; c < numCh; ++c)
            {
                const float dry = io[c][i];
                io[c][i] = blend (dry, wet[c], mixCurrent);
                if (send != nullptr)
                    send[c][i] = wet[c] * sendAmt;
            }

            advance (head);
            if (fading)
            {
                advance (fadeHead);
                --fadeRemaining;
            }

            ++elapsed;
            ++writeCount;
            if (! stuttering && fadeRemaining == 0)
                head = liveHead ((double) writeCount); // back on the record head, exactly
        }
    }

private:
    static constexpr int64_t kSafetySamples = 4096;

    enum class HeadMode : uint8_t { linear, freeze, scratch };

    struct Head
    {
        HeadMode mode = HeadMode::linear;
        double pos = 0.0;  // absolute playback position (in samples recorded so far)
        double rate = 1.0; // 1 = normal speed, 2 = octave up, -1 = backwards
        float gL = 1.0f, gR = 1.0f, amp = 1.0f;
        // freeze / scratch
        double base = 0.0, len = 0.0, t = 0.0, period = 0.0;
        float texture = 0.0f;
        uint32_t seed = 0;
        // pitch: loop inside [regionStart, regionStart + regionLen)
        bool wrapInRegion = false;
        double regionStart = 0.0, regionLen = 0.0;
    };

    struct SvfState { float ic1 = 0.0f, ic2 = 0.0f; };

    static Head liveHead (double pos) noexcept
    {
        Head h;
        h.pos = pos;
        return h;
    }

    static bool isPlainLive (const Head& h) noexcept
    {
        return h.mode == HeadMode::linear && exactly (h.rate, 1.0) && exactly (h.gL, 1.0f) && exactly (h.gR, 1.0f)
               && exactly (h.amp, 1.0f) && ! h.wrapInRegion;
    }

    static float rampStep (double rampSamples) noexcept
    {
        return rampSamples >= 1.0 ? (float) (1.0 / rampSamples) : 1.0f;
    }

    // Balance law: centre = unity on both sides (so a centred step is transparent).
    void setGains (Head& h, float lvl, float p) const noexcept
    {
        if (channels < 2)
            p = 0.0f;
        h.gL = lvl * (p > 0.0f ? 1.0f - p : 1.0f);
        h.gR = lvl * (p < 0.0f ? 1.0f + p : 1.0f);
    }

    static float chanGain (const Head& h, int c) noexcept { return (c == 0 ? h.gL : h.gR) * h.amp; }

    static double scratchPos (const Head& h) noexcept
    {
        constexpr double twoPi = 6.28318530717958647692;
        return h.base + h.len * 0.5 * (1.0 - std::cos (twoPi * h.t / h.period));
    }

    static void advance (Head& h) noexcept
    {
        switch (h.mode)
        {
            case HeadMode::linear:  h.pos += h.rate; break;
            case HeadMode::freeze:  h.t += 1.0; break;
            case HeadMode::scratch: h.t += 1.0; h.pos = scratchPos (h); break;
        }
    }

    float readHead (int c, const Head& h) const noexcept
    {
        if (h.mode != HeadMode::freeze)
            return read (c, h.pos);

        // Two Hann-windowed grains half a grain apart: sin^2 + cos^2 = 1, so the
        // level stays put. Each grain may wander backwards (texture) when it restarts.
        constexpr double pi = 3.14159265358979323846;
        float acc = 0.0f;
        for (int g = 0; g < 2; ++g)
        {
            const double cyc = h.t / h.len + 0.5 * (double) g;
            const double k = std::floor (cyc);
            const double ph = cyc - k;
            const double s = std::sin (pi * ph);
            const double jitter = (double) h.texture * h.len * (double) rand01 (h.seed, 300u + (uint32_t) g, (uint32_t) (int64_t) k);
            acc += (float) (s * s) * read (c, h.base - jitter + ph * h.len);
        }
        return acc;
    }

    void startNextRepeat() noexcept
    {
        ++repeatIndex;
        const int k = (int) std::min<int64_t> (repeatIndex, 1 << 20);
        const double S = (double) sliceStart;

        Head h = liveHead (regionStart);
        double per = sliceLen;
        repeatStartElapsed = elapsed;

        switch (type)
        {
            case StutterType::repeat:
                h.pos = regionStart - (double) p2 * 0.25 * sliceLen * (double) rand01 (seed, 101u, (uint32_t) k);
                break;

            case StutterType::reverse:
                if (choiceIndex (p1, 2) == 1)
                {
                    // Ping-pong: back from the middle of the slice to its start, then
                    // forward again - a yo-yo inside every repeat, no jump at the turn.
                    h.pos = regionStart + std::floor (sliceLen * 0.5);
                    h.rate = -1.0;
                }
                else
                {
                    h.pos = regionStart + sliceLen - 1.0;
                    h.rate = -1.0;
                }
                break;

            case StutterType::roll:
            {
                const double f = rollFactor (p2);
                per = choiceIndex (p1, 2) == 0 ? std::max (minPeriod, sliceLen * std::pow (f, (double) k))
                                               : sliceLen * std::pow (1.0 / f, (double) k);
                h.pos = S;
                break;
            }

            case StutterType::pitch:
                h.pos = S;
                h.rate = pitchRate();
                h.wrapInRegion = true; // faster than real time: loop inside the slice
                h.regionStart = S;
                h.regionLen = sliceLen;
                break;

            case StutterType::fade:
            {
                const double f = fadeFactor (p1);
                const int n = choiceIndex (p2, 2) == 0 ? k : std::max (0, slicesPerStep - 1 - k);
                h.pos = S;
                h.amp = (float) std::pow (f, (double) n);
                break;
            }

            case StutterType::shuffle:
            {
                int j = k; // this slice, live
                const int back = std::min (slicesPerStep - 1, maxLookbackSlices);
                if (back > 0 && rand01 (seed, 202u, (uint32_t) k) < p1)
                    j = k - 1 - std::min (back - 1, (int) (rand01 (seed, 203u, (uint32_t) k) * (float) back));

                if (j == k)
                {
                    h.pos = (double) writeCount;
                }
                else
                {
                    h.pos = S + (double) j * sliceLen;
                    if (rand01 (seed, 204u, (uint32_t) k) < p2)
                    {
                        h.pos += sliceLen - 1.0;
                        h.rate = -1.0;
                    }
                }
                break;
            }

            case StutterType::pingPong:
            {
                const float side = (k % 2 == 1) ? -1.0f : 1.0f;
                setGains (h, 1.0f, side * clamp01 (p1));
                h.pos = S;
                h.amp = (float) std::pow (pingDecay (p2), (double) k);
                break;
            }

            case StutterType::gate:
            case StutterType::tapeStop:
            case StutterType::freeze:
            case StutterType::scratch:
                break; // never loop
        }

        const int loopFade = std::min (fadeSamples, (int) (per * 0.5));
        beginJump (h, loopFade);

        nextWrapT += per;
        scheduleWrap (true);
    }

    double pitchRate() const noexcept
    {
        const double progress = stepLen > 0.0 ? std::min (1.0, (double) elapsed / stepLen) : 0.0;
        const double semis = pitchSemis (p1) + pitchSemis (p2) * progress;
        return std::clamp (std::exp2 (semis / 12.0), 0.25, 4.0);
    }

    void updateContinuous() noexcept
    {
        switch (type)
        {
            case StutterType::tapeStop:
            {
                const double x = std::min (1.0, (double) elapsed / sliceLen);
                const double e = tapeCurve (p2);
                const double r = choiceIndex (p1, 2) == 1 ? std::pow (x, e) : std::pow (1.0 - x, e);
                head.rate = r;
                head.amp = (float) std::min (1.0, r * 4.0); // level drops in the last quarter, like a real deck
                break;
            }

            case StutterType::pitch:
                if (repeatIndex > 0)
                {
                    head.rate = pitchRate();
                    if (head.wrapInRegion && head.pos >= head.regionStart + head.regionLen)
                    {
                        Head h = head;
                        h.pos -= head.regionLen;
                        beginJump (h, std::min (fadeSamples, (int) (head.regionLen * 0.25 / head.rate)));
                    }
                }
                break;

            case StutterType::reverse:
                if (repeatIndex > 0 && choiceIndex (p1, 2) == 1 && head.rate < 0.0
                    && (double) (elapsed - repeatStartElapsed) >= std::floor (sliceLen * 0.5))
                    head.rate = 1.0; // the turn of the yo-yo
                break;

            case StutterType::repeat:
            case StutterType::gate:
            case StutterType::roll:
            case StutterType::fade:
            case StutterType::shuffle:
            case StutterType::freeze:
            case StutterType::scratch:
            case StutterType::pingPong:
                break;
        }
    }

    // A loop point that would land on (or within half a sample of) the end of
    // the glitch is skipped: the next step takes over there anyway.
    void scheduleWrap (bool loops) noexcept
    {
        nextWrap = (! loops || nextWrapT > stepLen - 0.5)
                       ? std::numeric_limits<int64_t>::max()
                       : (int64_t) std::llround (nextWrapT - gridPhase);
    }

    void beginJump (const Head& incoming, int len) noexcept
    {
        if (len <= 0)
        {
            head = incoming;
            fadeRemaining = 0;
            return;
        }

        // If a previous crossfade is still under way, fade out whichever head
        // is currently louder, so a jump-on-a-jump stays as smooth as possible.
        Head outgoing = head;
        if (fadeRemaining > 0 && (fadeLen - fadeRemaining) * 2 < fadeLen)
            outgoing = fadeHead;

        // Same audio on both sides (only gain/pan/speed changing): a linear fade
        // keeps the level flat. Different audio: an equal-power fade does.
        fadeLinear = outgoing.mode == HeadMode::linear && incoming.mode == HeadMode::linear
                     && std::abs (outgoing.pos - incoming.pos) < 1.0;

        fadeHead = outgoing;
        head = incoming;
        fadeLen = len;
        fadeRemaining = len;
    }

    float read (int c, double pos) const noexcept
    {
        // Linear interpolation: exact when pos is a whole sample, smooth for varispeed.
        const auto i0 = (int64_t) std::floor (pos);
        const auto frac = (float) (pos - (double) i0);
        const auto& b = buffers[(size_t) c];
        const float a = b[(size_t) (i0 & mask)];
        if (frac <= 0.0f)
            return a;
        const float n = b[(size_t) ((i0 + 1) & mask)];
        return a + frac * (n - a);
    }

    //==========================================================================
    // Level / pan / send glide linearly over the crossfade time; the crusher fades in/out.
    void setShaping (const LiveControls& c) noexcept
    {
        const float lvl = std::max (0.0f, c.level);
        const float pn = channels < 2 ? 0.0f : std::clamp (c.pan, -1.0f, 1.0f);
        const std::array<float, 2> target { lvl * (pn > 0.0f ? 1.0f - pn : 1.0f), lvl * (pn < 0.0f ? 1.0f + pn : 1.0f) };
        const int ramp = std::max (1, fadeSamples);

        if (! exactly (target[0], shapeTarget[0]) || ! exactly (target[1], shapeTarget[1]))
        {
            shapeTarget = target;
            if (fadeSamples <= 0) { shape = target; shapeRamp = 0; }
            else
            {
                shapeStep = { (target[0] - shape[0]) / (float) ramp, (target[1] - shape[1]) / (float) ramp };
                shapeRamp = ramp;
            }
        }

        const float sendT = clamp01 (c.send);
        if (! exactly (sendT, sendTarget))
        {
            sendTarget = sendT;
            if (fadeSamples <= 0) { sendAmt = sendT; sendRamp = 0; }
            else { sendStep = (sendT - sendAmt) / (float) ramp; sendRamp = ramp; }
        }

        const bool crushOn = c.bits > 0.001f || c.rate > 0.001f;
        crushAmtT = crushOn ? 1.0f : 0.0f;
        if (crushOn)
        {
            crushBitsNow = c.bits > 0.001f ? crushBits (c.bits) : 24.0;
            crushFactorNow = c.rate > 0.001f ? crushRateFactor (c.rate) : 1.0;
            if (exactly (crushAmt, 0.0f)) crusher.reset();
        }
        crushStep = rampStep ((double) fadeSamples);
    }

    void setActivityTarget (float t) noexcept
    {
        if (exactly (t, actTarget))
            return;
        actTarget = t;
        if (fadeSamples <= 0) { actAmt = t; actRamp = 0; return; }
        actRamp = fadeSamples;
        actStep = (t - actAmt) / (float) fadeSamples;
    }

    bool updateCrushSmoothing() noexcept
    {
        if (exactly (crushAmt, crushAmtT))
            return crushAmt > 0.0f;
        crushAmt = crushAmt < crushAmtT ? std::min (crushAmtT, crushAmt + crushStep) : std::max (crushAmtT, crushAmt - crushStep);
        return true;
    }

    //==========================================================================
    // Filter: two state-variable filters (low-pass and high-pass, like a DJ
    // mixer's filter knob) on the wet signal, faded in and out with the glitch.
    void setFilterTarget (float f) noexcept
    {
        lpAmtT = f < -0.001f ? 1.0f : 0.0f;
        hpAmtT = f > 0.001f ? 1.0f : 0.0f;
        if (lpAmtT > 0.0f)
        {
            lpLogT = (float) std::log (filterLpHz (f));
            if (lpAmt <= 0.0f) lpLog = lpLogT; // fading in from nothing: start at the right cutoff
        }
        if (hpAmtT > 0.0f)
        {
            hpLogT = (float) std::log (filterHpHz (f));
            if (hpAmt <= 0.0f) hpLog = hpLogT;
        }
    }

    bool updateFilterSmoothing() noexcept
    {
        if (exactly (lpAmt, 0.0f) && exactly (hpAmt, 0.0f) && exactly (lpAmtT, 0.0f) && exactly (hpAmtT, 0.0f))
            return false;

        auto glide = [this] (float& v, float target, float snap)
        {
            v = target + filterSmooth * (v - target);
            if (std::abs (v - target) < snap) v = target;
        };
        glide (lpAmt, lpAmtT, 1.0e-4f);
        glide (hpAmt, hpAmtT, 1.0e-4f);
        glide (lpLog, lpLogT, 1.0e-4f);
        glide (hpLog, hpLogT, 1.0e-4f);

        if (exactly (lpAmt, 0.0f)) for (auto& f : lpState) f = {};
        if (exactly (hpAmt, 0.0f)) for (auto& f : hpState) f = {};
        return lpAmt > 0.0f || hpAmt > 0.0f;
    }

    float svf (SvfState& st, float x, float g, bool highPass) const noexcept
    {
        // Zavalishin TPT state-variable filter, Q ~ 0.8.
        constexpr float k = 1.25f;
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1, a3 = g * a2;
        const float v3 = x - st.ic2;
        const float v1 = a1 * st.ic1 + a2 * v3;
        const float v2 = st.ic2 + a2 * st.ic1 + a3 * v3;
        st.ic1 = 2.0f * v1 - st.ic1;
        st.ic2 = 2.0f * v2 - st.ic2;
        return highPass ? x - k * v1 - v2 : v2;
    }

    float applyFilter (int c, float x) noexcept
    {
        const float nyq = (float) (0.45 * sr);
        constexpr float pi = 3.14159265f;
        float y = x;
        if (lpAmt > 0.0f)
        {
            const float g = std::tan (pi * std::min (std::exp (lpLog), nyq) / (float) sr);
            y += lpAmt * (svf (lpState[(size_t) c], x, g, false) - x);
        }
        if (hpAmt > 0.0f)
        {
            const float g = std::tan (pi * std::min (std::exp (hpLog), nyq) / (float) sr);
            y += hpAmt * (svf (hpState[(size_t) c], x, g, true) - x);
        }
        return y;
    }

    //==========================================================================
    std::array<std::vector<float>, kMaxChannels> buffers;
    int channels = 2;
    double sr = 48000.0;
    int64_t capacity = 0, mask = 0;

    int64_t writeCount = 0; // absolute index of the next sample to record
    Head head, fadeHead;
    int fadeLen = 0, fadeRemaining = 0;
    bool fadeLinear = false;

    bool stuttering = false;
    StutterType type = StutterType::repeat;
    float p1 = 0.0f, p2 = 0.0f, level = 1.0f, pan = 0.0f;
    uint32_t seed = 0;
    int64_t sliceStart = 0;
    double sliceLen = 0.0, stepLen = 0.0, gridPhase = 0.0, minPeriod = 32.0;
    double regionStart = 0.0, oldestNeeded = 0.0;
    int fadeSamples = 0, slicesPerStep = 1, maxLookbackSlices = 0;
    int64_t elapsed = 0, repeatIndex = 0, nextWrap = 0;
    double nextWrapT = 0.0;

    float gateGain = 1.0f, gateUp = 1.0f, gateDown = 1.0f;

    float lpAmt = 0.0f, hpAmt = 0.0f, lpAmtT = 0.0f, hpAmtT = 0.0f;
    float lpLog = 0.0f, hpLog = 0.0f, lpLogT = 0.0f, hpLogT = 0.0f;
    float filterSmooth = 0.99f;

    std::array<float, 2> shape { 1.0f, 1.0f }, shapeTarget { 1.0f, 1.0f }, shapeStep { 0.0f, 0.0f };
    int shapeRamp = 0;
    float sendAmt = 0.0f, sendTarget = 0.0f, sendStep = 0.0f;
    int sendRamp = 0;
    float actAmt = 0.0f, actTarget = 0.0f, actStep = 0.0f;
    int actRamp = 0;
    float crushAmt = 0.0f, crushAmtT = 0.0f, crushStep = 1.0f;
    double crushBitsNow = 24.0, crushFactorNow = 1.0;
    Crusher crusher;
    int64_t repeatStartElapsed = 0;
    std::array<SvfState, kMaxChannels> lpState {}, hpState {};

    float mixTarget = 1.0f, mixCurrent = 1.0f, mixStep = 0.0f;
    int mixRampLeft = 0, mixSmoothingSamples = 960;
    bool mixInitialised = false;
};

} // namespace gn
