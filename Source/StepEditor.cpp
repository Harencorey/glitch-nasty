#include "StepEditor.h"

namespace
{
juce::String signedValue (double v, int decimals, const juce::String& unit)
{
    const auto s = juce::String (std::abs (v), decimals);
    return (v > 0.0005 ? "+" : (v < -0.0005 ? "-" : "")) + s + unit;
}

juce::String percent (double v) { return juce::String (juce::roundToInt (v * 100.0)) + " %"; }

juce::String hz (double f)
{
    return f >= 1000.0 ? juce::String (f / 1000.0, f >= 10000.0 ? 0 : 1) + "k" : juce::String (juce::roundToInt (f));
}
} // namespace

StepEditor::StepEditor (GlitchNastyProcessor& p) : processor (p)
{
    using namespace gn::colours;

    knobs[kCtl1].setup (*this, "CTL 1", {});
    knobs[kCtl2].setup (*this, "CTL 2", {});
    knobs[kLength].setup (*this, "LENGTH", "How many steps this glitch lasts. Above 1, it ties over the following steps (they're skipped).");
    knobs[kLevel].setup (*this, "LEVEL", "Level of this step's glitch. All the way down = silent (a mute step).");
    knobs[kPan].setup (*this, "PAN", "Where this step's glitch sits in the stereo field.");
    knobs[kFilter].setup (*this, "FILTER", "Left = low-pass (down to 200 Hz), right = high-pass (up to 4 kHz), centre = off. Only on the glitch, never the dry signal.");

    for (int k : { kCtl1, kCtl2 })
        knobs[(size_t) k].slider.setColour (juce::Slider::rotarySliderFillColourId, nasty);

    auto& len = knobs[kLength].slider;
    len.setRange (1.0, gn::kMaxSteps, 1.0);
    len.setDoubleClickReturnValue (true, 1.0);
    len.textFromValueFunction = [] (double v) { const int n = juce::roundToInt (v); return juce::String (n) + (n == 1 ? " step" : " steps"); };

    auto& lvl = knobs[kLevel].slider;
    lvl.setRange (gn::kLevelMinDb, gn::kLevelMaxDb, 0.1);
    lvl.setSkewFactorFromMidPoint (-6.0);
    lvl.setDoubleClickReturnValue (true, 0.0);
    lvl.textFromValueFunction = [] (double v) { return v <= gn::kLevelMinDb + 0.05 ? juce::String ("Off") : signedValue (v, 1, " dB"); };

    auto& pan = knobs[kPan].slider;
    pan.setRange (-1.0, 1.0, 0.01);
    pan.setDoubleClickReturnValue (true, 0.0);
    pan.getProperties().set ("bipolar", true);
    pan.textFromValueFunction = [] (double v)
    {
        const int pc = juce::roundToInt (std::abs (v) * 100.0);
        return pc == 0 ? juce::String ("C") : (v < 0 ? "L " : "R ") + juce::String (pc);
    };

    auto& flt = knobs[kFilter].slider;
    flt.setRange (-1.0, 1.0, 0.01);
    flt.setDoubleClickReturnValue (true, 0.0);
    flt.getProperties().set ("bipolar", true);
    flt.textFromValueFunction = [] (double v)
    {
        if (v < -0.001) return "LP " + hz (gn::filterLpHz ((float) v));
        if (v > 0.001)  return "HP " + hz (gn::filterHpHz ((float) v));
        return juce::String ("Off");
    };

    for (int k = 0; k < kNumKnobs; ++k)
        knobs[(size_t) k].slider.onValueChange = [this, k] { if (! updating) knobChanged (k); };

    refresh (true);
    startTimerHz (15);
}

void StepEditor::setStep (int newStep)
{
    step = juce::jlimit (0, gn::kMaxSteps - 1, newStep);
    refresh (true);
}

const gn::TypeControl& StepEditor::typeControl (int which) const
{
    const auto& info = gn::kTypeInfo[(size_t) juce::jlimit (0, gn::kNumStutterTypes - 1, current.type)];
    return which == kCtl1 ? info.c1 : info.c2;
}

