#include "StepEditor.h"
#include "Format.h"

StepEditor::StepEditor (GlitchNastyProcessor& p) : processor (p)
{
    using namespace gn::colours;

    knobs[kCtl1].setup (*this, "CTL 1", {});
    knobs[kCtl2].setup (*this, "CTL 2", {});
    knobs[kLevel].setup (*this, "LEVEL", "Level of this glitch. All the way down = silent (a mute step).");
    knobs[kPan].setup (*this, "PAN", "Where this glitch sits in the stereo field.");
    knobs[kFilter].setup (*this, "FILTER", "Left = low-pass (down to 200 Hz), right = high-pass (up to 4 kHz), centre = off. Only on the glitch, never the dry signal.");
    knobs[kBits].setup (*this, "BITS", "Bit crush for this glitch: off, then 16 bits down to 1. Set it differently on each step to sequence the crusher.");
    knobs[kRate].setup (*this, "RATE", "Sample-rate reduction for this glitch: off, then down to 1/64 of the sample rate (aliasing included, on purpose).");
    knobs[kSend].setup (*this, "SEND", "How much of this glitch goes to the delay and reverb (when the FX source is set to Glitches).");

    for (int k : { kCtl1, kCtl2 })
        knobs[(size_t) k].slider.setColour (juce::Slider::rotarySliderFillColourId, nasty);

    auto& lvl = knobs[kLevel].slider;
    lvl.setRange (gn::kLevelMinDb, gn::kLevelMaxDb, 0.1);
    lvl.setSkewFactorFromMidPoint (-6.0);
    lvl.setDoubleClickReturnValue (true, 0.0);
    lvl.textFromValueFunction = [] (double v) { return gn::fmt::level (v); };

    auto& pan = knobs[kPan].slider;
    pan.setRange (-1.0, 1.0, 0.01);
    pan.setDoubleClickReturnValue (true, 0.0);
    pan.getProperties().set ("bipolar", true);
    pan.textFromValueFunction = [] (double v) { return gn::fmt::pan (v); };

    auto& flt = knobs[kFilter].slider;
    flt.setRange (-1.0, 1.0, 0.01);
    flt.setDoubleClickReturnValue (true, 0.0);
    flt.getProperties().set ("bipolar", true);
    flt.textFromValueFunction = [] (double v) { return gn::fmt::filter (v); };

    auto& bits = knobs[kBits].slider;
    bits.setRange (0.0, 1.0, 0.001);
    bits.setDoubleClickReturnValue (true, 0.0);
    bits.textFromValueFunction = [] (double v) { return gn::fmt::bits (v); };

    auto& rate = knobs[kRate].slider;
    rate.setRange (0.0, 1.0, 0.001);
    rate.setDoubleClickReturnValue (true, 0.0);
    rate.textFromValueFunction = [] (double v) { return gn::fmt::rate (v); };

    auto& send = knobs[kSend].slider;
    send.setRange (0.0, 1.0, 0.01);
    send.setDoubleClickReturnValue (true, 1.0);
    send.textFromValueFunction = [] (double v) { return gn::fmt::percent (v); };

    for (int k : { kBits, kRate, kSend })
        knobs[(size_t) k].slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff53d7ff));

    for (int k = 0; k < kNumKnobs; ++k)
        knobs[(size_t) k].slider.onValueChange = [this, k] { if (! updating) knobChanged (k); };

    for (int w = 0; w < 2; ++w)
    {
        addChildComponent (switches[(size_t) w]);
        switches[(size_t) w].onChange = [this, w] (int index)
        {
            const auto& tc = typeControl (w == 0 ? kCtl1 : kCtl2);
            const float value = tc.numChoices > 1 ? (float) index / (float) (tc.numChoices - 1) : 0.0f;
            editTarget ([w, value] (gn::StepData& st) { if (w == 0) st.p1 = value; else st.p2 = value; });
            knobs[kCtl1].slider.updateText();
            knobs[kCtl2].slider.updateText();
        };
    }

    refresh (true);
    startTimerHz (15);
}

void StepEditor::setStep (int newLane, int column)
{
    lane = juce::jlimit (0, gn::kNumLanes - 1, newLane);
    step = juce::jlimit (0, gn::kMaxSteps - 1, column);
    liveTarget = false;
    refresh (true);
}

int StepEditor::ownerStep() const
{
    const auto pat = processor.getPattern (processor.getEditPattern());
    return gn::ownerOf (pat.lanes[(size_t) lane].steps, step, processor.getNumSteps());
}

