#pragma once

#include "FxPanel.h"
#include "LivePanel.h"
#include "PatternBar.h"
#include "PluginProcessor.h"
#include "StepEditor.h"
#include "StepGrid.h"
#include "Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

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
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void attachKnob (gn::ui::Knob&, std::unique_ptr<SliderAttachment>&, const char* paramId, const juce::String& name,
                     const juce::String& help, bool bipolar = false);
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

    gn::ui::Knob bpm, steps, swing, xfade, mix, density, seed, output;
    std::unique_ptr<SliderAttachment> bpmAtt, stepsAtt, swingAtt, xfadeAtt, mixAtt, densityAtt, seedAtt, outputAtt;

    juce::TextButton tapButton { "TAP" };
    juce::Label clockReadout;

    juce::TextButton engageButton { "ENGAGE" };
    std::unique_ptr<ButtonAttachment> engageAttachment;
    juce::Label engageStatus;

    PatternBar patternBar;
    StepGrid grid;
    StepEditor stepEditor;
    LivePanel livePanel;
    FxPanel fxPanel;

    std::vector<double> tapTimes;
    int blink = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlitchNastyEditor)
};