void StepEditor::configureTypeKnob (int which, int type)
{
    const auto& info = gn::kTypeInfo[(size_t) type];
    const auto& tc = which == kCtl1 ? info.c1 : info.c2;
    auto& k = knobs[(size_t) which];

    k.label.setText (juce::String (tc.name).toUpperCase(), juce::dontSendNotification);
    k.slider.setTooltip (juce::String (gn::kTypeNames[type]) + " - " + tc.name + ". " + info.help);
    k.label.setTooltip (k.slider.getTooltip());

    const float def = which == kCtl1 ? info.p1Default : info.p2Default;
    if (tc.numChoices > 0)
    {
        k.slider.setRange (0.0, tc.numChoices - 1, 1.0);
        k.slider.setDoubleClickReturnValue (true, gn::choiceIndex (def, tc.numChoices));
    }
    else
    {
        k.slider.setRange (0.0, 1.0, 0.001);
        k.slider.setDoubleClickReturnValue (true, def);
    }

    // Pitch and glide are bipolar (0 st in the middle).
    k.slider.getProperties().set ("bipolar", (gn::StutterType) type == gn::StutterType::pitch);
    k.slider.textFromValueFunction = [this, which] (double v) { return formatTypeControl (which, v); };
}

juce::String StepEditor::formatTypeControl (int which, double v) const
{
    using T = gn::StutterType;
    const auto& tc = typeControl (which);
    if (tc.numChoices > 0)
        return tc.choices[juce::jlimit (0, tc.numChoices - 1, juce::roundToInt (v))];

    const float p = (float) v;
    const bool first = which == kCtl1;
    auto offsetText = [this, p]
    {
        const double stepPpq = processor.getGridPpq() * (double) current.length;
        const int n = gn::slicesIn (stepPpq, gn::kDivisionPpq[(size_t) juce::jlimit (0, gn::kNumDivisions - 1, current.division)]);
        return "Slice " + juce::String (gn::sliceOffset (p, n) + 1) + "/" + juce::String (n);
    };

    switch ((T) current.type)
    {
        case T::repeat:   return first ? offsetText() : percent (p);
        case T::reverse:  return offsetText();
        case T::gate:     return percent (gn::gateDuty (p));
        case T::roll:     return "x" + juce::String (gn::rollFactor (p), 2);
        case T::pitch:    return signedValue (gn::pitchSemis (p), 1, " st");
        case T::tapeStop: return p < 0.01f ? juce::String ("Linear") : "Exp " + percent (p);
        case T::fade:     return juce::String (20.0 * std::log10 (gn::fadeFactor (p)), 1) + " dB/rep";
        case T::freeze:   return first ? juce::String (juce::roundToInt (gn::freezeGrainMs (p))) + " ms" : percent (p);
        case T::pingPong: return first ? percent (p) : juce::String (20.0 * std::log10 (gn::pingDecay (p)), 1) + " dB/rep";
        case T::shuffle:
        case T::scratch:  return percent (p);
    }
    return percent (p);
}

void StepEditor::knobChanged (int which)
{
    const double v = knobs[(size_t) which].slider.getValue();
    float p = (float) v;
    if (which == kCtl1 || which == kCtl2)
    {
        const auto& tc = typeControl (which);
        if (tc.numChoices > 0)
            p = (float) v / (float) (tc.numChoices - 1);
    }

    processor.editStep (processor.getEditPattern(), step, [which, v, p] (gn::StepData& s)
    {
        switch (which)
        {
            case kCtl1:   s.p1 = p; break;
            case kCtl2:   s.p2 = p; break;
            case kLength: s.length = juce::roundToInt (v); break;
            case kLevel:  s.levelDb = (float) v; break;
            case kPan:    s.pan = (float) v; break;
            case kFilter: s.filter = (float) v; break;
            default: break;
        }
    });
    current = processor.getStep (processor.getEditPattern(), step);

    // Offset readouts depend on length/division: keep them current.
    knobs[kCtl1].slider.updateText();
    knobs[kCtl2].slider.updateText();
}

