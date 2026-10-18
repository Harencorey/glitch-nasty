#include "StepGrid.h"

namespace
{
constexpr float kNumberH = 20.0f;
constexpr float kPadH    = 46.0f;
constexpr float kTypeH   = 22.0f;
constexpr float kRepeatH = 24.0f;
constexpr float kGap     = 6.0f;
constexpr float kColumnPad = 3.0f;
} // namespace

StepGrid::StepGrid (GlitchNastyProcessor& p) : processor (p)
{
    for (int i = 0; i < gn::kMaxSteps; ++i)
    {
        auto& s = steps[(size_t) i];
        s.on     = processor.apvts.getParameter (gn::ids::stepOn (i));
        s.chance = processor.apvts.getParameter (gn::ids::stepProb (i));
        s.repeat = processor.apvts.getParameter (gn::ids::stepDiv (i));
        s.type   = processor.apvts.getParameter (gn::ids::stepType (i));
    }
    stepsParam = processor.apvts.getParameter (gn::ids::steps);

    startTimerHz (30);
}

StepGrid::~StepGrid()
{
    releaseAllGestures();
}

//==============================================================================
int StepGrid::numActiveSteps() const
{
    return juce::roundToInt (stepsParam->convertFrom0to1 (stepsParam->getValue()));
}

int StepGrid::columnAt (float x) const
{
    const float w = (float) getWidth() / (float) gn::kMaxSteps;
    return juce::jlimit (0, gn::kMaxSteps - 1, (int) std::floor (x / w));
}

