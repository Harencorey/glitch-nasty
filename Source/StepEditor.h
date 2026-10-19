#pragma once
//==============================================================================
// Glitch Nasty - step editor.
//
// Edits either a step of the pattern (click a step number) or a live pad's
// own settings (click a pad). Shows the two type-specific controls (names,
// ranges and readouts change with the type), then Length (ties), Level, Pan,
// Filter, and the crusher (Bits, Rate) and the delay/reverb Send.
// Edits apply straight away, even to a glitch that's already playing.
//==============================================================================

#include "PluginProcessor.h"
#include "Theme.h"

class StepEditor : public juce::Component,
                   private juce::Timer
{
public:
    explicit StepEditor (GlitchNastyProcessor&);

    void setStep (int lane, int column); // edit the step that owns this column of a lane (itself, or the tie covering it)
    void setLivePad (int type);    // edit a live pad's settings
    bool isEditingLivePad() const noexcept { return liveTarget; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum { kCtl1, kCtl2, kLevel, kPan, kFilter, kBits, kRate, kSend, kNumKnobs };

    void timerCallback() override;
    void refresh (bool force);
    void configureTypeKnob (int which, int type);
    void knobChanged (int which);
    juce::String formatTypeControl (int which, double sliderValue) const;
    const gn::TypeControl& typeControl (int which) const;
    gn::StepData readTarget() const;
    void editTarget (const std::function<void (gn::StepData&)>& change);

    GlitchNastyProcessor& processor;
    std::array<gn::ui::Knob, kNumKnobs> knobs;
    std::array<gn::ui::ChoiceSwitch, 2> switches; // shown instead of CTL knobs when the control is a choice
    int ownerStep() const;   // the step being edited (the tie's first step when 'step' is inside one)
    int step = 0, lane = 0, liveType = 0;
    bool liveTarget = false;
    int shownType = -1, shownPattern = -1, shownStep = -1, shownLane = -1;
    bool shownLive = false;
    bool updating = false;
    gn::StepData current;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepEditor)
};
