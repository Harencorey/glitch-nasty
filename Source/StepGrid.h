#pragma once
//==============================================================================
// Glitch Nasty - the step pattern grid.
//
// Each column is one step of the pattern being edited:
//   number   click to select the step (its controls open in the step editor);
//            lights green as the playhead passes, pink while it's glitching
//   pad      click to switch the step on/off (click-drag paints across steps).
//            An on step has [ ] brackets at its edges: drag either edge to make
//            the glitch last more (or fewer) steps - a tie. Inside a tie, click a
//            step to add a LAYER: its own glitch stacked on top of the tied one.
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
    void setSelectedStep (int step); // -1 = none (a live pad is being edited)
    int getSelectedStep() const noexcept { return selected; }

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

private:
    enum class Row { none, number, pad, type, chance, repeat };
    enum class DragMode { none, paintPads, drawChance, resizeLeft, resizeRight };

    struct Edge { int owner = -1; bool left = false; };

    void timerCallback() override { repaint(); }

    int columnAt (float x) const;
    Row rowAt (float y) const;
    juce::Rectangle<float> cell (int column, Row row) const;
    juce::Rectangle<float> columnBounds (int column) const;
    juce::Rectangle<float> spanBounds (int start, int span) const;
    Edge edgeAt (juce::Point<float> pos) const;

    // owner[i] == i for a normal step; otherwise the tied step that covers it.
    static std::array<int, gn::kMaxSteps> coverOwners (const gn::Pattern&, int numSteps);
    static int spanOf (const gn::Pattern&, int owner, int numSteps);

    void select (int step);
    void setChanceFromY (int column, float y);
    void showChoiceMenu (int column, Row row);
    static void drawBracket (juce::Graphics&, juce::Rectangle<float> pad, bool left, juce::Colour);

    GlitchNastyProcessor& processor;
    int selected = 0;
    DragMode dragMode = DragMode::none;
    bool paintValue = false;
    int dragOwner = -1, dragAnchor = -1; // resize: the step being resized and its fixed end
    Edge hoverEdge;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepGrid)
};