void StepEditor::setLivePad (int type)
{
    liveType = juce::jlimit (0, gn::kNumStutterTypes - 1, type);
    liveTarget = true;
    refresh (true);
}

gn::StepData StepEditor::readTarget() const
{
    return liveTarget ? processor.getLiveStep (liveType) : processor.getStep (processor.getEditPattern(), lane, ownerStep());
}

void StepEditor::editTarget (const std::function<void (gn::StepData&)>& change)
{
    if (liveTarget) processor.editLiveStep (liveType, change);
    else            processor.editStep (processor.getEditPattern(), lane, ownerStep(), change);
    current = readTarget();
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
    auto& sw = switches[(size_t) (which == kCtl1 ? 0 : 1)];
    const bool isChoice = tc.numChoices > 0;
    k.slider.setVisible (! isChoice);
    sw.setVisible (isChoice);
    if (isChoice)
    {
        juce::StringArray names;
        for (int i = 0; i < tc.numChoices; ++i) names.add (tc.choices[i]);
        sw.setChoices (names);
        sw.setTooltip (k.slider.getTooltip());
    }
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

    k.slider.getProperties().set ("bipolar", (gn::StutterType) type == gn::StutterType::pitch);
    k.slider.textFromValueFunction = [this, which] (double v) { return formatTypeControl (which, v); };
}

juce::String StepEditor::formatTypeControl (int which, double v) const
{
    const auto& tc = typeControl (which);
    const float p = tc.numChoices > 1 ? (float) v / (float) (tc.numChoices - 1) : (float) v;
    int slices = 0;
    if (! liveTarget)
    {
        const double stepPpq = processor.getGridPpq() * (double) current.length;
        slices = gn::slicesIn (stepPpq, gn::kDivisionPpq[(size_t) juce::jlimit (0, gn::kNumDivisions - 1, current.division)]);
    }
    return gn::fmt::typeControl (current.type, which == kCtl1, p, slices);
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

    editTarget ([which, v, p] (gn::StepData& s)
    {
        switch (which)
        {
            case kCtl1:   s.p1 = p; break;
            case kCtl2:   s.p2 = p; break;
            case kLevel:  s.levelDb = (float) v; break;
            case kPan:    s.pan = (float) v; break;
            case kFilter: s.filter = (float) v; break;
            case kBits:   s.bits = (float) v; break;
            case kRate:   s.rate = (float) v; break;
            case kSend:   s.send = (float) v; break;
            default: break;
        }
    });

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

    using gn::exactly;
    const int pat = processor.getEditPattern();
    const auto s = readTarget();
    const bool changed = force || pat != shownPattern || step != shownStep || lane != shownLane || liveTarget != shownLive || s.type != shownType
                         || ! exactly (s.p1, current.p1) || ! exactly (s.p2, current.p2) || s.length != current.length
                         || ! exactly (s.levelDb, current.levelDb) || ! exactly (s.pan, current.pan)
                         || ! exactly (s.filter, current.filter) || s.division != current.division || s.on != current.on
                         || ! exactly (s.bits, current.bits) || ! exactly (s.rate, current.rate) || ! exactly (s.send, current.send);
    if (! changed)
        return;

    current = s;
    const juce::ScopedValueSetter<bool> svs (updating, true);

    if (force || s.type != shownType || liveTarget != shownLive)
    {
        configureTypeKnob (kCtl1, s.type);
        configureTypeKnob (kCtl2, s.type);
        shownType = s.type;
    }
    shownPattern = pat;
    shownStep = step;
    shownLane = lane;
    shownLive = liveTarget;

    auto setTypeKnob = [this] (int which, float p)
    {
        const auto& tc = typeControl (which);
        knobs[(size_t) which].slider.setValue (tc.numChoices > 0 ? (double) gn::choiceIndex (p, tc.numChoices) : (double) p,
                                               juce::dontSendNotification);
    };
    setTypeKnob (kCtl1, s.p1);
    setTypeKnob (kCtl2, s.p2);
    for (int w = 0; w < 2; ++w)
    {
        const auto& tc = typeControl (w == 0 ? kCtl1 : kCtl2);
        if (tc.numChoices > 0)
            switches[(size_t) w].setIndex (gn::choiceIndex (w == 0 ? s.p1 : s.p2, tc.numChoices));
    }
    knobs[kLevel].slider.setValue (s.levelDb, juce::dontSendNotification);
    knobs[kPan].slider.setValue (s.pan, juce::dontSendNotification);
    knobs[kFilter].slider.setValue (s.filter, juce::dontSendNotification);
    knobs[kBits].slider.setValue (s.bits, juce::dontSendNotification);
    knobs[kRate].slider.setValue (s.rate, juce::dontSendNotification);
    knobs[kSend].slider.setValue (s.send, juce::dontSendNotification);

    for (auto& k : knobs)
    {
        k.slider.updateText(); // readouts depend on the type, length and grid
        k.slider.setAlpha (s.on || liveTarget ? 1.0f : 0.55f);
    }
    repaint();
}

