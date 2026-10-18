#pragma once
//==============================================================================
// Glitch Nasty - pattern bar: pattern tabs A-H and the pattern tools
// (copy / paste / shift / reverse / clear / randomize with locks).
//==============================================================================

#include "PluginProcessor.h"
#include "Theme.h"

class PatternBar : public juce::Component,
                   private juce::Timer
{
public:
    explicit PatternBar (GlitchNastyProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void selectPattern (int p);

    GlitchNastyProcessor& processor;
    std::array<juce::TextButton, gn::kNumPatterns> tabs;
    juce::TextButton copyButton { "COPY" }, pasteButton { "PASTE" }, leftButton { "<" }, rightButton { ">" },
                     reverseButton { "REV" }, clearButton { "CLEAR" }, randomButton { "RANDOM" };
    std::array<juce::TextButton, 5> locks;
    int shownPattern = -1, shownPlaying = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatternBar)
};
