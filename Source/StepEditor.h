#pragma once
//==============================================================================
// Glitch Nasty - step editor.
//
// Shows the selected step's controls: the two type-specific ones (their names,
// ranges and readouts change with the type), then Length (ties), Level, Pan
// and Filter. Edits go straight into the pattern.
//==============================================================================

#include "PluginProcessor.h"
#include "Theme.h"

class StepEditor : public juce::Component,
                   private juce::Timer
{
public:
    explicit StepEditor (GlitchNastyProcessor&);

    void setStep (int newStep);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum { kCtl1, kCtl2, kLength, kLevel, kPan, kFilter, kNumKnobs };

    void timerCallback() override;
    void refresh (bool force);
    void configureTypeKnob (int which, int type);
    void knobChanged (int which);
    juce::String formatTypeControl (int which, double sliderValue) const;
    const gn::TypeControl& typeControl (int which) const;

    GlitchNastyProcessor& processor;
    std::array<gn::ui::Knob, kNumKnobs> knobs;
    int step = 0, shownType = -1, shownPattern = -1, shownStep = -1;
    bool updating = false;
    gn::StepData current;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepEditor)
};
