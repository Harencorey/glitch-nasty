#pragma once
//==============================================================================
// Glitch Nasty - parameter IDs and layout.
// Changing an ID later breaks saved sessions/automation, so they live here once.
//==============================================================================

#include "GlitchCore.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace gn::ids
{
inline constexpr const char* sync   = "sync";
inline constexpr const char* bpm    = "bpm";
inline constexpr const char* grid   = "grid";
inline constexpr const char* steps  = "steps";
inline constexpr const char* xfade  = "xfade";
inline constexpr const char* mix    = "mix";
inline constexpr const char* output = "output";

inline juce::String stepOn (int i)   { return "s" + juce::String (i + 1) + "_on"; }
inline juce::String stepProb (int i) { return "s" + juce::String (i + 1) + "_prob"; }
inline juce::String stepDiv (int i)  { return "s" + juce::String (i + 1) + "_div"; }
inline juce::String stepType (int i) { return "s" + juce::String (i + 1) + "_type"; }
} // namespace gn::ids

namespace gn
{

inline juce::StringArray namesOf (const char* const* names, int n)
{
    juce::StringArray a;
    for (int i = 0; i < n; ++i) a.add (names[i]);
    return a;
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    constexpr int v = 1; // parameter version hint (AU needs this)

    auto withText = [] (std::function<String (float, int)> f) { return AudioParameterFloatAttributes().withStringFromValueFunction (std::move (f)); };

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::sync, v }, "Sync",
                                                        StringArray { "Host", "Internal" }, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::bpm, v }, "Internal BPM",
                                                       NormalisableRange<float> (40.0f, 300.0f, 0.01f), 120.0f,
                                                       withText ([] (float x, int) { return String (x, 1) + " BPM"; })));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::grid, v }, "Grid",
                                                        namesOf (kGridNames, kNumGrids), kDefaultGrid));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { ids::steps, v }, "Steps", 1, kMaxSteps, kMaxSteps));

    NormalisableRange<float> xfRange (0.0f, 20.0f, 0.01f);
    xfRange.setSkewForCentre (3.0f);
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::xfade, v }, "Crossfade", xfRange, 3.0f,
                                                       withText ([] (float x, int) { return String (x, 1) + " ms"; })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::mix, v }, "Mix",
                                                       NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
                                                       withText ([] (float x, int) { return String (roundToInt (x)) + " %"; })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::output, v }, "Output",
                                                       NormalisableRange<float> (-24.0f, 12.0f, 0.01f), 0.0f,
                                                       withText ([] (float x, int) { return String (x, 1) + " dB"; })));

    // A starter pattern so the plug-in does something the first time you load it.
    // Types: 0 Repeat, 1 Reverse, 2 Gate, 3 Roll, 4 Pitch+, 5 Pitch-, 6 Tape Stop, 7 Fade
    // Divisions: 5 = 1/16, 6 = 1/16T, 7 = 1/32, 8 = 1/32T, 9 = 1/64
    struct Default { bool on; float prob; int div; int type; };
    const Default defaults[kMaxSteps] = {
        { false, 100, 7, 0 }, { false, 100, 7, 0 }, { false, 100, 7, 0 }, { true, 100, 7, 0 },
        { false, 100, 7, 0 }, { false, 100, 7, 0 }, { true, 50, 6, 1 },   { false, 100, 7, 0 },
        { false, 100, 7, 0 }, { false, 100, 7, 0 }, { true, 60, 7, 2 },   { true, 100, 7, 4 },
        { false, 100, 7, 0 }, { false, 100, 7, 0 }, { true, 75, 7, 3 },   { true, 100, 5, 6 },
    };

    for (int i = 0; i < kMaxSteps; ++i)
    {
        auto group = std::make_unique<AudioProcessorParameterGroup> ("step" + String (i + 1), "Step " + String (i + 1), " | ");
        const String n = "Step " + String (i + 1) + " ";
        group->addChild (std::make_unique<AudioParameterBool> (ParameterID { ids::stepOn (i), v }, n + "On", defaults[i].on));
        group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { ids::stepProb (i), v }, n + "Chance",
                                                                NormalisableRange<float> (0.0f, 100.0f, 1.0f), defaults[i].prob,
                                                                withText ([] (float x, int) { return String (roundToInt (x)) + " %"; })));
        group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { ids::stepDiv (i), v }, n + "Repeat",
                                                                 namesOf (kDivisionNames, kNumDivisions), defaults[i].div));
        group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { ids::stepType (i), v }, n + "Type",
                                                                 namesOf (kTypeNames, kNumStutterTypes), defaults[i].type));
        layout.add (std::move (group));
    }

    return layout;
}

} // namespace gn