StepGrid::Row StepGrid::rowAt (float y) const
{
    const float h = (float) getHeight();
    if (y < kNumberH) return Row::number;
    if (y < kNumberH + kPadH) return Row::pad;
    const float typeTop = kNumberH + kPadH + kGap;
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

//==============================================================================
void StepGrid::paint (juce::Graphics& g)
{
    using namespace gn::colours;
    const int active = numActiveSteps();
    const int playing = processor.uiPlayingStep.load();
    const int firing = processor.uiFiringStep.load();

    for (int i = 0; i < gn::kMaxSteps; ++i)
    {
        const auto& s = steps[(size_t) i];
        const bool inPattern = i < active;
        const bool on = s.on->getValue() > 0.5f;
        const float chance = s.chance->getValue(); // 0..1
        const int div = juce::roundToInt (s.repeat->convertFrom0to1 (s.repeat->getValue()));
        const float alpha = inPattern ? 1.0f : 0.25f;

        // Step number / playhead
        auto num = cell (i, Row::number);
        const bool isPlaying = (i == playing);
        if (isPlaying)
        {
            g.setColour ((i == firing ? nasty : acid).withAlpha (0.9f));
            g.fillRoundedRectangle (num.reduced (0.0f, 3.0f), 3.0f);
        }
        g.setColour (isPlaying ? background : (i % 4 == 0 ? text : dimText).withMultipliedAlpha (alpha));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (juce::String (i + 1), num, juce::Justification::centred);

        // Pad
        auto pad = cell (i, Row::pad);
        g.setColour (on ? (i == firing ? nasty : acid).withAlpha (alpha) : panel);
        g.fillRoundedRectangle (pad, 4.0f);
        g.setColour (outline.withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (pad, 4.0f, 1.0f);

        // Stutter type
        const int type = juce::roundToInt (s.type->convertFrom0to1 (s.type->getValue()));
        auto typeCell = cell (i, Row::type);
        g.setColour (panel);
        g.fillRoundedRectangle (typeCell, 3.0f);
        g.setColour ((on ? (type == 0 ? text : nasty.brighter (0.3f)) : dimText).withMultipliedAlpha (alpha));
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (gn::kTypeShortNames[juce::jlimit (0, gn::kNumStutterTypes - 1, type)], typeCell, juce::Justification::centred);

        // Chance bar
        auto bar = cell (i, Row::chance);
        g.setColour (panel);
        g.fillRoundedRectangle (bar, 3.0f);
        auto fill = bar.withTop (bar.getBottom() - bar.getHeight() * chance);
        g.setColour (on ? acid.withAlpha (0.55f * alpha) : dimText.withAlpha (0.08f * alpha));
        g.fillRoundedRectangle (fill, 3.0f);
        g.setColour (outline.withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (bar, 3.0f, 1.0f);
        if (chance < 0.995f)
        {
            g.setColour (text.withAlpha (0.8f * alpha));
            g.setFont (juce::FontOptions (10.0f));
            g.drawText (juce::String (juce::roundToInt (chance * 100.0f)), bar.removeFromBottom (16.0f), juce::Justification::centred);
        }

        // Repeat length
        auto rep = cell (i, Row::repeat);
        g.setColour (panel);
        g.fillRoundedRectangle (rep, 3.0f);
        g.setColour ((on ? text : dimText).withMultipliedAlpha (alpha));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (gn::kDivisionNames[juce::jlimit (0, gn::kNumDivisions - 1, div)], rep, juce::Justification::centred);
    }

    // Beat dividers every 4 steps
    g.setColour (outline);
    const float w = (float) getWidth() / (float) gn::kMaxSteps;
    for (int i = 4; i < gn::kMaxSteps; i += 4)
        g.drawVerticalLine (juce::roundToInt ((float) i * w), kNumberH, (float) getHeight());
}

void StepGrid::timerCallback()
{
    repaint(); // cheap at this size; also picks up automation, undo and the playhead
}

//==============================================================================
void StepGrid::setParam (juce::RangedAudioParameter* p, float plainValue)
{
    const float norm = p->convertTo0to1 (plainValue);
    if (std::abs (p->getValue() - norm) > 1.0e-6f)
        p->setValueNotifyingHost (norm);
}

void StepGrid::touch (int column, juce::RangedAudioParameter* p)
{
    // One host "gesture" per parameter per drag, so automation and undo see a single move.
    auto& slot = touched[(size_t) column];
    if (slot != p)
    {
        if (slot != nullptr) slot->endChangeGesture();
        p->beginChangeGesture();
        slot = p;
    }
}

void StepGrid::releaseAllGestures()
{
    for (auto& t : touched)
    {
        if (t != nullptr) t->endChangeGesture();
        t = nullptr;
    }
}

void StepGrid::setChanceFromY (int column, float y)
{
    auto bar = cell (column, Row::chance);
    const float v = juce::jlimit (0.0f, 1.0f, (bar.getBottom() - y) / bar.getHeight());
    auto* p = steps[(size_t) column].chance;
    touch (column, p);
    setParam (p, std::round (v * 100.0f));
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    const auto pos = e.position;
    const int col = columnAt (pos.x);
    processor.undoManager.beginNewTransaction();

    switch (rowAt (pos.y))
    {
        case Row::pad:
        {
            auto* p = steps[(size_t) col].on;
            paintValue = p->getValue() < 0.5f;
            dragMode = DragMode::paintPads;
            touch (col, p);
            setParam (p, paintValue ? 1.0f : 0.0f);
            break;
        }
        case Row::chance:
            dragMode = DragMode::drawChance;
            setChanceFromY (col, pos.y);
            break;
        case Row::repeat:
        case Row::type:
            showChoiceMenu (col, rowAt (pos.y));
            break;
        case Row::number:
        case Row::none:
            break;
    }
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    const int col = columnAt (e.position.x);
    if (dragMode == DragMode::paintPads)
    {
        auto* p = steps[(size_t) col].on;
        touch (col, p);
        setParam (p, paintValue ? 1.0f : 0.0f);
    }
    else if (dragMode == DragMode::drawChance)
    {
        setChanceFromY (col, e.position.y);
    }
}

void StepGrid::mouseUp (const juce::MouseEvent&)
{
    dragMode = DragMode::none;
    releaseAllGestures();
}

void StepGrid::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (rowAt (e.position.y) != Row::chance)
        return;

    const int col = columnAt (e.position.x);
    auto* p = steps[(size_t) col].chance;
    p->beginChangeGesture();
    setParam (p, 100.0f);
    p->endChangeGesture();
}

void StepGrid::showChoiceMenu (int column, Row row)
{
    const bool isType = (row == Row::type);
    auto* p = isType ? steps[(size_t) column].type : steps[(size_t) column].repeat;
    const int current = juce::roundToInt (p->convertFrom0to1 (p->getValue()));

    juce::PopupMenu menu;
    menu.addSectionHeader ("Step " + juce::String (column + 1) + (isType ? " type" : " repeat"));
    const int count = isType ? gn::kNumStutterTypes : gn::kNumDivisions;
    for (int i = 0; i < count; ++i)
        menu.addItem (i + 1, isType ? gn::kTypeNames[i] : gn::kDivisionNames[i], true, i == current);

    juce::Component::SafePointer<StepGrid> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                  .withTargetScreenArea (localAreaToGlobal (cell (column, row).toNearestInt())),
                        [safe, p] (int result)
                        {
                            if (safe == nullptr || result <= 0)
                                return;
                            p->beginChangeGesture();
                            p->setValueNotifyingHost (p->convertTo0to1 ((float) (result - 1)));
                            p->endChangeGesture();
                        });
}

juce::String StepGrid::getTooltip()
{
    const auto pos = getMouseXYRelative().toFloat();
    switch (rowAt (pos.y))
    {
        case Row::number: return "Step number. Lights green as the playhead passes, pink while that step is glitching.";
        case Row::pad:    return "Click to switch the step on or off. Click and drag across steps to paint several at once.";
        case Row::type:   return "What the glitch does. Repeat: loops the slice. Reverse: plays it backwards. Gate: chops the live signal on/off. "
                                 "Roll: each repeat half as long (build-up). Pitch +12/-12: repeats an octave up/down. "
                                 "Tape Stop: slows to a halt over the repeat length. Fade: each repeat quieter.";
        case Row::chance: return "Chance this step glitches each time it comes around. Drag up/down; drag sideways to draw across steps. Double-click = 100%.";
        case Row::repeat: return "Repeat length: the slice that loops for the rest of the step (Gate: the on/off cycle; Tape Stop: how long it takes to stop). Click to choose.";
        case Row::none:   break;
    }
    return {};
}