void StepEditor::paint (juce::Graphics& g)
{
    using namespace gn::colours;
    auto r = getLocalBounds().toFloat();
    g.setColour (panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (liveTarget ? nasty.withAlpha (0.6f) : outline);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, liveTarget ? 1.5f : 1.0f);

    auto header = getLocalBounds().reduced (14, 8).removeFromTop (20);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    const auto typeName = juce::String (gn::kTypeNames[(size_t) current.type]).toUpperCase();
    if (liveTarget)
    {
        g.setColour (nasty);
        g.drawText ("LIVE PAD  " + typeName, header, juce::Justification::centredLeft);
        g.setColour (dimText);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("click a step number to edit the pattern", header, juce::Justification::centredRight);
    }
    else
    {
        const auto pat = processor.getPattern (processor.getEditPattern());
        const int n = processor.getNumSteps();
        const int owner = gn::ownerOf (pat.lanes[(size_t) lane].steps, step, n);
        const int span = current.on ? std::max (1, std::min (current.length, n - owner)) : 1;
        const auto laneColour = lane == 0 ? acid : gn::colours::lane2;
        g.setColour (current.on ? laneColour : dimText);
        juce::String title = "LANE " + juce::String (lane + 1) + "  ";
        title << (span > 1 ? "STEPS " + juce::String (owner + 1) + "-" + juce::String (owner + span) : "STEP " + juce::String (owner + 1));
        title << "  " << typeName;
        g.drawText (title, header, juce::Justification::centredLeft);

        // How many repeats this step has room for: several controls only show with 2+.
        const double stepPpq = processor.getGridPpq() * (double) current.length;
        const int slices = gn::slicesIn (stepPpq, gn::kDivisionPpq[(size_t) juce::jlimit (0, gn::kNumDivisions - 1, current.division)]);
        g.setColour (dimText);
        g.setFont (juce::FontOptions (12.0f));
        juce::String info = juce::String ("PATTERN ") + gn::kPatternNames[(size_t) processor.getEditPattern()]
                            + "   " + juce::String (slices - 1) + (slices == 2 ? " repeat" : " repeats");
        if (! current.on) info << "   (step is off)";

        // Locks drawn on the selected column (they override the knobs on that step).
        int locks = 0;
        for (const auto& row : pat.lanes[(size_t) lane].mod)
            locks += gn::isSet (row[(size_t) step]) ? 1 : 0;
        if (locks > 0)
            info << "   " << locks << (locks == 1 ? " lock" : " locks") << " on step " << (step + 1);
        g.drawText (info, header, juce::Justification::centredRight);
    }

    auto help = getLocalBounds().reduced (14, 0).withTrimmedTop (32).removeFromTop (30);
    g.setColour (text.withAlpha (0.7f));
    g.setFont (juce::FontOptions (11.5f));
    g.drawFittedText (gn::kTypeInfo[(size_t) current.type].help, help, juce::Justification::topLeft, 2, 0.95f);

    // Dividers: type controls | shaping | crush & send
    g.setColour (outline);
    for (int k : { kLevel, kBits })
        g.drawVerticalLine (knobs[(size_t) k].label.getBounds().getX() - 3, (float) help.getBottom() + 6.0f, (float) getHeight() - 10.0f);
}

void StepEditor::resized()
{
    auto r = getLocalBounds().reduced (8, 0);
    r.removeFromTop (68);
    r.removeFromBottom (8);
    const int w = r.getWidth() / kNumKnobs;
    for (int k = 0; k < kNumKnobs; ++k)
        knobs[(size_t) k].setBounds (r.removeFromLeft (w));
    for (int w2 = 0; w2 < 2; ++w2)
        switches[(size_t) w2].setBounds (knobs[(size_t) w2].slider.getBounds());
}
