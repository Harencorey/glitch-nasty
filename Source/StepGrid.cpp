#include "StepGrid.h"

namespace
{
constexpr float kNumberH = 20.0f;
constexpr float kPadH    = 40.0f;
constexpr float kTypeH   = 22.0f;
constexpr float kRepeatH = 22.0f;
constexpr float kGap     = 5.0f;
constexpr float kColumnPad = 3.0f;
constexpr float kEdgeGrab = 9.0f; // how close to a pad edge counts as grabbing it
} // namespace

StepGrid::StepGrid (GlitchNastyProcessor& p) : processor (p)
{
    startTimerHz (30);
}

void StepGrid::setSelectedStep (int step)
{
    selected = juce::jlimit (-1, gn::kMaxSteps - 1, step);
    repaint();
}

void StepGrid::select (int step)
{
    setSelectedStep (step);
    if (onStepSelected)
        onStepSelected (selected);
}

//==============================================================================
int StepGrid::columnAt (float x) const
{
    const float w = (float) getWidth() / (float) gn::kMaxSteps;
    return juce::jlimit (0, gn::kMaxSteps - 1, (int) std::floor (x / w));
}

StepGrid::Row StepGrid::rowAt (float y) const
{
    const float h = (float) getHeight();
    const float typeTop = kNumberH + kPadH + kGap;
    if (y < kNumberH) return Row::number;
    if (y < kNumberH + kPadH) return Row::pad;
    if (y >= typeTop && y < typeTop + kTypeH) return Row::type;
    if (y >= h - kRepeatH) return Row::repeat;
    if (y >= typeTop + kTypeH + kGap && y < h - kRepeatH - kGap) return Row::chance;
    return Row::none;
}

juce::Rectangle<float> StepGrid::cell (int column, Row row) const
{
    const float w = (float) getWidth() / (float) gn::kMaxSteps;
    const float x = (float) column * w + kColumnPad;
    const float cw = w - 2.0f * kColumnPad;
    const float h = (float) getHeight();

    switch (row)
    {
        case Row::number: return { x, 0.0f, cw, kNumberH };
        case Row::pad:    return { x, kNumberH, cw, kPadH };
        case Row::type:   return { x, kNumberH + kPadH + kGap, cw, kTypeH };
        case Row::chance:
        {
            const float top = kNumberH + kPadH + kTypeH + 2.0f * kGap;
            return { x, top, cw, h - top - kRepeatH - kGap };
        }
        case Row::repeat: return { x, h - kRepeatH, cw, kRepeatH };
        case Row::none:   break;
    }
    return {};
}

juce::Rectangle<float> StepGrid::columnBounds (int column) const
{
    const float w = (float) getWidth() / (float) gn::kMaxSteps;
    return { (float) column * w + 1.0f, 0.0f, w - 2.0f, (float) getHeight() };
}

juce::Rectangle<float> StepGrid::spanBounds (int start, int span) const
{
    return cell (start, Row::pad).withRight (cell (start + span - 1, Row::pad).getRight());
}

int StepGrid::spanOf (const gn::Pattern& pat, int owner, int numSteps)
{
    return std::max (1, std::min (pat[(size_t) owner].length, numSteps - owner));
}

std::array<int, gn::kMaxSteps> StepGrid::coverOwners (const gn::Pattern& pat, int numSteps)
{
    std::array<int, gn::kMaxSteps> owner {};
    for (int i = 0; i < gn::kMaxSteps; ++i)
        owner[(size_t) i] = i;

    for (int i = 0; i < numSteps; ++i)
    {
        const auto& s = pat[(size_t) i];
        const int len = spanOf (pat, i, numSteps);
        if (s.on && len > 1)
        {
            for (int j = i + 1; j < i + len; ++j)
                owner[(size_t) j] = i;
            i += len - 1;
        }
    }
    return owner;
}

StepGrid::Edge StepGrid::edgeAt (juce::Point<float> pos) const
{
    if (rowAt (pos.y) != Row::pad)
        return {};

    const auto pat = processor.getPattern (processor.getEditPattern());
    const int n = processor.getNumSteps();
    const auto owner = coverOwners (pat, n);
    for (int i = 0; i < n; ++i)
    {
        if (owner[(size_t) i] != i || ! pat[(size_t) i].on)
            continue;
        const auto b = spanBounds (i, spanOf (pat, i, n));
        if (std::abs (pos.x - b.getX()) <= kEdgeGrab)     return { i, true };
        if (std::abs (pos.x - b.getRight()) <= kEdgeGrab) return { i, false };
    }
    return {};
}