void StepEditor::timerCallback()
{
    refresh (false);
}

void StepEditor::refresh (bool force)
{
    for (auto& k : knobs)
        if (k.slider.isMouseButtonDown())
            return; // don't fight the user's drag

    const int pat = processor.getEditPattern();
    const auto s = processor.getStep (pat, step);
    using gn::exactly;
    const bool changed = force || pat != shownPattern || step != shownStep || s.type != shownType
                         || ! exactly (s.p1, current.p1) || ! exactly (s.p2, current.p2) || s.length != current.length
                         || ! exactly (s.levelDb, current.levelDb) || ! exactly (s.pan, current.pan)
                         || ! exactly (s.filter, current.filter) || s.division != current.division || s.on != current.on;
    if (! changed)
        return;

    current = s;
    const juce::ScopedValueSetter<bool> svs (updating, true);

    if (force || s.type != shownType)
    {
        configureTypeKnob (kCtl1, s.type);
        configureTypeKnob (kCtl2, s.type);
        shownType = s.type;
    }
    shownPattern = pat;
    shownStep = step;

    auto setTypeKnob = [this] (int which, float p)
    {
        const auto& tc = typeControl (which);
        knobs[(size_t) which].slider.setValue (tc.numChoices > 0 ? (double) gn::choiceIndex (p, tc.numChoices) : (double) p,
                                               juce::dontSendNotification);
        knobs[(size_t) which].slider.updateText();
    };
    setTypeKnob (kCtl1, s.p1);
    setTypeKnob (kCtl2, s.p2);
    knobs[kLength].slider.setValue (s.length, juce::dontSendNotification);
    knobs[kLevel].slider.setValue (s.levelDb, juce::dontSendNotification);
    knobs[kPan].slider.setValue (s.pan, juce::dontSendNotification);
    knobs[kFilter].slider.setValue (s.filter, juce::dontSendNotification);

    for (auto& k : knobs)
    {
        k.slider.updateText(); // readouts depend on the type, length and grid
        k.slider.setAlpha (s.on ? 1.0f : 0.55f);
    }
    repaint();
}

void StepEditor::paint (juce::Graphics& g)
{
    using namespace gn::colours;
    auto r = getLocalBounds().toFloat();
    g.setColour (panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (outline);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

    auto header = getLocalBounds().reduced (14, 8).removeFromTop (20);
    const auto patName = gn::kPatternNames[(size_t) processor.getEditPattern()];
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.setColour (current.on ? acid : dimText);
    const juce::String title = juce::String ("STEP ") + juce::String (step + 1) + "  " + juce::String (gn::kTypeNames[(size_t) current.type]).toUpperCase();
    g.drawText (title, header, juce::Justification::centredLeft);
    g.setColour (dimText);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (juce::String ("PATTERN ") + patName + (current.on ? "" : "   (step is off)"), header, juce::Justification::centredRight);

    // Type description
    auto help = getLocalBounds().reduced (14, 0).withTrimmedTop (32).removeFromTop (30);
    g.setColour (text.withAlpha (0.7f));
    g.setFont (juce::FontOptions (11.5f));
    g.drawFittedText (gn::kTypeInfo[(size_t) current.type].help, help, juce::Justification::topLeft, 2, 0.95f);

    // Divider between the type controls and the shaping controls
    const auto div = knobs[kLength].label.getBounds().getX() - 6;
    g.setColour (outline);
    g.drawVerticalLine (div, (float) help.getBottom() + 6.0f, (float) getHeight() - 10.0f);
}

void StepEditor::resized()
{
    auto r = getLocalBounds().reduced (10, 0);
    r.removeFromTop (68);
    r.removeFromBottom (8);
    const int w = r.getWidth() / kNumKnobs;
    for (int k = 0; k < kNumKnobs; ++k)
    {
        auto col = r.removeFromLeft (w);
        if (k == kLength) col.removeFromLeft (6);
        knobs[(size_t) k].setBounds (col);
    }
}
