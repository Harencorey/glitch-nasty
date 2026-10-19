#include "StepGrid.h"
#include "Format.h"

namespace
{
constexpr float kNumberH = 20.0f;
constexpr float kLane1H  = 36.0f;
constexpr float kLane2H  = 30.0f;
constexpr float kLaneGap = 4.0f;
constexpr float kTypeH   = 22.0f;
constexpr float kRepeatH = 22.0f;
constexpr float kGap     = 6.0f;
constexpr float kColumnPad = 3.0f;
constexpr float kEdgeGrab = 9.0f; // how close to a pad edge counts as grabbing it

constexpr float kLane1Top = kNumberH;
constexpr float kLane2Top = kLane1Top + kLane1H + kLaneGap;
constexpr float kTypeTop  = kLane2Top + kLane2H + kGap;
constexpr float kBarsTop  = kTypeTop + kTypeH + kGap;

juce::Colour laneColour (int lane) { return lane == 0 ? gn::colours::acid : gn::colours::lane2; }
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
        onStepSelected (lane(), selected);
}

void StepGrid::setLane (int l)
{
    if (l == lane())
        return;
    processor.setActiveLane (l);
    repaint();
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
    if (y < kNumberH) return Row::number;
    if (y < kLane1Top + kLane1H + kLaneGap * 0.5f) return Row::lane1;
    if (y < kLane2Top + kLane2H + kGap * 0.5f) return Row::lane2;
    if (y >= kTypeTop && y < kTypeTop + kTypeH) return Row::type;
    if (y >= h - kRepeatH) return Row::repeat;
    if (y >= kBarsTop && y < h - kRepeatH - kGap) return Row::bars;
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
        case Row::lane1:  return { x, kLane1Top, cw, kLane1H };
        case Row::lane2:  return { x, kLane2Top, cw, kLane2H };
        case Row::type:   return { x, kTypeTop, cw, kTypeH };
        case Row::bars:   return { x, kBarsTop, cw, h - kBarsTop - kRepeatH - kGap };
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

juce::Rectangle<float> StepGrid::spanBounds (int ln, int start, int span) const
{
    return cell (start, padRow (ln)).withRight (cell (start + span - 1, padRow (ln)).getRight());
}

int StepGrid::spanOf (const gn::Steps& steps, int owner, int numSteps)
{
    return std::max (1, std::min (steps[(size_t) owner].length, numSteps - owner));
}

std::array<int, gn::kMaxSteps> StepGrid::coverOwners (const gn::Steps& steps, int numSteps)
{
    std::array<int, gn::kMaxSteps> owner {};
    for (int i = 0; i < gn::kMaxSteps; ++i)
        owner[(size_t) i] = i;

    for (int i = 0; i < numSteps; ++i)
    {
        const int len = spanOf (steps, i, numSteps);
        if (steps[(size_t) i].on && len > 1)
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
    const int ln = laneOf (rowAt (pos.y));
    if (ln < 0)
        return {};

    const auto pat = processor.getPattern (processor.getEditPattern());
    const auto& steps = pat.lanes[(size_t) ln].steps;
    const int n = processor.getNumSteps();
    const auto owner = coverOwners (steps, n);
    for (int i = 0; i < n; ++i)
    {
        if (owner[(size_t) i] != i || ! steps[(size_t) i].on)
            continue;
        const auto b = spanBounds (ln, i, spanOf (steps, i, n));
        if (std::abs (pos.x - b.getX()) <= kEdgeGrab)     return { ln, i, true };
        if (std::abs (pos.x - b.getRight()) <= kEdgeGrab) return { ln, i, false };
    }
    return {};
}

void StepGrid::drawBracket (juce::Graphics& g, juce::Rectangle<float> pad, bool left, juce::Colour c)
{
    const float x = left ? pad.getX() + 4.0f : pad.getRight() - 4.0f;
    const float dir = left ? 1.0f : -1.0f;
    const float top = pad.getY() + 5.0f, bottom = pad.getBottom() - 5.0f;
    juce::Path p;
    p.startNewSubPath (x + dir * 5.0f, top);
    p.lineTo (x, top);
    p.lineTo (x, bottom);
    p.lineTo (x + dir * 5.0f, bottom);
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::square));
}

