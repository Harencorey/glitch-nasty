#pragma once
//==============================================================================
// Glitch Nasty - the step pattern grid.
//
// Left gutter: one label per lane (click = edit that lane) and + / - to add a
// lane or remove the one being edited (each pattern has 1-4 lanes).
// Each column is one step. Rows, top to bottom:
//   number   click to select the step (its controls open in the step editor);
//            lights green as the playhead passes, pink while it's glitching
//   lanes    one row of pads per lane. Lane 1 is the main sequence; each lane
//            below glitches the output of the ones above it. Click an empty
//            pad to switch it on (drag to paint more); click an on step to
//            select it; DOUBLE-click an on step to switch it off - so clicking
//            around while you tweak never knocks a step out. Drag the [ ] edges
//            of an on step to stretch it over more steps (a tie).
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
    enum class Row { none, number, lane, type, bars, repeat };
    enum class DragMode { none, paintPads, drawBars, resizeLeft, resizeRight };
    enum class GutterHit { none, laneLabel, plus, minus };

    struct Hit { Row row = Row::none; int lane = -1; };
    struct Edge { int lane = -1, owner = -1; bool left = false; };

    void timerCallback() override { repaint(); }

    // Layout (depends on how many lanes the pattern has).
    int numLanes() const;
    float laneTop (int lane) const noexcept;
    float laneHeight (int lane) const noexcept { return lane == 0 ? 34.0f : 26.0f; }
    float typeTop() const noexcept;
    float barsTop() const noexcept { return typeTop() + 22.0f + 6.0f; }
    float columnWidth() const noexcept;

    int columnAt (float x) const;
    Hit hitAt (juce::Point<float>) const;
    GutterHit gutterAt (juce::Point<float>, int& lane) const;
    juce::Rectangle<float> cell (int column, Row row, int lane = 0) const;
    juce::Rectangle<float> columnBounds (int column) const;
    juce::Rectangle<float> spanBounds (int lane, int start, int span) const;
    juce::Rectangle<float> plusBounds() const;
    juce::Rectangle<float> minusBounds() const;
    Edge edgeAt (juce::Point<float> pos) const;

    // owner[i] == i for a normal step; otherwise the tied step that covers it.
    static std::array<int, gn::kMaxSteps> coverOwners (const gn::Steps&, int numSteps);
    static int spanOf (const gn::Steps&, int owner, int numSteps);

    int lane() const { return processor.getActiveLane(); }
    void setLane (int l);
    void select (int step);
    void setBarFromY (int column, float y);
    void showChoiceMenu (int column, Row row);
    void removeLaneAsked();
    void paintGutter (juce::Graphics&, int lanes);
    void paintLaneRow (juce::Graphics&, const gn::Pattern&, int lane, int active, int playing, bool showPlayhead);
    void paintBars (juce::Graphics&, const gn::Pattern&, int active);
    static void drawBracket (juce::Graphics&, juce::Rectangle<float> pad, bool left, juce::Colour);

    GlitchNastyProcessor& processor;
    int selected = 0;
    DragMode dragMode = DragMode::none;
    int dragLane = 0, dragOwner = -1, dragAnchor = -1; // resize: the step being resized and its fixed end
    int justSwitchedOnLane = -1, justSwitchedOnCol = -1; // so a double-click on an empty pad doesn't undo itself
    Edge hoverEdge;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepGrid)
};
