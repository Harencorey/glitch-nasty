#pragma once
//==============================================================================
// Glitch Nasty - the step pattern grid.
//
// Each column is one step. Rows, top to bottom:
//   number   click to select the step (its controls open in the step editor);
//            lights green as the playhead passes, pink while it's glitching
//   lane 1   pads for lane 1 (acid). Click = on/off, click-drag paints.
//   lane 2   pads for lane 2 (blue): a second sequence whose glitches process
//            lane 1's output. Clicking either row makes that lane the one the
//            rows below (and the step editor) show.
//            An on step has [ ] brackets at its edges: drag either edge to make
//            the glitch last more (or fewer) steps - a tie.
//   type     click for the glitch type menu (of the lane being edited)
//   bars     what the BARS selector picks: Chance, or a parameter lock -
//            drag to set the value per step (even in the middle of a tie),
//            double-click to clear it (the step's own setting shows as a ghost)
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

    std::function<void (int lane, int column)> onStepSelected;
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
    enum class Row { none, number, lane1, lane2, type, bars, repeat };
    enum class DragMode { none, paintPads, drawBars, resizeLeft, resizeRight };

    struct Edge { int lane = -1, owner = -1; bool left = false; };

    void timerCallback() override { repaint(); }

    int columnAt (float x) const;
    Row rowAt (float y) const;
    static int laneOf (Row r) noexcept { return r == Row::lane1 ? 0 : (r == Row::lane2 ? 1 : -1); }
    static Row padRow (int lane) noexcept { return lane == 0 ? Row::lane1 : Row::lane2; }
    juce::Rectangle<float> cell (int column, Row row) const;
    juce::Rectangle<float> columnBounds (int column) const;
    juce::Rectangle<float> spanBounds (int lane, int start, int span) const;
    Edge edgeAt (juce::Point<float> pos) const;

    // owner[i] == i for a normal step; otherwise the tied step that covers it.
    static std::array<int, gn::kMaxSteps> coverOwners (const gn::Steps&, int numSteps);
    static int spanOf (const gn::Steps&, int owner, int numSteps);

    int lane() const noexcept { return processor.getActiveLane(); }
    void setLane (int l);
    void select (int step);
    void setBarFromY (int column, float y);
    void showChoiceMenu (int column, Row row);
    void paintLaneRow (juce::Graphics&, const gn::Pattern&, int lane, int active, int playing, bool showPlayhead, bool live);
    void paintBars (juce::Graphics&, const gn::Pattern&, int active);
    static void drawBracket (juce::Graphics&, juce::Rectangle<float> pad, bool left, juce::Colour);

    GlitchNastyProcessor& processor;
    int selected = 0;
    DragMode dragMode = DragMode::none;
    bool paintValue = false;
    int dragLane = 0, dragOwner = -1, dragAnchor = -1; // resize: the step being resized and its fixed end
    Edge hoverEdge;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepGrid)
};
