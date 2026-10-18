#pragma once
//==============================================================================
// Glitch Nasty - the step pattern grid.
//
// Each column is one step of the pattern being edited:
//   number   click to select the step (its controls open in the step editor);
//            lights green as the playhead passes, pink while it's glitching
//   pad      click to switch the step on/off (click-drag paints across steps);
//            a tied step draws as one long pad across the steps it covers
//   type     click for the glitch type menu
//   chance   drag up/down (drag sideways to draw across steps); double-click = 100 %
//   repeat   click for the repeat length menu
//==============================================================================

#include "PluginProcessor.h"
#include "Theme.h"

class StepGrid : public juce::Component,
                 public juce::TooltipClient,
                 private juce::Timer
{
public:
    explicit StepGrid (GlitchNastyProcessor&);

    std::function<void (int)> onStepSelected;
    void setSelectedStep (int step);
    int getSelectedStep() const noexcept { return selected; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

private:
    enum class Row { none, number, pad, type, chance, repeat };
    enum class DragMode { none, paintPads, drawChance };

    void timerCallback() override { repaint(); }

    int columnAt (float x) const;
    Row rowAt (float y) const;
    juce::Rectangle<float> cell (int column, Row row) const;
    juce::Rectangle<float> columnBounds (int column) const;

    // owner[i] == i for a normal step; otherwise the tied step that covers it.
    static std::array<int, gn::kMaxSteps> coverOwners (const gn::Pattern&, int numSteps);

    void select (int step);
    void setChanceFromY (int column, float y);
    void showChoiceMenu (int column, Row row);

    GlitchNastyProcessor& processor;
    int selected = 0;
    DragMode dragMode = DragMode::none;
    bool paintValue = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepGrid)
};
