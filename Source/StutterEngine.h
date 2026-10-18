#pragma once
//==============================================================================
// Glitch Nasty - StutterEngine
//
// Think of this as a tape loop that is ALWAYS recording, with a playback head
// that normally sits right on top of the record head (so you hear the input
// with zero delay). When a stutter starts, we remember where the record head
// was ("sliceStart"). For the first pass the playback head just rides along
// with the record head, so you still hear live audio - that's why there's no
// latency. When the slice length is reached, the playback head jumps back and
// replays that chunk until the step ends.
//
// The stutter TYPE decides what each replay does:
//   Repeat     same slice, same speed
//   Reverse    slice played backwards
//   Gate       no looping: the live signal is chopped on/off in time
//   Roll       each repeat half as long as the last (build-up roll)
//   Pitch +12  repeats at double speed (whole slice, twice as often)
//   Pitch -12  repeats at half speed (first half of the slice, an octave down)
//   Tape Stop  no looping: playback slows to a halt over the repeat length
//   Fade       each repeat quieter than the last (dying echo)
//
// The playback head has a speed ("rate") and a level ("gain"). Every jump is
// covered by a short equal-power crossfade: the old head keeps playing and
// fades out while the new head fades in (an angled tape splice, not a cut).
//
// Real-time safety: all memory is allocated in prepare(). process() never
// allocates, locks or does I/O. No JUCE dependency, so the offline tests can
// compile it on their own.
//==============================================================================

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace gn
{

enum class StutterType : int
{
    repeat = 0,
    reverse,
    gate,
    roll,
    pitchUp,
    pitchDown,
    tapeStop,
    fade
};
constexpr int kNumStutterTypes = 8;

struct StutterSettings
{
    StutterType type = StutterType::repeat;
    double sliceLen = 0.0;   // repeat length, in samples (may be fractional)
    double stepLen = 0.0;    // how long this step lasts, in samples
    double lateBy = 0.0;     // the true grid line was this far (0..1 samples) before this sample
    double minPeriod = 32.0; // shortest repeat a Roll may reach, in samples
    int fadeSamples = 0;     // crossfade length at every jump
};

class StutterEngine
{
public:
    static constexpr int kMaxChannels = 2;
    static constexpr float kFadeDecay = 0.6f;  // Fade type: each repeat -4.4 dB
    static constexpr double kGateDuty = 0.5;   // Gate type: open for the first half of each repeat

    // Call from prepareToPlay (allocates).
    void prepare (int numChannels, double sampleRate, double maxSliceSeconds)
    {
        channels = std::clamp (numChannels, 1, kMaxChannels);

        // Ring buffer sized to a power of two so wrapping is a cheap bit-mask.
        const auto needed = (int64_t) std::ceil (sampleRate * maxSliceSeconds) + kSafetySamples;
        int64_t cap = 1;
        while (cap < needed)
            cap <<= 1;

        capacity = cap;
        mask = cap - 1;

        for (auto& b : buffers)
            b.assign ((size_t) capacity, 0.0f);

        mixSmoothingSamples = std::max (1, (int) (0.02 * sampleRate)); // 20 ms
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
        stuttering = false;
        type = StutterType::repeat;
        sliceStart = 0;
        sliceLen = stepLen = gridPhase = period = 0.0;
        elapsed = 0;
        repeatIndex = 0;
        nextWrapT = 0.0;
        nextWrap = std::numeric_limits<int64_t>::max();
        gateGain = 1.0f;
        gateStep = 1.0f;
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

    // Start a new stutter at the next sample process() handles.
    void startStutter (const StutterSettings& s)
    {
        type = s.type;
        sliceLen = std::clamp (s.sliceLen, 16.0, maxSliceSamples());
        stepLen = s.stepLen;
        gridPhase = s.lateBy;
        fadeSamples = std::max (s.fadeSamples, 0);
        minPeriod = std::max (16.0, s.minPeriod);

        // The first pass is always the live signal (that's the zero latency).
        if (stuttering)
            beginJump ((double) writeCount, 1.0, 1.0f, fadeSamples); // leave the old slice
        else
            head = { (double) writeCount, 1.0, 1.0f };

        stuttering = true;
        sliceStart = writeCount;
        elapsed = 0;
        repeatIndex = 0;
        period = sliceLen;

        // Gate and Tape Stop never loop; the others loop at the end of the first pass.
        nextWrapT = sliceLen;
        scheduleWrap (type != StutterType::gate && type != StutterType::tapeStop);

        // Gate edges get the crossfade length as their ramp (capped so short gates still open).
        const double ramp = std::min ((double) fadeSamples, sliceLen * kGateDuty * 0.5);
        gateStep = ramp >= 1.0 ? (float) (1.0 / ramp) : 1.0f;
    }

    // Return to the live input at the next sample (crossfaded).
    void stopStutter (int fadeSamplesForReturn)
    {
        if (! stuttering)
            return;

        stuttering = false;
        beginJump ((double) writeCount, 1.0, 1.0f, fadeSamplesForReturn);
        const double ramp = (double) std::max (fadeSamplesForReturn, 0);
        gateStep = ramp >= 1.0 ? (float) (1.0 / ramp) : 1.0f;
    }

    bool isStuttering() const noexcept { return stuttering; }
    int64_t getCapacity() const noexcept { return capacity; }
    double maxSliceSamples() const noexcept { return (double) (capacity - kSafetySamples); }

    // Processes io[ch][start .. start+num) in place.
    void process (float* const* io, int numCh, int start, int num) noexcept
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

            // 2) Gate level (moves toward open/closed with a short ramp).
            float gateTarget = 1.0f;
            if (stuttering && type == StutterType::gate)
            {
                const double t = (double) elapsed + gridPhase;
                gateTarget = std::fmod (t, sliceLen) < sliceLen * kGateDuty ? 1.0f : 0.0f;
            }
            if (gateGain < gateTarget)      gateGain = std::min (gateTarget, gateGain + gateStep);
            else if (gateGain > gateTarget) gateGain = std::max (gateTarget, gateGain - gateStep);

            // 3) Fast path: not glitching, no fade tail, gate open -> output == input, untouched.
            if (! stuttering && fadeRemaining == 0 && gateGain >= 1.0f)
            {
                ++writeCount;
                head = { (double) writeCount, 1.0, 1.0f };
                continue;
            }

            // 4) Loop point reached? Start the next repeat.
            if (stuttering && elapsed >= nextWrap)
                startNextRepeat();

            // 5) Tape Stop: the head decelerates to a standstill over the repeat length.
            if (stuttering && type == StutterType::tapeStop)
            {
                const double rate = std::max (0.0, 1.0 - (double) elapsed / sliceLen);
                head.rate = rate;
                head.gain = (float) std::min (1.0, rate * 4.0); // level drops in the last quarter, like a real deck
            }

            // 6) Play back (with crossfade if one is running).
            float gIn = 1.0f, gOut = 0.0f;
            const bool fading = fadeRemaining > 0;
            if (fading)
            {
                const double t = ((double) (fadeLen - fadeRemaining) + 0.5) / (double) fadeLen;
                gIn  = (float) std::sin (t * halfPi);
                gOut = (float) std::cos (t * halfPi);
            }

            for (int c = 0; c < numCh; ++c)
            {
                const float dry = io[c][i];
                float wet = read (c, head.pos) * head.gain;
                if (fading)
                    wet = wet * gIn + read (c, fadeHead.pos) * fadeHead.gain * gOut;
                wet *= gateGain;

                io[c][i] = dry + mixCurrent * (wet - dry);
            }

            head.pos += head.rate;
            if (fading)
            {
                fadeHead.pos += fadeHead.rate;
                --fadeRemaining;
            }

            ++elapsed;
            ++writeCount;
            if (! stuttering && fadeRemaining == 0)
                head = { (double) writeCount, 1.0, 1.0f }; // back on the record head, exactly
        }
    }

private:
    static constexpr int64_t kSafetySamples = 4096;

    struct Head
    {
        double pos = 0.0;  // absolute playback position (in samples recorded so far)
        double rate = 1.0; // 1 = normal speed, 2 = octave up, -1 = backwards
        float gain = 1.0f;
    };

    void startNextRepeat() noexcept
    {
        ++repeatIndex;
        double newPos = (double) sliceStart, rate = 1.0;
        float gain = 1.0f;
        period = sliceLen;

        switch (type)
        {
            case StutterType::repeat:
                break;
            case StutterType::reverse:
                newPos = (double) sliceStart + sliceLen - 1.0; // start at the end, play backwards
                rate = -1.0;
                break;
            case StutterType::roll:
                period = std::max (minPeriod, sliceLen * std::pow (0.5, (double) repeatIndex));
                break;
            case StutterType::pitchUp:
                period = sliceLen * 0.5; // the whole slice at double speed, twice as often
                rate = 2.0;
                break;
            case StutterType::pitchDown:
                rate = 0.5;              // the first half of the slice, an octave down
                break;
            case StutterType::fade:
                gain = std::pow (kFadeDecay, (float) repeatIndex);
                break;
            case StutterType::gate:
            case StutterType::tapeStop:
                break; // never loop
        }

        const int loopFade = std::min (fadeSamples, (int) (period * 0.5));
        beginJump (newPos, rate, gain, loopFade);

        nextWrapT += period;
        scheduleWrap (true);
    }

    // A loop point that would land on (or within half a sample of) the step's end
    // is skipped: the next step takes over there anyway, and otherwise you'd get a
    // 1-sample blip.
    void scheduleWrap (bool loops) noexcept
    {
        nextWrap = (! loops || nextWrapT > stepLen - 0.5)
                       ? std::numeric_limits<int64_t>::max()
                       : (int64_t) std::llround (nextWrapT - gridPhase);
    }

    void beginJump (double newPos, double newRate, float newGain, int fadeLenSamples) noexcept
    {
        const Head incoming { newPos, newRate, newGain };

        if (fadeLenSamples <= 0)
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

        fadeHead = outgoing;
        head = incoming;
        fadeLen = fadeLenSamples;
        fadeRemaining = fadeLenSamples;
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

    std::array<std::vector<float>, kMaxChannels> buffers;
    int channels = 2;
    int64_t capacity = 0, mask = 0;

    int64_t writeCount = 0; // absolute index of the next sample to record
    Head head, fadeHead;
    int fadeLen = 0, fadeRemaining = 0;

    bool stuttering = false;
    StutterType type = StutterType::repeat;
    int64_t sliceStart = 0;
    double sliceLen = 0.0, stepLen = 0.0, gridPhase = 0.0, period = 0.0, minPeriod = 32.0;
    int fadeSamples = 0;
    int64_t elapsed = 0, repeatIndex = 0, nextWrap = 0;
    double nextWrapT = 0.0;

    float gateGain = 1.0f, gateStep = 1.0f;

    float mixTarget = 1.0f, mixCurrent = 1.0f, mixStep = 0.0f;
    int mixRampLeft = 0, mixSmoothingSamples = 960;
    bool mixInitialised = false;
};

} // namespace gn
