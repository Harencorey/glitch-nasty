#pragma once
//==============================================================================
// Glitch Nasty - StutterEngine
//
// Think of this as a tape loop that is ALWAYS recording, with a playback head
// that normally sits right on top of the record head (so you hear the input
// with zero delay). When a stutter starts, we remember where the record head
// was ("sliceStart"). For the first pass the playback head just rides along
// with the record head, so you still hear live audio - that's why there's no
// latency. When the slice length is reached, the playback head jumps back to
// sliceStart and replays the same chunk, over and over, until the step ends.
//
// Every jump (loop point, start of a new stutter, return to live) is covered
// by a short equal-power crossfade: the old head keeps playing and fades out
// while the new head fades in. That's the same idea as a tape editor's angled
// splice instead of a straight cut.
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

class StutterEngine
{
public:
    static constexpr int kMaxChannels = 2;

    // Call from prepareToPlay (allocates).
    void prepare (int numChannels, double sampleRate, double maxSliceSeconds)
    {
        channels = std::clamp (numChannels, 1, kMaxChannels);
        sr = sampleRate;

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
        head = 0.0;
        fadeHead = 0.0;
        fadeLen = 0;
        fadeRemaining = 0;
        stuttering = false;
        sliceStart = 0;
        sliceLen = 0.0;
        elapsed = 0;
        wrapIndex = 1;
        nextWrap = 0;
        loopFade = 0;
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
    // sliceLenSamples may be fractional. lateBy (0..1) says how far the true grid
    // line was before this sample, so the loop points land on the real grid
    // instead of drifting by the rounding of the step start.
    // stepLenSamples: how long this step lasts. A loop point that would land on
    // (or within half a sample of) the step's end is skipped, because the next
    // step takes over there anyway; otherwise you'd get a 1-sample blip.
    void startStutter (double sliceLenSamples, int fadeSamples, double lateBy, double stepLenSamples)
    {
        sliceLen = std::clamp (sliceLenSamples, 16.0, maxSliceSamples());
        stepLen = stepLenSamples;
        loopFade = std::min (std::max (fadeSamples, 0), (int) (sliceLen * 0.5));

        if (stuttering)
            beginJump ((double) writeCount, fadeSamples); // leave the old slice, start from "now"
        else
            head = (double) writeCount;                   // already live: no jump needed

        stuttering = true;
        sliceStart = writeCount;
        elapsed = 0;
        wrapIndex = 1;
        gridPhase = lateBy;
        scheduleWrap();
    }

    // Return to the live input at the next sample (crossfaded).
    void stopStutter (int fadeSamples)
    {
        if (! stuttering)
            return;

        stuttering = false;
        beginJump ((double) writeCount, fadeSamples);
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

            // 2) Fast path: not glitching and no fade tail -> output == input, untouched.
            if (! stuttering && fadeRemaining == 0)
            {
                ++writeCount;
                head = (double) writeCount;
                continue;
            }

            // 3) Loop point reached? Jump back to the start of the slice.
            if (stuttering && elapsed >= nextWrap)
            {
                beginJump ((double) sliceStart, loopFade);
                ++wrapIndex;
                scheduleWrap();
            }

            // 4) Play back (with crossfade if one is running).
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
                float wet = read (c, head);
                if (fading)
                    wet = wet * gIn + read (c, fadeHead) * gOut;

                io[c][i] = dry + mixCurrent * (wet - dry);
            }

            head += 1.0;
            if (fading)
            {
                fadeHead += 1.0;
                --fadeRemaining;
            }

            ++elapsed;
            ++writeCount;
        }
    }

private:
    static constexpr int64_t kSafetySamples = 4096;

    void scheduleWrap() noexcept
    {
        const double t = (double) wrapIndex * sliceLen; // time of the next loop point, from the true grid line
        nextWrap = (t > stepLen - 0.5) ? std::numeric_limits<int64_t>::max()
                                       : (int64_t) std::llround (t - gridPhase);
    }

    void beginJump (double newHead, int fadeSamples) noexcept
    {
        if (fadeSamples <= 0)
        {
            head = newHead;
            fadeRemaining = 0;
            return;
        }

        // If a previous crossfade is still under way, fade out whichever head
        // is currently louder, so a jump-on-a-jump stays as smooth as possible.
        double outgoing = head;
        if (fadeRemaining > 0 && (fadeLen - fadeRemaining) * 2 < fadeLen)
            outgoing = fadeHead;

        fadeHead = outgoing;
        head = newHead;
        fadeLen = fadeSamples;
        fadeRemaining = fadeSamples;
    }

    float read (int c, double pos) const noexcept
    {
        // Linear interpolation (exact when pos is a whole sample, which it is
        // in Phase 1; varispeed in Phase 3 will use the fractional part).
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
    double sr = 48000.0;
    int64_t capacity = 0, mask = 0;

    int64_t writeCount = 0;   // absolute index of the next sample to record
    double head = 0.0;        // absolute playback position
    double fadeHead = 0.0;    // outgoing head during a crossfade
    int fadeLen = 0, fadeRemaining = 0;

    bool stuttering = false;
    int64_t sliceStart = 0;
    double sliceLen = 0.0, gridPhase = 0.0, stepLen = 0.0;
    int64_t elapsed = 0, wrapIndex = 1, nextWrap = 0;
    int loopFade = 0;

    float mixTarget = 1.0f, mixCurrent = 1.0f, mixStep = 0.0f;
    int mixRampLeft = 0, mixSmoothingSamples = 960;
    bool mixInitialised = false;
};

} // namespace gn
