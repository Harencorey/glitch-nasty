#pragma once
//==============================================================================
// Glitch Nasty - GridScheduler
//
// Turns "where is the song right now" (PPQ = position in quarter notes) into
// "on which exact sample inside this audio block does the next grid line land".
// Grid lines almost never sit on a block boundary, so we split the block at
// those samples - like a drum machine firing its trigger on the exact tick
// rather than the next time it happens to check.
//
// It also notices when the position jumps (DAW loop/cycle, locating, starting
// playback). Then it resyncs and waits for the next grid line instead of
// firing a half-step.
//==============================================================================

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gn
{

struct GridEvent
{
    int offset;    // sample index inside the block
    int64_t step;  // absolute step number since PPQ 0 (can be negative in pre-roll)
    double lateBy; // how far (in samples, usually 0..1) the true grid line is before 'offset'
};

class GridScheduler
{
public:
    static constexpr int kMaxEvents = 256;

    void reset() noexcept { synced = false; }

    // Finds every grid line inside [ppqStart, ppqStart + numSamples).
    // 'resynced' is set when the position jumped or playback just started.
    int process (double ppqStart, double samplesPerQuarter, double stepPpq,
                 int numSamples, GridEvent* out, bool& resynced) noexcept
    {
        resynced = false;
        const double samplesPerStep = samplesPerQuarter * stepPpq;
        const double s0 = ppqStart / stepPpq; // position, in steps
        constexpr double tolSamples = 1.0e-3; // treat lines within 1/1000 sample as "now"

        const bool jumped = std::abs (ppqStart - expectedPpq) * samplesPerQuarter > kJumpToleranceSamples;

        if (! synced || std::abs (stepPpq - lastStepPpq) > 1.0e-12 || jumped)
        {
            // The step we're inside counts as already started (no half-steps),
            // unless we're sitting exactly on a grid line.
            lastStep = (int64_t) std::ceil (s0 - tolSamples / samplesPerStep) - 1;
            synced = true;
            resynced = true;
        }

        lastStepPpq = stepPpq;
        expectedPpq = ppqStart + (double) numSamples / samplesPerQuarter;

        int count = 0;
        for (int64_t k = lastStep + 1; count < kMaxEvents; ++k)
        {
            const double offsetSamples = ((double) k - s0) * samplesPerStep;
            int n = (int) std::ceil (offsetSamples - tolSamples);
            if (n >= numSamples)
                break;
            if (n < 0)
                n = 0; // host nudged us slightly past the line: fire immediately

            out[count++] = { n, k, std::clamp ((double) n - offsetSamples, 0.0, 1.0) };
            lastStep = k;
        }

        return count;
    }

    // The step currently playing (absolute).
    int64_t currentStep() const noexcept { return lastStep; }

private:
    // Hosts round their PPQ a little; only call it a jump beyond 8 samples.
    static constexpr double kJumpToleranceSamples = 8.0;

    bool synced = false;
    int64_t lastStep = 0;
    double expectedPpq = 0.0;
    double lastStepPpq = 0.0;
};

} // namespace gn
