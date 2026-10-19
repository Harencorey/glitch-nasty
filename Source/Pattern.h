#pragma once
//==============================================================================
// Glitch Nasty - pattern data shared by the engine, the plug-in and the UI.
//
// A pattern is 16 steps. Each step says whether it glitches (on), how (type),
// how often (chance), how fast (repeat length), how long (length in steps -
// a "tie"), and how it sounds (level, pan, filter, plus two controls whose
// meaning depends on the type). There are 8 patterns, A-H.
//
// Plain C++ (no JUCE) so the offline tests use exactly the same definitions.
//==============================================================================

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace gn
{

constexpr int kMaxSteps = 16;
constexpr int kNumPatterns = 8;
constexpr const char* kPatternNames[kNumPatterns] = { "A", "B", "C", "D", "E", "F", "G", "H" };

// Grid = how long each step is.
constexpr int kNumGrids = 6;
constexpr const char* kGridNames[kNumGrids] = { "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T" };
constexpr double kGridPpq[kNumGrids]        = { 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0 };
constexpr int kDefaultGrid = 2; // 1/16

// Repeat length = the slice that loops inside a step. Longest first.
constexpr int kNumDivisions = 10;
constexpr const char* kDivisionNames[kNumDivisions] = {
    "1/4", "1/8D", "1/8", "1/16D", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64"
};
constexpr double kDivisionPpq[kNumDivisions] = {
    1.0, 0.75, 0.5, 0.375, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0, 0.0625
};
constexpr int kDiv8 = 2, kDiv16D = 3, kDiv16 = 5, kDiv16T = 6, kDiv32 = 7, kDiv32T = 8, kDiv64 = 9;

//==============================================================================
enum class StutterType : int
{
    repeat = 0,
    reverse,
    gate,
    roll,
    pitch,
    tapeStop,
    fade,
    shuffle,
    freeze,
    scratch,
    pingPong
};
constexpr int kNumStutterTypes = 11;

constexpr const char* kTypeNames[kNumStutterTypes] = {
    "Repeat", "Reverse", "Gate", "Roll", "Pitch", "Tape Stop", "Fade", "Shuffle", "Freeze", "Scratch", "Ping-Pong"
};
constexpr const char* kTypeShortNames[kNumStutterTypes] = {
    "RPT", "REV", "GATE", "ROLL", "PITCH", "STOP", "FADE", "SHUF", "FRZ", "SCR", "PING"
};

// Each type has two controls of its own. numChoices > 0 = a switch with named
// positions; 0 = a continuous 0..1 knob. The stored value is always 0..1.
struct TypeControl
{
    const char* name;
    int numChoices;
    const char* choices[3];
};

struct TypeInfo
{
    TypeControl c1, c2;
    float p1Default, p2Default;
    const char* help;
};

constexpr TypeInfo kTypeInfo[kNumStutterTypes] = {
    { { "Offset", 0, {} }, { "Variation", 0, {} }, 0.0f, 0.0f,
      "Loops a slice of the step until the step ends. Offset: which slice of the step gets looped. "
      "Variation: each repeat starts a little earlier, at random (humanized)." },
    { { "Mode", 2, { "Reverse", "Ping-Pong", nullptr } }, { "Offset", 0, {} }, 0.0f, 0.0f,
      "Plays the slice backwards. Ping-Pong goes back to the start of the slice, then forward again, inside every repeat. Offset: which slice gets used." },
    { { "Duty", 0, {} }, { "Shape", 3, { "Hard", "Soft", "Pump" } }, 0.5f, 0.0f,
      "Chops the live signal on and off. Duty: how much of each cycle is open. "
      "Shape: hard edges, soft edges, or Pump (ducks then swells back, like sidechain)." },
    { { "Direction", 2, { "Speed Up", "Slow Down", nullptr } }, { "Curve", 0, {} }, 0.0f, 1.0f,
      "Each repeat shorter than the last (Speed Up: the build-up roll) or longer (Slow Down). "
      "Curve: how quickly it changes." },
    { { "Pitch", 0, {} }, { "Glide", 0, {} }, 0.75f, 0.5f,
      "Repeats played faster or slower, like a sampler: -24 to +24 semitones. "
      "Glide bends the pitch across the step, for risers and droppers." },
    { { "Direction", 2, { "Stop", "Start", nullptr } }, { "Curve", 0, {} }, 0.0f, 0.0f,
      "Stop: slows to a halt over the repeat length. Start: spins up from nothing. Curve: linear to exponential." },
    { { "Decay", 0, {} }, { "Direction", 2, { "Fade Out", "Fade In", nullptr } }, 3.0f / 7.0f, 0.0f,
      "Each repeat quieter than the last (Fade Out: a dying echo) or louder (Fade In: a swell)." },
    { { "Chaos", 0, {} }, { "Reverse", 0, {} }, 0.7f, 0.0f,
      "Chops the step into slices and plays them in a scrambled order, using audio that has already "
      "happened (so still zero latency). Chaos: how many slices get swapped. Reverse: chance a swapped slice plays backwards." },
    { { "Grain", 0, {} }, { "Texture", 0, {} }, 0.4f, 0.2f,
      "Holds a tiny grain of the sound as a sustained drone. Grain: 10-200 ms. Texture: how much the grain wanders." },
    { { "Depth", 0, {} }, { "Speed", 3, { "1/2x", "1x", "2x" } }, 0.5f, 0.5f,
      "Vinyl scratch: the audio is dragged back and forth. Depth: how far. Speed: once per repeat length, or half / double." },
    { { "Width", 0, {} }, { "Decay", 0, {} }, 1.0f, 0.0f,
      "Repeats bounce between left and right. Width: how far. Decay: each repeat quieter." },
};

//==============================================================================
// How the 0..1 controls map to real values. The engine and the UI's readouts
// both use these, so what you see is what you hear.
inline float clamp01 (float v) noexcept { return std::clamp (v, 0.0f, 1.0f); }

// a -> b by t, exactly b at t = 1 (a + t * (b - a) isn't, in floating point).
inline float blend (float a, float b, float t) noexcept { return t >= 1.0f ? b : a + t * (b - a); }

// Exact comparison on purpose (state flags like "fully off"), without -Wfloat-equal noise.
template <typename F> inline bool exactly (F a, F b) noexcept { return ! (a < b) && ! (a > b); }

inline int choiceIndex (float p, int numChoices) noexcept
{
    if (numChoices <= 1)
        return 0;
    return std::clamp ((int) std::lround (clamp01 (p) * (float) (numChoices - 1)), 0, numChoices - 1);
}

inline double gateDuty (float p) noexcept     { return 0.05 + 0.9 * (double) clamp01 (p); }
inline double rollFactor (float p) noexcept   { return 0.5 + 0.4 * (1.0 - (double) clamp01 (p)); }
inline double pitchSemis (float p) noexcept   { return ((double) clamp01 (p) - 0.5) * 48.0; }
inline double tapeCurve (float p) noexcept    { return 1.0 + 2.0 * (double) clamp01 (p); }
inline double fadeFactor (float p) noexcept   { return 0.9 - 0.7 * (double) clamp01 (p); }
inline double freezeGrainMs (float p) noexcept { return 10.0 * std::pow (20.0, (double) clamp01 (p)); }
inline double scratchDepth (float p) noexcept { return 0.05 + 0.95 * (double) clamp01 (p); }
inline double scratchSpeed (float p) noexcept { constexpr double s[3] = { 0.5, 1.0, 2.0 }; return s[choiceIndex (p, 3)]; }
inline double pingDecay (float p) noexcept    { return 1.0 - 0.6 * (double) clamp01 (p); }

// Filter knob: -1..0 = low-pass 20 kHz -> 200 Hz, 0..+1 = high-pass 20 Hz -> 4 kHz.
inline double filterLpHz (float f) noexcept { return 20000.0 * std::pow (0.01, (double) std::clamp (-f, 0.0f, 1.0f)); }
inline double filterHpHz (float f) noexcept { return 20.0 * std::pow (200.0, (double) std::clamp (f, 0.0f, 1.0f)); }

// Crusher amounts (0 = off): bits 16 -> 1 spread evenly over the knob (halfway ~ 6 bits),
// sample rate divided by 1 -> 64.
inline double crushBits (float amt) noexcept       { return 1.0 + 15.0 * std::pow (1.0 - (double) clamp01 (amt), 1.6); }
inline double crushRateFactor (float amt) noexcept { return std::pow (64.0, (double) clamp01 (amt)); }

constexpr float kLevelMinDb = -30.0f, kLevelMaxDb = 6.0f;
inline float levelGain (float db) noexcept
{
    return db <= kLevelMinDb + 0.01f ? 0.0f : std::pow (10.0f, std::min (db, kLevelMaxDb) / 20.0f);
}

// How many repeat-length slices fit in a step, and which one an Offset picks.
inline int slicesIn (double stepLen, double sliceLen) noexcept
{
    return std::max (1, (int) std::floor (stepLen / sliceLen + 1.0e-6));
}
inline int sliceOffset (float p, int slicesPerStep) noexcept
{
    return slicesPerStep <= 1 ? 0 : std::clamp ((int) std::lround (clamp01 (p) * (float) (slicesPerStep - 1)), 0, slicesPerStep - 1);
}

//==============================================================================
struct StepData
{
    bool on = false;
    float chance = 1.0f;   // 0..1
    int division = kDiv32; // repeat length (index into kDivisionPpq)
    int type = 0;          // StutterType
    int length = 1;        // steps this glitch lasts (ties over the following steps)
    float levelDb = 0.0f;  // kLevelMinDb (off) .. kLevelMaxDb
    float pan = 0.0f;      // -1 (L) .. +1 (R)
    float filter = 0.0f;   // -1 (LP) .. 0 (off) .. +1 (HP)
    float p1 = 0.0f;       // type control 1 (0..1)
    float p2 = 0.0f;       // type control 2 (0..1)
    float bits = 0.0f;     // crusher bit reduction (0 = off .. 1 = 1 bit)
    float rate = 0.0f;     // crusher sample-rate reduction (0 = off .. 1 = /64)
    float send = 1.0f;     // how much of this glitch goes to the delay/reverb (0..1)
};

using Pattern = std::array<StepData, kMaxSteps>;

struct PatternBank
{
    std::array<Pattern, kNumPatterns> patterns {};
    std::array<StepData, kNumStutterTypes> live {}; // the live pads' own settings, one per type
};

// Changing the type resets its two controls to that type's defaults.
inline void setType (StepData& s, int type) noexcept
{
    s.type = std::clamp (type, 0, kNumStutterTypes - 1);
    s.p1 = kTypeInfo[s.type].p1Default;
    s.p2 = kTypeInfo[s.type].p2Default;
}

inline StepData makeStep (StutterType type, int division, float chance = 1.0f, int length = 1)
{
    StepData s;
    s.on = true;
    s.division = division;
    s.chance = chance;
    s.length = length;
    setType (s, (int) type);
    return s;
}

// Factory patterns: A shows the basics, B-G show off the types, H is empty.
inline PatternBank makeDefaultBank()
{
    using T = StutterType;
    PatternBank b;

    auto& A = b.patterns[0]; // basics
    A[3]  = makeStep (T::repeat, kDiv32);
    A[6]  = makeStep (T::reverse, kDiv16T, 0.5f);
    A[6].pan = -0.4f;
    A[10] = makeStep (T::gate, kDiv32, 0.6f);
    A[11] = makeStep (T::pitch, kDiv32);
    A[14] = makeStep (T::roll, kDiv32, 0.75f);
    A[15] = makeStep (T::tapeStop, kDiv16);

    auto& B = b.patterns[1]; // build-up
    B[0]  = makeStep (T::repeat, kDiv16);
    B[4]  = makeStep (T::shuffle, kDiv16, 1.0f, 4);
    B[8]  = makeStep (T::pingPong, kDiv16D, 1.0f, 2);
    B[12] = makeStep (T::roll, kDiv16, 1.0f, 4);
    B[12].filter = 0.35f;

    auto& C = b.patterns[2]; // trance gate
    for (int i = 0; i < kMaxSteps; ++i)
    {
        C[(size_t) i] = makeStep (T::gate, kDiv32);
        C[(size_t) i].p1 = (i % 4 == 3) ? 0.25f : 0.5f;
        C[(size_t) i].p2 = (i % 8 >= 4) ? 0.5f : 0.0f;
    }
    C[7].p2 = C[15].p2 = 1.0f; // pump

    auto& D = b.patterns[3]; // breaks
    D[0]  = makeStep (T::shuffle, kDiv16, 1.0f, 4);
    D[4]  = makeStep (T::repeat, kDiv32, 0.6f);
    D[6]  = makeStep (T::reverse, kDiv16);
    D[8]  = makeStep (T::shuffle, kDiv16, 1.0f, 4);
    D[8].p2 = 0.4f;
    D[13] = makeStep (T::fade, kDiv64, 1.0f, 3);

    auto& E = b.patterns[4]; // pitch riser / dropper
    E[0] = makeStep (T::pitch, kDiv16, 1.0f, 8);
    E[0].p1 = 0.5f;  // 0 st
    E[0].p2 = 1.0f;  // glide +24
    E[4] = makeStep (T::roll, kDiv32);      // a layer: rolls the rising pitch
    E[8] = makeStep (T::pitch, kDiv32, 1.0f, 8);
    E[8].p2 = 0.0f;  // glide -24
    E[12] = makeStep (T::reverse, kDiv64);  // a layer: reverses the falling pitch

    auto& F = b.patterns[5]; // textures
    F[0]  = makeStep (T::freeze, kDiv16, 1.0f, 4);
    F[6]  = makeStep (T::scratch, kDiv16, 1.0f, 2);
    F[10] = makeStep (T::freeze, kDiv16, 1.0f, 2);
    F[10].p1 = 0.15f;
    F[12] = makeStep (T::tapeStop, kDiv8, 1.0f, 4);

    auto& G = b.patterns[6]; // ping-pong echoes
    for (int i : { 2, 6, 10, 14 })
    {
        G[(size_t) i] = makeStep (T::pingPong, kDiv16T);
        G[(size_t) i].p2 = 0.4f;
    }
    G[6].filter = 0.5f;
    G[14].filter = -0.6f;

    for (int t = 0; t < kNumStutterTypes; ++t)
        b.live[(size_t) t] = makeStep ((T) t, kDiv16);

    return b; // H stays empty
}

} // namespace gn
