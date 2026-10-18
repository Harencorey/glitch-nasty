#pragma once
//==============================================================================
// Glitch Nasty - the step pattern grid.
//
// Each column is one step:
//   number   lights up as the playhead passes; magenta when that step is glitching
//   pad      click to switch the step on/off (click-drag paints across steps)
//   chance   drag up/down for the probability (drag across steps to draw a curve);
//            double-click = 100 %
//   repeat   click for the repeat length menu
//==============================================================================

#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace gn::colours
{
const juce::Colour background { 0xff0e0f11 };
const juce::Colour panel      { 0xff191a1e };
const juce::Colour outline    { 0xff2b2d33 };
const juce::Colour text       { 0xffd8dade };
const juce::Colour dimText    { 0xff6c6f78 };
const juce::Colour acid       { 0xffb6ff3b };
const juce::Colour nasty      { 0xffff2e88 };
} // namespace gn::colours

class StepGrid : public juce::Component,
                 public juce::TooltipClient,
                 private juce::Timer
{
public:
    explicit StepGrid (GlitchNastyProcessor&);
    ~StepGrid() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

private:
    enum class Row { none, number, pad, chance, repeat };
    enum class DragMode { none, paintPads, drawChance };

    struct StepParams
    {
        juce::RangedAudioParameter* on = nullptr;
        juce::RangedAudioParameter* chance = nullptr;
        juce::RangedAudioParameter* repeat = nullptr;
    };

    void timerCallback() override;

    int columnAt (float x) const;
    Row rowAt (float y) const;
    juce::Rectangle<float> cell (int column, Row row) const;
    int numActiveSteps() const;

    void setParam (juce::RangedAudioParameter* p, float plainValue);
    void touch (int column, juce::RangedAudioParameter* p);
    void releaseAllGestures();
    void setChanceFromY (int column, float y);
    void showRepeatMenu (int column);

    GlitchNastyProcessor& processor;
    std::array<StepParams, gn::kMaxSteps> steps;
    juce::RangedAudioParameter* stepsParam = nullptr;

    DragMode dragMode = DragMode::none;
    bool paintValue = false;
    std::array<juce::RangedAudioParameter*, gn::kMaxSteps> touched {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepGrid)
};
