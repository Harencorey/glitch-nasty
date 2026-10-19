#include "StepGrid.h"
#include "Format.h"

namespace
{
constexpr float kGutter    = 50.0f; // lane labels and + / - on the left
constexpr float kNumberH   = 20.0f;
constexpr float kLaneGap   = 3.0f;
constexpr float kRepeatH   = 22.0f;
constexpr float kGap       = 6.0f;
constexpr float kColumnPad = 3.0f;
constexpr float kEdgeGrab  = 9.0f; // how close to a pad edge counts as grabbing it

using gn::colours::laneColour;
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
    if (onStepSelected)
        onStepSelected (lane(), juce::jmax (0, selected));
    repaint();
}

//==============================================================================
int StepGrid::numLanes() const { return processor.getNumLanes (processor.getEditPattern()); }

float StepGrid::laneTop (int l) const noexcept
{
    float y = kNumberH;
    for (int i = 0; i < l; ++i)
        y += laneHeight (i) + kLaneGap;
    return y;
}

float StepGrid::typeTop() const noexcept
{
    const int n = numLanes();
    return laneTop (n - 1) + laneHeight (n - 1) + kGap;
}

float StepGrid::columnWidth() const noexcept
{
    return ((float) getWidth() - kGutter) / (float) gn::kMaxSteps;
}

int StepGrid::columnAt (float x) const
{
    return juce::jlimit (0, gn::kMaxSteps - 1, (int) std::floor ((x - kGutter) / columnWidth()));
}

StepGrid::Hit StepGrid::hitAt (juce::Point<float> pos) const
{
    if (pos.x < kGutter)
        return {};
    const float y = pos.y, h = (float) getHeight();
    if (y < kNumberH) return { Row::number, -1 };
    const int n = numLanes();
    for (int l = 0; l < n; ++l)
        if (y < laneTop (l) + laneHeight (l) + (l == n - 1 ? kGap : kLaneGap) * 0.5f)
            return { Row::lane, l };
    if (y >= typeTop() && y < typeTop() + 22.0f) return { Row::type, -1 };
    if (y >= h - kRepeatH) return { Row::repeat, -1 };
    if (y >= barsTop() && y < h - kRepeatH - kGap) return { Row::bars, -1 };
    return {};
}

juce::Rectangle<float> StepGrid::plusBounds() const
{
    return { 3.0f, typeTop(), (kGutter - 10.0f) * 0.5f, 22.0f };
}

juce::Rectangle<float> StepGrid::minusBounds() const
{
    return plusBounds().translated (plusBounds().getWidth() + 4.0f, 0.0f);
}

StepGrid::GutterHit StepGrid::gutterAt (juce::Point<float> pos, int& laneOut) const
{
    laneOut = -1;
    if (pos.x >= kGutter)
        return GutterHit::none;
    if (plusBounds().expanded (1.0f).contains (pos))  return GutterHit::plus;
    if (minusBounds().expanded (1.0f).contains (pos)) return GutterHit::minus;
    for (int l = 0; l < numLanes(); ++l)
        if (pos.y >= laneTop (l) && pos.y < laneTop (l) + laneHeight (l))
        {
            laneOut = l;
            return GutterHit::laneLabel;
        }
    return GutterHit::none;
}

juce::Rectangle<float> StepGrid::cell (int column, Row row, int ln) const
{
    const float w = columnWidth();
    const float x = kGutter + (float) column * w + kColumnPad;
    const float cw = w - 2.0f * kColumnPad;
    const float h = (float) getHeight();

    switch (row)
    {
        case Row::number: return { x, 0.0f, cw, kNumberH };
        case Row::lane:   return { x, laneTop (ln), cw, laneHeight (ln) };
        case Row::type:   return { x, typeTop(), cw, 22.0f };
        case Row::bars:   return { x, barsTop(), cw, h - barsTop() - kRepeatH - kGap };
        case Row::repeat: return { x, h - kRepeatH, cw, kRepeatH };
        case Row::none:   break;
    }
    return {};
}

juce::Rectangle<float> StepGrid::columnBounds (int column) const
{
    const float w = columnWidth();
    return { kGutter + (float) column * w + 1.0f, 0.0f, w - 2.0f, (float) getHeight() };
}