//==============================================================================
void StepGrid::paintLaneRow (juce::Graphics& g, const gn::Pattern& pattern, int ln, int active, int playing, bool showPlayhead, bool live)
{
    using namespace gn::colours;
    const auto& steps = pattern.lanes[(size_t) ln].steps;
    const auto owner = coverOwners (steps, active);
    const bool editing = ln == lane();
    const int firing = (showPlayhead && ! (live && ln == 0)) ? processor.uiFiringStep[(size_t) ln].load() : -1;
    const int firingLen = processor.uiFiringLen[(size_t) ln].load();
    const auto row = padRow (ln);
    const auto colour = laneColour (ln);

    // The lane being edited gets a frame, so it's obvious which one the rows below show.
    {
        auto band = cell (0, row).withRight (cell (gn::kMaxSteps - 1, row).getRight()).expanded (2.0f, 2.0f);
        g.setColour (colour.withAlpha (editing ? 0.10f : 0.03f));
        g.fillRoundedRectangle (band, 5.0f);
        if (editing)
        {
            g.setColour (colour.withAlpha (0.5f));
            g.drawRoundedRectangle (band, 5.0f, 1.2f);
        }
    }

    for (int i = 0; i < gn::kMaxSteps; ++i)
    {
        if (owner[(size_t) i] != i)
            continue; // drawn as part of the tie

        const auto& s = steps[(size_t) i];
        const float alpha = (i < active ? 1.0f : 0.25f) * (editing ? 1.0f : 0.7f);
        const int span = s.on ? spanOf (steps, i, active) : 1;
        const bool inFiring = firing >= 0 && firing == i;
        auto pad = spanBounds (ln, i, span);

        g.setColour (s.on ? (inFiring ? nasty : colour).withAlpha (alpha) : panel);
        g.fillRoundedRectangle (pad, 4.0f);
        g.setColour (outline.withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (pad, 4.0f, 1.0f);

        if (s.on && i < active)
        {
            const bool hoverHere = hoverEdge.lane == ln && hoverEdge.owner == i;
            const bool hl = hoverHere || (dragLane == ln && dragOwner == i && dragMode != DragMode::none);
            const auto c = background.withAlpha (hl ? 0.95f : 0.55f);
            drawBracket (g, pad, true, hoverHere && hoverEdge.left ? background : c);
            drawBracket (g, pad, false, hoverHere && ! hoverEdge.left ? background : c);

            g.setColour (background.withAlpha (0.75f));
            g.setFont (juce::FontOptions (ln == 0 ? 10.5f : 9.5f, juce::Font::bold));
            juce::String label = gn::kTypeShortNames[juce::jlimit (0, gn::kNumStutterTypes - 1, s.type)];
            if (span > 1) label << "  " << span << " STEPS";
            g.drawText (label, pad.reduced (12.0f, 0.0f), span > 1 ? juce::Justification::centredLeft : juce::Justification::centred);
        }
        else if (i == 0)
        {
            g.setColour (colour.withAlpha (0.45f * alpha));
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.drawText ("LANE " + juce::String (ln + 1), pad, juce::Justification::centred);
        }

        // The playhead's position inside a firing tie.
        if (inFiring && playing >= i && playing < i + firingLen)
        {
            auto tick = cell (playing, row).removeFromBottom (3.0f).reduced (4.0f, 0.0f);
            g.setColour (background.withAlpha (0.8f));
            g.fillRect (tick);
        }
    }
}

void StepGrid::paintBars (juce::Graphics& g, const gn::Pattern& pattern, int active)
{
    using namespace gn::colours;
    const int ln = lane();
    const auto& laneData = pattern.lanes[(size_t) ln];
    const auto& steps = laneData.steps;
    const auto owner = coverOwners (steps, active);
    const int target = processor.getBarsTarget();
    const auto colour = laneColour (ln);

    for (int i = 0; i < gn::kMaxSteps; ++i)
    {
        const float alpha = i < active ? 1.0f : 0.25f;
        const int o = owner[(size_t) i];
        const auto& own = steps[(size_t) o];
        const bool covered = o != i;
        auto bar = cell (i, Row::bars);
        g.setColour (panel);
        g.fillRoundedRectangle (bar, 3.0f);

        if (target < 0) // ---- Chance -----------------------------------------
        {
            const auto& s = steps[(size_t) i];
            const float rowAlpha = covered ? 0.25f * alpha : alpha;
            if (! covered)
            {
                auto fill = bar.withTop (bar.getBottom() - bar.getHeight() * s.chance);
                g.setColour (s.on ? colour.withAlpha (0.55f * rowAlpha) : dimText.withAlpha (0.08f * rowAlpha));
                g.fillRoundedRectangle (fill, 3.0f);
                if (s.chance < 0.995f)
                {
                    g.setColour (text.withAlpha (0.8f * rowAlpha));
                    g.setFont (juce::FontOptions (10.0f));
                    g.drawText (juce::String (juce::roundToInt (s.chance * 100.0f)), bar.withTop (bar.getBottom() - 16.0f),
                                juce::Justification::centred);
                }

                // Small marks for level / pan / filter / crush
                if (s.on)
                {
                    auto marks = bar.withTrimmedTop (14.0f).withHeight (12.0f).reduced (2.0f, 0.0f);
                    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
                    g.setColour (background.withAlpha (0.8f));
                    juce::String m;
                    if (s.filter < -0.001f) m << "LP ";
                    if (s.filter > 0.001f)  m << "HP ";
                    if (s.bits > 0.001f || s.rate > 0.001f) m << "CR ";
                    if (std::abs (s.pan) > 0.01f) m << (s.pan < 0 ? "L" : "R");
                    if (std::abs (s.levelDb) > 0.05f) m << (s.levelDb < 0 ? "-" : "+");
                    if (m.isNotEmpty() && s.chance > 0.3f)
                        g.drawText (m.trim(), marks, juce::Justification::centredTop);
                }
            }
            else
            {
                g.setColour (dimText.withMultipliedAlpha (alpha * 0.6f));
                g.setFont (juce::FontOptions (9.5f));
                g.drawText ("tied", bar, juce::Justification::centred);
            }
        }
        else // ---- Parameter lock ---------------------------------------------
        {
            const auto t = (gn::ModTarget) target;
            const float lock = laneData.mod[(size_t) target][(size_t) i];
            const bool set = gn::isSet (lock);
            const bool playsHere = own.on; // there's a glitch on this column for the lock to change
            const float v = set ? lock : gn::modValueOf (own, t);
            const bool bipolar = gn::fmt::isBipolar (t, own.type);
            const float y = bar.getBottom() - bar.getHeight() * juce::jlimit (0.0f, 1.0f, v);
            const float base = bipolar ? bar.getCentreY() : bar.getBottom();
            auto fill = juce::Rectangle<float> (bar.getX(), std::min (y, base), bar.getWidth(), std::max (1.5f, std::abs (y - base)));

            if (set)
            {
                g.setColour ((playsHere ? colour : dimText).withAlpha (0.7f * alpha));
                g.fillRoundedRectangle (fill, 2.0f);
                g.setColour (text.withAlpha ((playsHere ? 0.9f : 0.45f) * alpha));
                g.fillRect (bar.getX() + 2.0f, y - 1.0f, bar.getWidth() - 4.0f, 2.0f);
            }
            else if (playsHere)
            {
                // The step's own setting, as a ghost: what plays on this column without a lock.
                g.setColour (colour.withAlpha (0.12f * alpha));
                g.fillRoundedRectangle (fill, 2.0f);
                g.setColour (colour.withAlpha (0.4f * alpha));
                g.drawHorizontalLine (juce::roundToInt (y), bar.getX() + 3.0f, bar.getRight() - 3.0f);
            }

            if (bipolar)
            {
                g.setColour (outline);
                g.drawHorizontalLine (juce::roundToInt (bar.getCentreY()), bar.getX(), bar.getRight());
            }

            if (playsHere)
            {
                const double stepPpq = processor.getGridPpq() * (double) own.length;
                const int slices = gn::slicesIn (stepPpq, gn::kDivisionPpq[(size_t) juce::jlimit (0, gn::kNumDivisions - 1, own.division)]);
                g.setColour ((set ? text : dimText).withAlpha (alpha));
                g.setFont (juce::FontOptions (9.5f, set ? juce::Font::bold : juce::Font::plain));
                g.drawFittedText (gn::fmt::modValue (t, own, v, slices), bar.withTop (bar.getBottom() - 16.0f).toNearestInt(),
                                  juce::Justification::centred, 1, 0.8f);
            }
        }

        g.setColour (outline.withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (bar, 3.0f, 1.0f);
    }
}

void StepGrid::paint (juce::Graphics& g)
{
    using namespace gn::colours;
    const int pat = processor.getEditPattern();
    const auto pattern = processor.getPattern (pat);
    const int active = processor.getNumSteps();
    const bool live = processor.uiLiveType.load() >= 0;
    const bool showPlayhead = processor.uiPattern.load() == pat;
    const int playing = showPlayhead ? processor.uiPlayingStep.load() : -1;
    const int ln = lane();
    const auto& steps = pattern.lanes[(size_t) ln].steps;
    const auto owner = coverOwners (steps, active);

    if (selected >= 0)
    {
        const auto sel = columnBounds (selected);
        g.setColour (laneColour (ln).withAlpha (0.06f));
        g.fillRoundedRectangle (sel, 5.0f);
        g.setColour (laneColour (ln).withAlpha (0.45f));
        g.drawRoundedRectangle (sel.reduced (0.5f), 5.0f, 1.2f);
    }

    // Is anything glitching on the playhead's column?
    bool glitching = live;
    for (int l = 0; l < gn::kNumLanes && showPlayhead; ++l)
    {
        const int f = processor.uiFiringStep[(size_t) l].load();
        glitching |= f >= 0 && playing >= f && playing < f + processor.uiFiringLen[(size_t) l].load();
    }

    for (int i = 0; i < gn::kMaxSteps; ++i)
    {
        const float alpha = i < active ? 1.0f : 0.25f;
        auto num = cell (i, Row::number);
        if (i == playing)
        {
            g.setColour ((glitching ? nasty : acid).withAlpha (0.9f));
            g.fillRoundedRectangle (num.reduced (0.0f, 3.0f), 3.0f);
        }
        g.setColour (i == playing ? background : (i % 4 == 0 ? text : dimText).withMultipliedAlpha (alpha));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (juce::String (i + 1), num, juce::Justification::centred);
    }

    for (int l = 0; l < gn::kNumLanes; ++l)
        paintLaneRow (g, pattern, l, active, playing, showPlayhead, live);

    for (int i = 0; i < gn::kMaxSteps; ++i)
    {
        const auto& s = steps[(size_t) i];
        const float alpha = i < active ? 1.0f : 0.25f;
        const bool covered = owner[(size_t) i] != i;
        const float rowAlpha = covered ? 0.25f * alpha : alpha;

        // Type
        auto typeCell = cell (i, Row::type);
        g.setColour (panel);
        g.fillRoundedRectangle (typeCell, 3.0f);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        if (covered)
        {
            g.setColour (dimText.withMultipliedAlpha (alpha));
            g.drawText ("tied", typeCell, juce::Justification::centred);
        }
        else
        {
            g.setColour ((s.on ? (s.type != 0 ? nasty.brighter (0.3f) : text) : dimText).withMultipliedAlpha (rowAlpha));
            g.drawText (gn::kTypeShortNames[juce::jlimit (0, gn::kNumStutterTypes - 1, s.type)], typeCell, juce::Justification::centred);
        }

        // Repeat length
        auto rep = cell (i, Row::repeat);
        g.setColour (panel);
        g.fillRoundedRectangle (rep, 3.0f);
        g.setColour ((s.on ? text : dimText).withMultipliedAlpha (rowAlpha));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (covered ? juce::String ("tied") : juce::String (gn::kDivisionNames[juce::jlimit (0, gn::kNumDivisions - 1, s.division)]),
                    rep, juce::Justification::centred);
    }

    paintBars (g, pattern, active);

    // Beat dividers every 4 steps
    g.setColour (outline);
    const float w = (float) getWidth() / (float) gn::kMaxSteps;
    for (int i = 4; i < gn::kMaxSteps; i += 4)
        g.drawVerticalLine (juce::roundToInt ((float) i * w), kTypeTop, (float) getHeight());
}

//==============================================================================
void StepGrid::mouseMove (const juce::MouseEvent& e)
{
    const auto edge = edgeAt (e.position);
    if (edge.owner != hoverEdge.owner || edge.left != hoverEdge.left || edge.lane != hoverEdge.lane)
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

void StepGrid::setBarFromY (int column, float y)
{
    auto bar = cell (column, Row::bars);
    const float v = juce::jlimit (0.0f, 1.0f, (bar.getBottom() - y) / bar.getHeight());
    const int pat = processor.getEditPattern();
    const int target = processor.getBarsTarget();

    if (target < 0)
    {
        processor.editStep (pat, lane(), column, [v] (gn::StepData& s) { s.chance = std::round (v * 100.0f) * 0.01f; });
        return;
    }

    // Choice controls (Reverse / Ping-Pong ...) snap to their positions; pitch snaps to semitones.
    const auto own = processor.getStep (pat, lane(), gn::ownerOf (processor.getPattern (pat).lanes[(size_t) lane()].steps, column,
                                                                  processor.getNumSteps()));
    float value = v;
    const auto t = (gn::ModTarget) target;
    if (t == gn::ModTarget::ctl1 || t == gn::ModTarget::ctl2)
    {
        const auto& info = gn::kTypeInfo[(size_t) juce::jlimit (0, gn::kNumStutterTypes - 1, own.type)];
        const auto& tc = t == gn::ModTarget::ctl1 ? info.c1 : info.c2;
        if (tc.numChoices > 1)
            value = (float) gn::choiceIndex (v, tc.numChoices) / (float) (tc.numChoices - 1);
        else if ((gn::StutterType) own.type == gn::StutterType::pitch)
            value = 0.5f + std::round ((v - 0.5f) * 48.0f) / 48.0f;
    }
    processor.setMod (pat, lane(), target, column, value);
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    const auto pos = e.position;
    const int col = columnAt (pos.x);
    const int pat = processor.getEditPattern();
    const int n = processor.getNumSteps();
    const auto row = rowAt (pos.y);

    // Grab a [ or ] to change how many steps the glitch lasts.
    const auto edge = edgeAt (pos);
    if (edge.owner >= 0)
    {
        setLane (edge.lane);
        const auto steps = processor.getPattern (pat).lanes[(size_t) edge.lane].steps;
        dragLane = edge.lane;
        dragOwner = edge.owner;
        dragAnchor = edge.left ? edge.owner + spanOf (steps, edge.owner, n) - 1 : edge.owner;
        dragMode = edge.left ? DragMode::resizeLeft : DragMode::resizeRight;
        select (edge.owner);
        return;
    }

    if (const int ln = laneOf (row); ln >= 0)
    {
        setLane (ln);
        const auto steps = processor.getPattern (pat).lanes[(size_t) ln].steps;
        select (col);
        if (gn::ownerOf (steps, col, n) != col)
        {
            dragMode = DragMode::none; // inside a tie: just select it (drag its edges to change it)
            return;
        }
        paintValue = ! steps[(size_t) col].on;
        dragMode = DragMode::paintPads;
        dragLane = ln;
        processor.editStep (pat, ln, col, [this] (gn::StepData& s) { s.on = paintValue; });
        return;
    }

    select (col);

    switch (row)
    {
        case Row::bars:
            if (e.mods.isAltDown() && processor.getBarsTarget() >= 0)
            {
                processor.setMod (pat, lane(), processor.getBarsTarget(), col, gn::kModUnset);
                return;
            }
            dragMode = DragMode::drawBars;
            setBarFromY (col, pos.y);
            break;
        case Row::type:
        case Row::repeat:
            showChoiceMenu (col, row);
            break;
        case Row::number:
        case Row::lane1:
        case Row::lane2:
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
        const auto steps = processor.getPattern (pat).lanes[(size_t) dragLane].steps;
        const int start = dragOwner, end = dragOwner + spanOf (steps, dragOwner, n) - 1;
        int newStart = start, newEnd = end;
        if (dragMode == DragMode::resizeRight) newEnd = std::max (dragAnchor, c);
        else                                   newStart = std::min (dragAnchor, c);

        if (newStart != start || newEnd != end)
        {
            processor.setStepSpan (pat, dragLane, start, newStart, newEnd);
            dragOwner = newStart;
            select (newStart);
        }
        return;
    }

    if (dragMode == DragMode::paintPads)
    {
        const auto steps = processor.getPattern (pat).lanes[(size_t) dragLane].steps;
        if (gn::ownerOf (steps, col, n) == col)
            processor.editStep (pat, dragLane, col, [this] (gn::StepData& s) { s.on = paintValue; });
        return;
    }

    // Bars: Chance only on steps that start something; locks anywhere (that's the point).
    if (processor.getBarsTarget() < 0
        && gn::ownerOf (processor.getPattern (pat).lanes[(size_t) lane()].steps, col, n) != col)
        return;
    setBarFromY (col, e.position.y);
}

void StepGrid::mouseUp (const juce::MouseEvent& e)
{
    dragMode = DragMode::none;
    dragOwner = dragAnchor = -1;
    mouseMove (e);
}

void StepGrid::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (rowAt (e.position.y) != Row::bars)
        return;
    const int col = columnAt (e.position.x);
    if (processor.getBarsTarget() < 0)
        processor.editStep (processor.getEditPattern(), lane(), col, [] (gn::StepData& s) { s.chance = 1.0f; });
    else
        processor.setMod (processor.getEditPattern(), lane(), processor.getBarsTarget(), col, gn::kModUnset);
}

void StepGrid::showChoiceMenu (int column, Row row)
{
    const bool isType = (row == Row::type);
    const int pat = processor.getEditPattern();
    const int ln = lane();
    const int o = gn::ownerOf (processor.getPattern (pat).lanes[(size_t) ln].steps, column, processor.getNumSteps());
    const auto step = processor.getStep (pat, ln, o);
    const int current = isType ? step.type : step.division;

    juce::PopupMenu menu;
    menu.addSectionHeader ("Lane " + juce::String (ln + 1) + ", step " + juce::String (o + 1) + (isType ? " type" : " repeat"));
    const int count = isType ? gn::kNumStutterTypes : gn::kNumDivisions;
    for (int i = 0; i < count; ++i)
        menu.addItem (i + 1, isType ? gn::kTypeNames[i] : gn::kDivisionNames[i], true, i == current);

    juce::Component::SafePointer<StepGrid> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                  .withTargetScreenArea (localAreaToGlobal (cell (column, row).toNearestInt())),
                        [safe, isType, pat, ln, o, column] (int result)
                        {
                            if (safe == nullptr || result <= 0)
                                return;
                            const int v = result - 1;
                            safe->processor.editStep (pat, ln, o, [isType, v] (gn::StepData& s)
                            {
                                if (isType) { if (s.type != v) gn::setType (s, v); }
                                else s.division = v;
                            });
                            if (safe->onStepSelected)
                                safe->onStepSelected (ln, column);
                        });
}

juce::String StepGrid::getTooltip()
{
    const auto pos = getMouseXYRelative().toFloat();
    if (edgeAt (pos).owner >= 0)
        return "Drag to make this glitch last more or fewer steps.";

    const int target = processor.getBarsTarget();
    switch (rowAt (pos.y))
    {
        case Row::number: return "Click to edit this step's controls below. Lights green as the playhead passes, pink while it's glitching.";
        case Row::lane1:  return "Lane 1: click a step on or off, drag across to paint. Drag the [ ] edges of an on step to stretch it. "
                                 "Click anywhere on this row to edit lane 1 below.";
        case Row::lane2:  return "Lane 2: a second sequence that glitches lane 1's output - e.g. a Roll here over a stretched Pitch on lane 1 "
                                 "rolls the pitched sound. Steps stretch the same way. Click anywhere on this row to edit lane 2 below.";
        case Row::type:
        {
            const int pat = processor.getEditPattern();
            const int o = gn::ownerOf (processor.getPattern (pat).lanes[(size_t) lane()].steps, columnAt (pos.x), processor.getNumSteps());
            const auto s = processor.getStep (pat, lane(), o);
            return juce::String (gn::kTypeNames[s.type]) + ": " + gn::kTypeInfo[(size_t) s.type].help + "  (Click to change.)";
        }
        case Row::bars:
            if (target < 0)
                return "Chance this step glitches each time it comes around. Drag up/down; drag sideways to draw across steps. Double-click = 100%. "
                       "Use BARS above to draw parameter locks instead.";
            return juce::String (gn::kModTargetNames[target]) + " locks: drag to set this parameter step by step - it changes the glitch that's "
                   "playing on that step, even in the middle of a stretched one. The faint line is the step's own setting. "
                   "Double-click (or Alt-click) to clear a lock.";
        case Row::repeat: return "Repeat length: the slice that loops (for Gate, the on/off cycle; for Tape Stop, how long it takes). Click to choose.";
        case Row::none:   break;
    }
    return {};
}
