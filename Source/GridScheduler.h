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
// Swing pushes every second grid line late (MPC-style): with swing s (0..0.5
// of a step), the first step of each pair is longer and the second shorter.
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

    // Where step k starts, measured in steps (odd steps pushed late by the swing).
    static double stepStart (int64_t k, double swing) noexcept
    {
        return (double) k + ((k & 1) != 0 ? swing : 0.0);
    }

    // Finds every grid line inside [ppqStart, ppqStart + numSamples).
    // 'resynced' is set when the position jumped or playback just started.
    int process (double ppqStart, double samplesPerQuarter, double stepPpq, double swing,
                 int numSamples, GridEvent* out, bool& resynced) noexcept
    {
        resynced = false;
        swing = std::clamp (swing, 0.0, 0.5);
        const double samplesPerStep = samplesPerQuarter * stepPpq;
        const double s0 = ppqStart / stepPpq;                 // position, in steps
        constexpr double tolSamples = 1.0e-3;                 // lines within 1/1000 sample count as "now"
        const double tol = tolSamples / samplesPerStep;

        const bool jumped = std::abs (ppqStart - expectedPpq) * samplesPerQuarter > kJumpToleranceSamples;

        if (! synced || std::abs (stepPpq - lastStepPpq) > 1.0e-12 || jumped)
        {
            // The step we're inside counts as already started (no half-steps),
            // unless we're sitting exactly on its grid line.
            auto k0 = (int64_t) std::floor (s0 + tol);
            if (stepStart (k0, swing) > s0 + tol)
                --k0; // we're in the late-pushed gap of a swung pair
            lastStep = (std::abs (stepStart (k0, swing) - s0) <= tol) ? k0 - 1 : k0;
            synced = true;
            resynced = true;
        }

        lastStepPpq = stepPpq;
        expectedPpq = ppqStart + (double) numSamples / samplesPerQuarter;

        int count = 0;
        for (int64_t k = lastStep + 1; count < kMaxEvents; ++k)
        {
            const double offsetSamples = (stepStart (k, swing) - s0) * samplesPerStep;
            int n = (int) std::ceil (offsetSamples - tolSamples);
            if (n >= numSamples)
                break;
            if (n < 0)
                n = 0; // host nudged us slightly past the line (or the swing moved it): fire now

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