void StepGrid::drawBracket (juce::Graphics& g, juce::Rectangle<float> pad, bool left, juce::Colour c)
{
    const float x = left ? pad.getX() + 4.0f : pad.getRight() - 4.0f;
    const float dir = left ? 1.0f : -1.0f;
    const float top = pad.getY() + 6.0f, bottom = pad.getBottom() - 6.0f;
    juce::Path p;
    p.startNewSubPath (x + dir * 5.0f, top);
    p.lineTo (x, top);
    p.lineTo (x, bottom);
    p.lineTo (x + dir * 5.0f, bottom);
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::square));
}

//==============================================================================
void StepGrid::paint (juce::Graphics& g)
{
    using namespace gn::colours;
    const int pat = processor.getEditPattern();
    const auto pattern = processor.getPattern (pat);
    const int active = processor.getNumSteps();
    const bool live = processor.uiLiveType.load() >= 0;
    const bool showPlayhead = processor.uiPattern.load() == pat;
    const int playing = showPlayhead ? processor.uiPlayingStep.load() : -1;
    const int firing = (showPlayhead && ! live) ? processor.uiFiringStep.load() : -1;
    const int firingLen = processor.uiFiringLen.load();
    const int layerPlaying = showPlayhead ? processor.uiLayerStep.load() : -1;
    const auto owner = coverOwners (pattern, active);

    if (selected >= 0)
    {
        const auto sel = columnBounds (selected);
        g.setColour (acid.withAlpha (0.06f));
        g.fillRoundedRectangle (sel, 5.0f);
        g.setColour (acid.withAlpha (0.45f));
        g.drawRoundedRectangle (sel.reduced (0.5f), 5.0f, 1.2f);
    }

    for (int i = 0; i < gn::kMaxSteps; ++i)
    {
        const auto& s = pattern[(size_t) i];
        const float alpha = i < active ? 1.0f : 0.25f;
        const bool covered = owner[(size_t) i] != i;
        const bool isLayer = covered && s.on;
        const bool inFiring = firing >= 0 && i >= firing && i < firing + firingLen;

        // Step number / playhead
        auto num = cell (i, Row::number);
        if (i == playing)
        {
            g.setColour ((inFiring || i == layerPlaying ? nasty : acid).withAlpha (0.9f));
            g.fillRoundedRectangle (num.reduced (0.0f, 3.0f), 3.0f);
        }
        g.setColour (i == playing ? background : (i % 4 == 0 ? text : dimText).withMultipliedAlpha (alpha));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (juce::String (i + 1), num, juce::Justification::centred);

        // Pad: an on step (with its tie) is one bar with [ ] brackets at the ends.
        if (! covered)
        {
            const int span = s.on ? spanOf (pattern, i, active) : 1;
            auto pad = spanBounds (i, span);
            g.setColour (s.on ? (inFiring ? nasty : acid).withAlpha (alpha) : panel);
            g.fillRoundedRectangle (pad, 4.0f);
            g.setColour (outline.withMultipliedAlpha (alpha));
            g.drawRoundedRectangle (pad, 4.0f, 1.0f);

            if (s.on && i < active)
            {
                const bool hl = hoverEdge.owner == i || (dragOwner == i && dragMode != DragMode::none);
                const auto c = background.withAlpha (hl ? 0.95f : 0.55f);
                drawBracket (g, pad, true, hoverEdge.owner == i && hoverEdge.left ? background : c);
                drawBracket (g, pad, false, hoverEdge.owner == i && ! hoverEdge.left ? background : c);
                if (span > 1)
                {
                    g.setColour (background.withAlpha (0.55f));
                    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
                    g.drawText (juce::String (span) + " STEPS", cell (i, Row::pad).reduced (10.0f, 0.0f).withWidth (70.0f),
                                juce::Justification::centredLeft);
                }
            }
        }

        // Layers inside a tie: a pink block on the tie bar.
        if (isLayer)
        {
            auto block = cell (i, Row::pad).reduced (4.0f, 8.0f);
            const bool lit = i == layerPlaying;
            g.setColour ((lit ? nasty.brighter (0.4f) : nasty).withAlpha (alpha));
            g.fillRoundedRectangle (block, 3.0f);
            g.setColour (background.withAlpha (0.85f));
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.drawText (gn::kTypeShortNames[juce::jlimit (0, gn::kNumStutterTypes - 1, s.type)], block, juce::Justification::centred);
        }

        const float rowAlpha = (covered && ! isLayer) ? 0.25f * alpha : alpha;

        // Type
        auto typeCell = cell (i, Row::type);
        g.setColour (panel);
        g.fillRoundedRectangle (typeCell, 3.0f);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        if (covered && ! isLayer)
        {
            g.setColour (dimText.withMultipliedAlpha (alpha));
            g.drawText ("tied", typeCell, juce::Justification::centred);
        }
        else
        {
            const bool pink = isLayer || (s.on && s.type != 0);
            g.setColour ((s.on ? (pink ? nasty.brighter (0.3f) : text) : dimText).withMultipliedAlpha (rowAlpha));
            g.drawText ((isLayer ? "+" : "") + juce::String (gn::kTypeShortNames[juce::jlimit (0, gn::kNumStutterTypes - 1, s.type)]),
                        typeCell, juce::Justification::centred);
        }

        // Chance bar
        auto bar = cell (i, Row::chance);
        g.setColour (panel);
        g.fillRoundedRectangle (bar, 3.0f);
        auto fill = bar.withTop (bar.getBottom() - bar.getHeight() * s.chance);
        g.setColour (s.on ? (isLayer ? nasty : acid).withAlpha (0.55f * rowAlpha) : dimText.withAlpha (0.08f * rowAlpha));
        g.fillRoundedRectangle (fill, 3.0f);
        g.setColour (outline.withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (bar, 3.0f, 1.0f);
        if (s.chance < 0.995f && (! covered || isLayer))
        {
            g.setColour (text.withAlpha (0.8f * rowAlpha));
            g.setFont (juce::FontOptions (10.0f));
            g.drawText (juce::String (juce::roundToInt (s.chance * 100.0f)), bar.removeFromBottom (16.0f), juce::Justification::centred);
        }

        // Small marks for level / pan / filter / crush
        if (s.on && (! covered || isLayer))
        {
            auto marks = cell (i, Row::chance).removeFromTop (12.0f).reduced (2.0f, 2.0f);
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.setColour (background.withAlpha (0.8f));
            juce::String m;
            if (s.filter < -0.001f) m << "LP ";
            if (s.filter > 0.001f)  m << "HP ";
            if (s.bits > 0.001f || s.rate > 0.001f) m << "CR ";
            if (std::abs (s.pan) > 0.01f) m << (s.pan < 0 ? "L" : "R");
            if (std::abs (s.levelDb) > 0.05f) m << (s.levelDb < 0 ? "-" : "+");
            if (m.isNotEmpty() && s.chance > 0.2f)
                g.drawText (m.trim(), marks, juce::Justification::centredTop);
        }

        // Repeat length
        auto rep = cell (i, Row::repeat);
        g.setColour (panel);
        g.fillRoundedRectangle (rep, 3.0f);
        g.setColour ((s.on ? text : dimText).withMultipliedAlpha (rowAlpha));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (gn::kDivisionNames[juce::jlimit (0, gn::kNumDivisions - 1, s.division)], rep, juce::Justification::centred);
    }

    // Beat dividers every 4 steps
    g.setColour (outline);
    const float w = (float) getWidth() / (float) gn::kMaxSteps;
    for (int i = 4; i < gn::kMaxSteps; i += 4)
        g.drawVerticalLine (juce::roundToInt ((float) i * w), kNumberH + kPadH + kGap, (float) getHeight());
}

//==============================================================================
void StepGrid::mouseMove (const juce::MouseEvent& e)
{
    const auto edge = edgeAt (e.position);
    if (edge.owner != hoverEdge.owner || edge.left != hoverEdge.left)
    {
        hoverEdge = edge;
        repaint();
    }
    setMouseCursor (edge.owner >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void StepGrid::mouseExit (const juce::MouseEvent&)
{
    hoverEdge = {};
    setMouseCursor (juce::MouseCursor::NormalCursor);
    repaint();
}

void StepGrid::setChanceFromY (int column, float y)
{
    auto bar = cell (column, Row::chance);
    const float v = juce::jlimit (0.0f, 1.0f, (bar.getBottom() - y) / bar.getHeight());
    processor.editStep (processor.getEditPattern(), column, [v] (gn::StepData& s) { s.chance = std::round (v * 100.0f) * 0.01f; });
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    const auto pos = e.position;
    const int col = columnAt (pos.x);
    const int pat = processor.getEditPattern();
    const int n = processor.getNumSteps();
    const auto pattern = processor.getPattern (pat);
    const auto owner = coverOwners (pattern, n);
    const bool covered = owner[(size_t) col] != col;
    const auto row = rowAt (pos.y);

    // Grab a [ or ] to change how many steps the glitch lasts.
    const auto edge = edgeAt (pos);
    if (edge.owner >= 0)
    {
        dragOwner = edge.owner;
        dragAnchor = edge.left ? edge.owner + spanOf (pattern, edge.owner, n) - 1 : edge.owner;
        dragMode = edge.left ? DragMode::resizeLeft : DragMode::resizeRight;
        select (edge.owner);
        return;
    }

    select (col);

    switch (row)
    {
        case Row::pad:
            if (covered)
            {
                // Inside a tie: switch a layer on/off on this step.
                paintValue = ! pattern[(size_t) col].on;
                processor.editStep (pat, col, [this] (gn::StepData& s) { s.on = paintValue; s.length = 1; });
                dragMode = DragMode::none;
            }
            else
            {
                paintValue = ! pattern[(size_t) col].on;
                dragMode = DragMode::paintPads;
                processor.editStep (pat, col, [this] (gn::StepData& s) { s.on = paintValue; });
            }
            break;
        case Row::chance:
            dragMode = DragMode::drawChance;
            setChanceFromY (col, pos.y);
            break;
        case Row::type:
        case Row::repeat:
            showChoiceMenu (col, row);
            break;
        case Row::number:
        case Row::none:
            break;
    }
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (dragMode == DragMode::none)
        return;

    const int col = columnAt (e.position.x);
    const int pat = processor.getEditPattern();
    const int n = processor.getNumSteps();

    if (dragMode == DragMode::resizeRight || dragMode == DragMode::resizeLeft)
    {
        const int c = juce::jlimit (0, n - 1, col);
        const auto pattern = processor.getPattern (pat);
        const int start = dragOwner, end = dragOwner + spanOf (pattern, dragOwner, n) - 1;
        int newStart = start, newEnd = end;
        if (dragMode == DragMode::resizeRight) newEnd = std::max (dragAnchor, c);
        else                                   newStart = std::min (dragAnchor, c);

        if (newStart != start || newEnd != end)
        {
            processor.setStepSpan (pat, start, newStart, newEnd);
            dragOwner = newStart;
            select (newStart);
        }
        return;
    }

    const auto owner = coverOwners (processor.getPattern (pat), n);
    if (owner[(size_t) col] != col)
        return;

    if (dragMode == DragMode::paintPads)
        processor.editStep (pat, col, [this] (gn::StepData& s) { s.on = paintValue; });
    else
        setChanceFromY (col, e.position.y);
}

void StepGrid::mouseUp (const juce::MouseEvent& e)
{
    dragMode = DragMode::none;
    dragOwner = dragAnchor = -1;
    mouseMove (e);
}

void StepGrid::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (rowAt (e.position.y) != Row::chance)
        return;
    processor.editStep (processor.getEditPattern(), columnAt (e.position.x), [] (gn::StepData& s) { s.chance = 1.0f; });
}

void StepGrid::showChoiceMenu (int column, Row row)
{
    const bool isType = (row == Row::type);
    const int pat = processor.getEditPattern();
    const auto step = processor.getStep (pat, column);
    const int current = isType ? step.type : step.division;

    juce::PopupMenu menu;
    menu.addSectionHeader ("Step " + juce::String (column + 1) + (isType ? " type" : " repeat"));
    const int count = isType ? gn::kNumStutterTypes : gn::kNumDivisions;
    for (int i = 0; i < count; ++i)
        menu.addItem (i + 1, isType ? gn::kTypeNames[i] : gn::kDivisionNames[i], true, i == current);

    juce::Component::SafePointer<StepGrid> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                  .withTargetScreenArea (localAreaToGlobal (cell (column, row).toNearestInt())),
                        [safe, isType, pat, column] (int result)
                        {
                            if (safe == nullptr || result <= 0)
                                return;
                            const int v = result - 1;
                            safe->processor.editStep (pat, column, [isType, v] (gn::StepData& s)
                            {
                                if (isType) { if (s.type != v) gn::setType (s, v); }
                                else s.division = v;
                            });
                            if (safe->onStepSelected)
                                safe->onStepSelected (column);
                        });
}

juce::String StepGrid::getTooltip()
{
    const auto pos = getMouseXYRelative().toFloat();
    if (edgeAt (pos).owner >= 0)
        return "Drag to make this glitch last more or fewer steps. Inside a long glitch, click a step to stack a second glitch on top (a layer).";

    switch (rowAt (pos.y))
    {
        case Row::number: return "Click to edit this step's controls below. Lights green as the playhead passes, pink while it's glitching.";
        case Row::pad:    return "Click to switch the step on or off; drag across steps to paint. Drag the [ ] edges of an on step to stretch it over several steps. "
                                 "Inside a stretched step, click a step to add a layer: its glitch plays on top of (and processes) the long one.";
        case Row::type:
        {
            const auto s = processor.getStep (processor.getEditPattern(), columnAt (pos.x));
            return juce::String (gn::kTypeNames[s.type]) + ": " + gn::kTypeInfo[(size_t) s.type].help + "  (Click to change.)";
        }
        case Row::chance: return "Chance this step glitches each time it comes around. Drag up/down; drag sideways to draw across steps. Double-click = 100%.";
        case Row::repeat: return "Repeat length: the slice that loops (for Gate, the on/off cycle; for Tape Stop, how long it takes). Click to choose.";
        case Row::none:   break;
    }
    return {};
}
