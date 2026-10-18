#pragma once
//==============================================================================
// Glitch Nasty - live panel: hold a pad to glitch right now (overrides the
// pattern), plus the live repeat length and quantize. The same thing works
// from MIDI notes (C3 upward), so a pad controller or keyboard can play it.
//==============================================================================

#include "PluginProcessor.h"
#include "Theme.h"

class LivePad : public juce::Component,
                public juce::SettableTooltipClient
{
public:
    LivePad (GlitchNastyProcessor& p, int t);

    std::function<void (int)> onPressed; // tells the panel this pad was clicked
    bool selected = false;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    GlitchNastyProcessor& processor;
    const int type;
    bool pressed = false;
};

class LivePanel : public juce::Component,
                  private juce::Timer
{
public:
    explicit LivePanel (GlitchNastyProcessor&);

    std::function<void (int)> onPadSelected; // a pad was clicked: edit its settings
    void setSelectedPad (int type);          // -1 = none

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    GlitchNastyProcessor& processor;
    std::vector<std::unique_ptr<LivePad>> pads;
    juce::ComboBox repeatBox, quantBox;
    juce::Label repeatLabel, quantLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> repeatAttachment, quantAttachment;
    int shownLive = -2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LivePanel)
};
