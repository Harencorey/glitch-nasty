#pragma once
//==============================================================================
// Glitch Nasty - how control values read on screen. Shared by the step editor
// knobs and the parameter-lock bars on the grid, so they always agree.
//==============================================================================

#include "Pattern.h"

#include <juce_core/juce_core.h>
#include <cmath>

namespace gn::fmt
{
inline juce::String signedValue (double v, int decimals, const juce::String& unit)
{
    const auto s = juce::String (std::abs (v), decimals);
    return (v > 0.0005 ? "+" : (v < -0.0005 ? "-" : "")) + s + unit;
}

inline juce::String percent (double v) { return juce::String (juce::roundToInt (v * 100.0)) + " %"; }

inline juce::String hz (double f)
{
    return f >= 1000.0 ? juce::String (f / 1000.0, f >= 10000.0 ? 0 : 1) + "k" : juce::String (juce::roundToInt (f));
}

inline juce::String level (double db) { return db <= kLevelMinDb + 0.05 ? juce::String ("Off") : signedValue (db, 1, " dB"); }

inline juce::String pan (double v)
{
    const int pc = juce::roundToInt (std::abs (v) * 100.0);
    return pc == 0 ? juce::String ("C") : (v < 0 ? "L " : "R ") + juce::String (pc);
}

inline juce::String filter (double v)
{
    if (v < -0.001) return "LP " + hz (filterLpHz ((float) v));
    if (v > 0.001)  return "HP " + hz (filterHpHz ((float) v));
    return juce::String ("Off");
}

inline juce::String bits (double v) { return v < 0.001 ? juce::String ("Off") : juce::String (crushBits ((float) v), 1) + " bit"; }
inline juce::String rate (double v) { return v < 0.001 ? juce::String ("Off") : "/" + juce::String (crushRateFactor ((float) v), 1); }

// A type's control 1 (first = true) or 2 at value p (0..1).
// 'slices' = how many repeat slices the step holds (for Offset readouts), or 0 if unknown (live pads).
inline juce::String typeControl (int type, bool first, float p, int slices)
{
    using T = StutterType;
    type = std::clamp (type, 0, kNumStutterTypes - 1);
    const auto& tc = first ? kTypeInfo[(size_t) type].c1 : kTypeInfo[(size_t) type].c2;
    if (tc.numChoices > 0)
        return tc.choices[choiceIndex (p, tc.numChoices)];

    auto offsetText = [p, slices]
    {
        if (slices <= 0) return "Slice " + percent (p);
        return "Slice " + juce::String (sliceOffset (p, slices) + 1) + "/" + juce::String (slices);
    };

    switch ((T) type)
    {
        case T::repeat:   return first ? offsetText() : percent (p);
        case T::reverse:  return offsetText();
        case T::gate:     return percent (gateDuty (p));
        case T::roll:     return "x" + juce::String (rollFactor (p), 2);
        case T::pitch:    return signedValue (pitchSemis (p), 1, " st");
        case T::tapeStop: return p < 0.01f ? juce::String ("Linear") : "Exp " + percent (p);
        case T::fade:     return juce::String (20.0 * std::log10 (fadeFactor (p)), 1) + " dB/rep";
        case T::freeze:   return first ? juce::String (juce::roundToInt (freezeGrainMs (p))) + " ms" : percent (p);
        case T::pingPong: return first ? percent (p) : juce::String (20.0 * std::log10 (pingDecay (p)), 1) + " dB/rep";
        case T::shuffle:
        case T::scratch:  return percent (p);
    }
    return percent (p);
}

// The name a lock target has for a given step type ("Pitch", "Offset", "Level" ...).
inline juce::String targetName (ModTarget t, int type)
{
    type = std::clamp (type, 0, kNumStutterTypes - 1);
    if (t == ModTarget::ctl1) return kTypeInfo[(size_t) type].c1.name;
    if (t == ModTarget::ctl2) return kTypeInfo[(size_t) type].c2.name;
    return kModTargetNames[(int) t];
}

// A lock value (0..1) for target t, read as the knob would show it on 'owner'.
inline juce::String modValue (ModTarget t, const StepData& owner, float v, int slices)
{
    StepData s = owner;
    applyMod (s, t, v);
    switch (t)
    {
        case ModTarget::ctl1:   return typeControl (s.type, true, s.p1, slices);
        case ModTarget::ctl2:   return typeControl (s.type, false, s.p2, slices);
        case ModTarget::level:  return level (s.levelDb);
        case ModTarget::pan:    return pan (s.pan);
        case ModTarget::filter: return filter (s.filter);
        case ModTarget::bits:   return bits (s.bits);
        case ModTarget::rate:   return rate (s.rate);
        case ModTarget::send:   return percent (s.send);
    }
    return {};
}

// Bipolar targets draw their bars from the middle.
inline bool isBipolar (ModTarget t, int type)
{
    if (t == ModTarget::pan || t == ModTarget::filter) return true;
    return (t == ModTarget::ctl1 || t == ModTarget::ctl2) && (StutterType) std::clamp (type, 0, kNumStutterTypes - 1) == StutterType::pitch;
}
} // namespace gn::fmt
