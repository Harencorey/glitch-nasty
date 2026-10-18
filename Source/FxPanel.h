#pragma once
//==============================================================================
// Glitch Nasty - effects row: global crusher, tempo-synced delay, reverb,
// and what feeds the delay/reverb (just the glitches, or everything).
//==============================================================================

#include "PluginProcessor.h"
#include "Theme.h"

class FxPanel : public juce::Component
{
public:
    explicit FxPanel (GlitchNastyProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Section { juce::String title; juce::Rectangle<int> bounds; };

    void knob (gn::ui::Knob&, const char* id, const juce::String& name, const juce::String& help, juce::Colour accent, bool bipolar = false);
    void combo (juce::ComboBox&, juce::Label&, const char* id, const juce::String& name, const juce::String& help);

    GlitchNastyProcessor& processor;
    gn::ui::Knob crushBits, crushRate, crushMix, dlyFb, dlyTone, dlyMix, revSize, revDamp, revMix;
    juce::ComboBox dlyTime, source;
    juce::Label dlyTimeLabel, sourceLabel;
    juce::TextButton pingButton { "PING-PONG" };
    std::vector<std::unique_ptr<SliderAttachment>> sliderAtts;
    std::vector<std::unique_ptr<ComboAttachment>> comboAtts;
    std::unique_ptr<ButtonAttachment> pingAtt;
    std::array<Section, 4> sections;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxPanel)
};