juce::Rectangle<float> StepGrid::spanBounds (int ln, int start, int span) const
{
    return cell (start, Row::lane, ln).withRight (cell (start + span - 1, Row::lane, ln).getRight());
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
    const auto hit = hitAt (pos);
    if (hit.row != Row::lane)
        return {};

    const int ln = hit.lane;
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
void StepGrid::paintGutter (juce::Graphics& g, int lanes)
{
    using namespace gn::colours;
    for (int l = 0; l < lanes; ++l)
    {
        auto r = juce::Rectangle<float> (3.0f, laneTop (l), kGutter - 8.0f, laneHeight (l));
        const bool editing = l == lane();
        g.setColour (laneColour (l).withAlpha (editing ? 0.9f : 0.15f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (editing ? background : laneColour (l));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("L" + juce::String (l + 1), r, juce::Justification::centred);
    }

    auto button = [&g] (juce::Rectangle<float> r, const juce::String& t, bool enabled)
    {
        g.setColour (raised.withAlpha (enabled ? 1.0f : 0.4f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (outline);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
        g.setColour ((enabled ? text : dimText).withAlpha (enabled ? 1.0f : 0.5f));
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (t, r, juce::Justification::centred);
    };
    button (plusBounds(), "+", lanes < gn::kMaxLanes);
    button (minusBounds(), "-", lanes > 1);
}

void StepGrid::paintLaneRow (juce::Graphics& g, const gn::Pattern& pattern, int ln, int active, int playing, bool showPlayhead)
{
    using namespace gn::colours;
    const auto& steps = pattern.lanes[(size_t) ln].steps;
    const auto owner = coverOwners (steps, active);
    const bool editing = ln == lane();
    const bool live = processor.uiLiveMask.load() != 0;
    const int firing = (showPlayhead && ! live) ? processor.uiFiringStep[(size_t) ln].load() : -1;
    const int firingLen = processor.uiFiringLen[(size_t) ln].load();
    const auto colour = laneColour (ln);

    // The lane being edited gets a frame, so it's obvious which one the rows below show.
    {
        auto band = cell (0, Row::lane, ln).withRight (cell (gn::kMaxSteps - 1, Row::lane, ln).getRight()).expanded (2.0f, 1.5f);
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

        // The playhead's position inside a firing tie.
        if (inFiring && playing >= i && playing < i + firingLen)
        {
            auto tick = cell (playing, Row::lane, ln).removeFromBottom (3.0f).reduced (4.0f, 0.0f);
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
    const int lanes = juce::jlimit (1, gn::kMaxLanes, pattern.numLanes);
    const int active = processor.getNumSteps();
    const bool live = processor.uiLiveMask.load() != 0;
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

    paintGutter (g, lanes);

    // Is anything glitching on the playhead's column?
    bool glitching = live;
    for (int l = 0; l < lanes && showPlayhead; ++l)
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

    for (int l = 0; l < lanes; ++l)
        paintLaneRow (g, pattern, l, active, playing, showPlayhead);

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
    const float w = columnWidth();
    for (int i = 4; i < gn::kMaxSteps; i += 4)
        g.drawVerticalLine (juce::roundToInt (kGutter + (float) i * w), typeTop(), (float) getHeight());
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
    int gl = -1;
    const bool clicky = gutterAt (e.position, gl) != GutterHit::none;
    setMouseCursor (edge.owner >= 0 ? juce::MouseCursor::LeftRightResizeCursor
                                    : (clicky ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor));
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

void StepGrid::removeLaneAsked()
{
    const int pat = processor.getEditPattern();
    const int l = lane();
    if (processor.getNumLanes (pat) <= 1)
        return;

    const auto lanePat = processor.getPattern (pat).lanes[(size_t) l];
    bool used = false;
    for (const auto& s : lanePat.steps) used |= s.on;
    for (const auto& row : lanePat.mod)
        for (float v : row) used |= gn::isSet (v);

    if (! used)
    {
        processor.removeLane (pat, l);
        setLane (processor.getActiveLane());
        return;
    }

    juce::Component::SafePointer<StepGrid> safe (this);
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                      .withTitle ("Remove lane " + juce::String (l + 1) + "?")
                                      .withMessage ("Its steps and locks in pattern " + juce::String (gn::kPatternNames[(size_t) pat])
                                                    + " will be deleted.")
                                      .withButton ("Remove")
                                      .withButton ("Cancel")
                                      .withAssociatedComponent (this),
                                  [safe, pat, l] (int result)
                                  {
                                      if (safe == nullptr || result != 1)
                                          return;
                                      safe->processor.removeLane (pat, l);
                                      if (safe->onStepSelected)
                                          safe->onStepSelected (safe->lane(), juce::jmax (0, safe->selected));
                                      safe->repaint();
                                  });
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    const auto pos = e.position;
    const int pat = processor.getEditPattern();
    const int n = processor.getNumSteps();

    // Gutter: pick a lane, add one, remove the one being edited.
    int gl = -1;
    switch (gutterAt (pos, gl))
    {
        case GutterHit::laneLabel: setLane (gl); return;
        case GutterHit::plus:
            processor.addLane (pat);
            if (onStepSelected) onStepSelected (lane(), juce::jmax (0, selected));
            repaint();
            return;
        case GutterHit::minus: removeLaneAsked(); return;
        case GutterHit::none: break;
    }

    const int col = columnAt (pos.x);
    const auto hit = hitAt (pos);

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

    if (hit.row == Row::lane)
    {
        const int ln = hit.lane;
        setLane (ln);
        const auto steps = processor.getPattern (pat).lanes[(size_t) ln].steps;
        const int o = gn::ownerOf (steps, col, n);
        dragMode = DragMode::none;
        dragLane = ln;

        if (o != col || steps[(size_t) col].on)
        {
            // An on step (or inside its tie): a click only selects it. A double-click switches it
            // off - unless this same double-click is what just switched it on.
            select (col);
            const bool switchedOnByThisClick = e.getNumberOfClicks() >= 2 && justSwitchedOnLane == ln
                                               && (justSwitchedOnCol == o || justSwitchedOnCol == col);
            if (e.getNumberOfClicks() >= 2 && ! switchedOnByThisClick)
                processor.editStep (pat, ln, o, [] (gn::StepData& s) { s.on = false; });
            if (e.getNumberOfClicks() < 2)
                justSwitchedOnLane = justSwitchedOnCol = -1;
            return;
        }

        // An empty pad: switch it on (and keep painting on while dragging).
        processor.editStep (pat, ln, col, [] (gn::StepData& s) { s.on = true; });
        justSwitchedOnLane = ln;
        justSwitchedOnCol = col;
        dragMode = DragMode::paintPads;
        select (col);
        return;
    }

    if (hit.row == Row::none && pos.x < kGutter)
        return;

    select (col);

    switch (hit.row)
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
            showChoiceMenu (col, hit.row);
            break;
        case Row::number:
        case Row::lane:
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
        // Painting only ever switches steps on; switching off is a deliberate double-click.
        const auto steps = processor.getPattern (pat).lanes[(size_t) dragLane].steps;
        if (gn::ownerOf (steps, col, n) == col && ! steps[(size_t) col].on)
            processor.editStep (pat, dragLane, col, [] (gn::StepData& s) { s.on = true; });
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
    // (Pads handle their double-click in mouseDown, where the click count is known.)
    if (hitAt (e.position).row != Row::bars)
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
    int gl = -1;
    switch (gutterAt (pos, gl))
    {
        case GutterHit::laneLabel: return "Lane " + juce::String (gl + 1) + ": click to edit it below.";
        case GutterHit::plus:      return "Add a lane (up to 4). Each lane glitches the output of the lanes above it, so they stack.";
        case GutterHit::minus:     return "Remove the lane you're editing (the framed one). Asks first if it has steps or locks.";
        case GutterHit::none:      break;
    }
    if (edgeAt (pos).owner >= 0)
        return "Drag to make this glitch last more or fewer steps.";

    const int target = processor.getBarsTarget();
    const auto hit = hitAt (pos);
    switch (hit.row)
    {
        case Row::number: return "Click to edit this step's controls below. Lights green as the playhead passes, pink while it's glitching.";
        case Row::lane:
            return "Lane " + juce::String (hit.lane + 1) + (hit.lane == 0 ? " (the main sequence)" : " (glitches the output of the lanes above)")
                   + ". Click an empty step to switch it on (drag to paint more). Click an on step to select it; "
                     "double-click it to switch it off. Drag the [ ] edges to stretch a step.";
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
