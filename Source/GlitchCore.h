#pragma once
//==============================================================================
// Glitch Nasty - GlitchCore
//
// The whole audio path in one JUCE-free class: clock -> grid scheduler ->
// step pattern (with per-step probability) -> stutter engine.
// The plug-in wrapper and the offline tests both run exactly this code.
//==============================================================================

#include "GridScheduler.h"
#include "StutterEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace gn
{

constexpr int kMaxSteps = 16;

// Grid = how long each step is.
constexpr int kNumGrids = 6;
constexpr const char* kGridNames[kNumGrids] = { "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T" };
constexpr double kGridPpq[kNumGrids]        = { 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0 };
constexpr int kDefaultGrid = 2; // 1/16

// Repeat = length of the slice that loops inside a step. Longest first.
constexpr int kNumDivisions = 10;
constexpr const char* kDivisionNames[kNumDivisions] = {
    "1/4", "1/8D", "1/8", "1/16D", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64"
};
constexpr double kDivisionPpq[kNumDivisions] = {
    1.0, 0.75, 0.5, 0.375, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0, 0.0625
};

// What each step does when it fires. Short names fit the grid; long names go in menus.
constexpr const char* kTypeShortNames[kNumStutterTypes] = { "RPT", "REV", "GATE", "ROLL", "PIT+", "PIT-", "STOP", "FADE" };
constexpr const char* kTypeNames[kNumStutterTypes] = {
    "Repeat", "Reverse", "Gate", "Roll", "Pitch +12", "Pitch -12", "Tape Stop", "Fade"
};

struct StepSettings
{
    bool on = false;
    float probability = 1.0f; // 0..1
    int division = 7;         // index into kDivisionPpq (1/32)
    int type = 0;             // StutterType
};

struct CoreParams
{
    int syncMode = 0;          // 0 = Host (falls back to Internal when there's no host clock), 1 = Internal
    double internalBpm = 120.0;
    int gridIndex = kDefaultGrid;
    int numSteps = 16;
    float crossfadeMs = 3.0f;
    float mix = 1.0f;          // 0..1
    bool snapToBeat = false;   // one-shot from TAP: snap the internal clock to the nearest beat
    std::array<StepSettings, kMaxSteps> steps {};
};

struct HostTime
{
    bool hasPpq = false, hasBpm = false, playing = false;
    double ppq = 0.0, bpm = 120.0;
};

class GlitchCore
{
public:
    // Longest slice: a 1/4 note at 20 BPM is 3 s; keep a little headroom.
    static constexpr double kMaxSliceSeconds = 3.5;

    void prepare (double sampleRate, int numChannels)
    {
        sr = sampleRate;
        engine.prepare (numChannels, sampleRate, kMaxSliceSeconds);
        reset();
    }

    void reset()
    {
        engine.reset();
        scheduler.reset();
        internalPpq = 0.0;
        playingStep = -1;
        firingStep = -1;
    }

    void process (float* const* io, int numCh, int numSamples, const CoreParams& p, const HostTime& host) noexcept
    {
        engine.setMix (p.mix);

        // ---- Clock ---------------------------------------------------------
        hostClock = (p.syncMode == 0 && host.hasPpq && host.hasBpm);
        const double bpm = std::clamp (hostClock ? host.bpm : p.internalBpm, 20.0, 999.0);
        const bool running = hostClock ? host.playing : true;
        const double samplesPerQuarter = sr * 60.0 / bpm;
        effectiveBpm = bpm;

        double ppq = 0.0;
        if (hostClock)
        {
            ppq = host.ppq;
        }
        else
        {
            if (p.snapToBeat)
                internalPpq = std::round (internalPpq);
            ppq = internalPpq;
            internalPpq += (double) numSamples / samplesPerQuarter;
        }

        const double stepPpq = kGridPpq[std::clamp (p.gridIndex, 0, kNumGrids - 1)];
        const double stepSamples = samplesPerQuarter * stepPpq;
        const int fade = fadeSamples (p.crossfadeMs, stepSamples);

        // ---- Transport stopped: straight through ---------------------------
        if (! running)
        {
            engine.stopStutter (fade);
            scheduler.reset();
            playingStep = -1;
            firingStep = -1;
            engine.process (io, numCh, 0, numSamples);
            return;
        }

        // ---- Split the block at each grid line -----------------------------
        bool resynced = false;
        const int numEvents = scheduler.process (ppq, samplesPerQuarter, stepPpq, numSamples, events.data(), resynced);

        if (resynced)
        {
            engine.stopStutter (fade);
            firingStep = -1;
            playingStep = stepIndex (scheduler.currentStep(), p.numSteps);
        }

        int cursor = 0;
        for (int e = 0; e < numEvents; ++e)
        {
            const auto& ev = events[(size_t) e];
            engine.process (io, numCh, cursor, ev.offset - cursor);
            cursor = ev.offset;
            triggerStep (ev.step, ev.lateBy, p, samplesPerQuarter, stepSamples, fade);
        }
        engine.process (io, numCh, cursor, numSamples - cursor);
    }

    // For the UI.
    int getPlayingStep() const noexcept { return playingStep; }
    int getFiringStep() const noexcept  { return firingStep; }
    double getEffectiveBpm() const noexcept { return effectiveBpm; }
    bool isUsingHostClock() const noexcept { return hostClock; }

    // For tests: deterministic dice.
    void setRandomSeed (uint32_t seed) noexcept { rngState = seed != 0 ? seed : 0x9E3779B9u; }

private:
    static int stepIndex (int64_t absStep, int numSteps) noexcept
    {
        const int64_t n = std::clamp (numSteps, 1, kMaxSteps);
        return (int) (((absStep % n) + n) % n);
    }

    int fadeSamples (float ms, double stepSamples) const noexcept
    {
        const double f = std::min ((double) ms * 0.001 * sr, stepSamples * 0.5);
        return std::max (0, (int) std::lround (f));
    }

    void triggerStep (int64_t absStep, double lateBy, const CoreParams& p, double samplesPerQuarter,
                      double stepSamples, int fade) noexcept
    {
        const int idx = stepIndex (absStep, p.numSteps);
        playingStep = idx;

        const auto& s = p.steps[(size_t) idx];
        const bool fire = s.on && (s.probability >= 0.999f || nextRandom() < s.probability);

        if (fire)
        {
            StutterSettings st;
            st.type = (StutterType) std::clamp (s.type, 0, kNumStutterTypes - 1);
            st.sliceLen = kDivisionPpq[std::clamp (s.division, 0, kNumDivisions - 1)] * samplesPerQuarter;
            st.stepLen = stepSamples;
            st.lateBy = lateBy;
            st.minPeriod = samplesPerQuarter / 32.0; // Roll bottoms out at 1/128 notes
            st.fadeSamples = fade;
            engine.startStutter (st);
            firingStep = idx;
        }
        else
        {
            engine.stopStutter (fade);
            firingStep = -1;
        }
    }

    // xorshift32: tiny, allocation-free, and seedable (handy for a "seed" control later).
    float nextRandom() noexcept
    {
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        return (float) (rngState >> 8) * (1.0f / 16777216.0f);
    }

    StutterEngine engine;
    GridScheduler scheduler;
    std::array<GridEvent, GridScheduler::kMaxEvents> events {};

    double sr = 48000.0;
    double internalPpq = 0.0;
    double effectiveBpm = 120.0;
    bool hostClock = false;
    int playingStep = -1, firingStep = -1;
    uint32_t rngState = 0x12345678u;
};

} // namespace gn
