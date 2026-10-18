#pragma once
//==============================================================================
// Glitch Nasty - parameter IDs and layout.
// Changing an ID later breaks saved sessions/automation, so they live here once.
//
// These are the automatable, global controls. The step patterns themselves
// (8 patterns x 16 steps) are saved in the session state, not as parameters -
// 1,500+ parameters would choke hosts.
//==============================================================================

#include "GlitchCore.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace gn::ids
{
inline constexpr const char* sync      = "sync";
inline constexpr const char* bpm       = "bpm";
inline constexpr const char* grid      = "grid";
inline constexpr const char* steps     = "steps";
inline constexpr const char* xfade     = "xfade";
inline constexpr const char* mix       = "mix";
inline constexpr const char* output    = "output";
inline constexpr const char* swing     = "swing";
inline constexpr const char* density   = "density";
inline constexpr const char* seed      = "seed";
inline constexpr const char* pattern   = "pattern";
inline constexpr const char* engage    = "engage";
inline constexpr const char* liveDiv   = "live_div";
inline constexpr const char* liveQuant = "live_quant";
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

    // ---- added in 0.2 ----
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::swing, v }, "Swing",
                                                       NormalisableRange<float> (50.0f, 75.0f, 0.1f), 50.0f,
                                                       withText ([] (float x, int) { return String (x, 1) + " %"; })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::density, v }, "Density",
                                                       NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f,
                                                       withText ([] (float x, int) { const int i = roundToInt (x); return (i > 0 ? "+" : "") + String (i) + " %"; })));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { ids::seed, v }, "Seed", 0, 999, 1));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::pattern, v }, "Pattern",
                                                        namesOf (kPatternNames, kNumPatterns), 0));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::engage, v }, "Engage", true));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::liveDiv, v }, "Live Repeat",
                                                        namesOf (kDivisionNames, kNumDivisions), kDiv16));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::liveQuant, v }, "Live Quantize",
                                                        namesOf (kQuantNames, kNumQuants), 1));
    return layout;
}

} // namespace gn
