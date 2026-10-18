#pragma once

#include "PluginProcessor.h"
#include "StepGrid.h"

#include <juce_audio_processors/juce_audio_processors.h>

class GlitchNastyLookAndFeel : public juce::LookAndFeel_V4
{
public:
    GlitchNastyLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle, juce::Slider&) override;
};

class GlitchNastyEditor : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit GlitchNastyEditor (GlitchNastyProcessor&);
    ~GlitchNastyEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void setupKnob (Knob&, const char* paramId, const juce::String& name, const juce::String& help);
    void setupCombo (juce::ComboBox&, juce::Label&, const char* paramId, const juce::String& name, const juce::String& help,
                     std::unique_ptr<ComboAttachment>&);
    void timerCallback() override;
    void tap();

    GlitchNastyProcessor& processor;
    GlitchNastyLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this, 3000 }; // hover help after ~3 s, same as Glow

    juce::ComboBox syncBox, gridBox;
    juce::Label syncLabel, gridLabel;
    std::unique_ptr<ComboAttachment> syncAttachment, gridAttachment;

    Knob bpm, steps, xfade, mix, output;
    juce::TextButton tapButton { "TAP" };
    juce::Label clockReadout;

    StepGrid grid;

    std::vector<double> tapTimes;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlitchNastyEditor)
};
