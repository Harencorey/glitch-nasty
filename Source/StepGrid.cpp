#include "StepGrid.h"

namespace
{
constexpr float kNumberH = 20.0f;
constexpr float kPadH    = 40.0f;
constexpr float kTypeH   = 22.0f;
constexpr float kRepeatH = 22.0f;
constexpr float kGap     = 5.0f;
constexpr float kColumnPad = 3.0f;
} // namespace

StepGrid::StepGrid (GlitchNastyProcessor& p) : processor (p)
{
    startTimerHz (30);
}

void StepGrid::setSelectedStep (int step)
{
    selected = juce::jlimit (0, gn::kMaxSteps - 1, step);
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

std::array<int, gn::kMaxSteps> StepGrid::coverOwners (const gn::Pattern& pat, int numSteps)
{
    std::array<int, gn::kMaxSteps> owner {};
    for (int i = 0; i < gn::kMaxSteps; ++i)
        owner[(size_t) i] = i;

    for (int i = 0; i < numSteps; ++i)
    {
        const auto& s = pat[(size_t) i];
        const int len = std::min (s.length, numSteps - i);
        if (s.on && len > 1)
        {
            for (int j = i + 1; j < i + len; ++j)
                owner[(size_t) j] = i;
            i += len - 1;
        }
    }
    return owner;
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
    const auto owner = coverOwners (pattern, active);

    // Selected column
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
        const bool inFiring = firing >= 0 && i >= firing && i < firing + firingLen;
        const bool nonDefault = s.type != 0;

        // Step number / playhead
        auto num = cell (i, Row::number);
        const bool isPlaying = (i == playing);
        if (isPlaying)
        {
            g.setColour ((inFiring ? nasty : acid).withAlpha (0.9f));
            g.fillRoundedRectangle (num.reduced (0.0f, 3.0f), 3.0f);
        }
        g.setColour (isPlaying ? background : (i % 4 == 0 ? text : dimText).withMultipliedAlpha (alpha));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (juce::String (i + 1), num, juce::Justification::centred);

        // Pad (a tied step draws one long pad across its span)
        if (! covered)
        {
            auto pad = cell (i, Row::pad);
            int span = 1;
            if (s.on)
            {
                span = std::max (1, std::min (s.length, active - i));
                pad = pad.withRight (cell (i + span - 1, Row::pad).getRight());
            }
            const bool lit = s.on && inFiring;
            g.setColour (s.on ? (lit ? nasty : acid).withAlpha (alpha) : panel);
            g.fillRoundedRectangle (pad, 4.0f);
            if (s.on && span > 1)
            {
                g.setColour (background.withAlpha (0.55f));
                g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
                g.drawText ("TIE x" + juce::String (span), pad.reduced (6.0f, 0.0f), juce::Justification::centredRight);
            }
            g.setColour (outline.withMultipliedAlpha (alpha));
            g.drawRoundedRectangle (pad, 4.0f, 1.0f);
        }

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
            g.setColour ((s.on ? (nonDefault ? nasty.brighter (0.3f) : text) : dimText).withMultipliedAlpha (rowAlpha));
            g.drawText (gn::kTypeShortNames[juce::jlimit (0, gn::kNumStutterTypes - 1, s.type)], typeCell, juce::Justification::centred);
        }

        // Chance bar
        auto bar = cell (i, Row::chance);
        g.setColour (panel);
        g.fillRoundedRectangle (bar, 3.0f);
        auto fill = bar.withTop (bar.getBottom() - bar.getHeight() * s.chance);
        g.setColour (s.on ? acid.withAlpha (0.55f * rowAlpha) : dimText.withAlpha (0.08f * rowAlpha));
        g.fillRoundedRectangle (fill, 3.0f);
        g.setColour (outline.withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (bar, 3.0f, 1.0f);
        if (s.chance < 0.995f && ! covered)
        {
            g.setColour (text.withAlpha (0.8f * rowAlpha));
            g.setFont (juce::FontOptions (10.0f));
            g.drawText (juce::String (juce::roundToInt (s.chance * 100.0f)), bar.removeFromBottom (16.0f), juce::Justification::centred);
        }

        // Small marks for level / pan / filter so you can read the shaping at a glance
        if (s.on && ! covered)
        {
            auto marks = cell (i, Row::chance).removeFromTop (12.0f).reduced (3.0f, 2.0f);
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.setColour (background.withAlpha (0.8f));
            juce::String m;
            if (s.filter < -0.001f) m << "LP ";
            if (s.filter > 0.001f)  m << "HP ";
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
    const auto owner = coverOwners (processor.getPattern (pat), processor.getNumSteps());
    const bool covered = owner[(size_t) col] != col;
    const auto row = rowAt (pos.y);

    select (covered ? owner[(size_t) col] : col);
    if (covered)
        return; // tied steps belong to the step that covers them

    switch (row)
    {
        case Row::pad:
            paintValue = ! processor.getStep (pat, col).on;
            dragMode = DragMode::paintPads;
            processor.editStep (pat, col, [this] (gn::StepData& s) { s.on = paintValue; });
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
    const auto owner = coverOwners (processor.getPattern (pat), processor.getNumSteps());
    if (owner[(size_t) col] != col)
        return;

    if (dragMode == DragMode::paintPads)
        processor.editStep (pat, col, [this] (gn::StepData& s) { s.on = paintValue; });
    else
        setChanceFromY (col, e.position.y);
}

void StepGrid::mouseUp (const juce::MouseEvent&)
{
    dragMode = DragMode::none;
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
    switch (rowAt (pos.y))
    {
        case Row::number: return "Click to edit this step's controls below. Lights green as the playhead passes, pink while it's glitching.";
        case Row::pad:    return "Click to switch the step on or off; drag across steps to paint. Set Length in the step editor to tie a glitch across several steps.";
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
