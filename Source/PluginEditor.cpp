#include "PluginEditor.h"

using namespace gn::colours;

GlitchNastyEditor::GlitchNastyEditor (GlitchNastyProcessor& p)
    : AudioProcessorEditor (&p), processor (p),
      patternBar (p), grid (p), stepEditor (p), livePanel (p), fxPanel (p)
{
    setLookAndFeel (&lookAndFeel);

    setupCombo (syncBox, syncLabel, gn::ids::sync, "SYNC",
                "Host: follow the DAW's tempo and position (passes audio straight through while the transport is stopped). "
                "Internal: run on the BPM knob, always. In Standalone there's no host, so it always runs Internal.",
                syncAttachment);
    setupCombo (gridBox, gridLabel, gn::ids::grid, "GRID",
                "How long each step is. 16 steps of 1/16 = one bar of 4/4; 1/4 = four bars.", gridAttachment);

    attachKnob (bpm, bpmAtt, gn::ids::bpm, "BPM", "Internal tempo. Used in Standalone, or with Sync set to Internal. Tap the TAP button to set it.");
    attachKnob (steps, stepsAtt, gn::ids::steps, "STEPS", "Pattern length. Steps past this are skipped (shown dim). Odd lengths drift against the bar for polyrhythms.");
    attachKnob (swing, swingAtt, gn::ids::swing, "SWING", "Pushes every second step late, MPC-style. 50% = straight, 66% = triplet feel, 75% = hard shuffle.");
    attachKnob (xfade, xfadeAtt, gn::ids::xfade, "X-FADE", "Crossfade at every loop point. ~3 ms is clean on almost anything; 0 ms is hard cuts with clicks (sometimes that's the point); longer gets smeary.");
    attachKnob (mix, mixAtt, gn::ids::mix, "MIX", "How much of the glitch you hear while a step is firing. Between glitches you always hear the dry signal untouched.");
    attachKnob (density, densityAtt, gn::ids::density, "DENSITY",
                "Scales every step's chance. Negative thins the pattern out (-100% = nothing fires), positive fills it in "
                "(+100% = every on-step fires). Great to automate into a drop.", true);
    attachKnob (seed, seedAtt, gn::ids::seed, "SEED",
                "The dice behind Chance, Variation, Shuffle and Freeze texture. The same seed always makes the same decisions at the "
                "same point in the song, so a happy accident bounces the same every time. Change it for a new take.");
    attachKnob (output, outputAtt, gn::ids::output, "OUTPUT", "Output trim.");

    tapButton.setTooltip ("Tap on the beat to set the internal tempo (averages your last 4 taps). Each tap also snaps the grid to the nearest beat, so you can line it up with a live band.");
    tapButton.setColour (juce::TextButton::textColourOffId, acid);
    tapButton.onClick = [this] { tap(); };
    addAndMakeVisible (tapButton);

    clockReadout.setJustificationType (juce::Justification::centred);
    clockReadout.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    clockReadout.setTooltip ("The clock actually driving the grid right now.");
    addAndMakeVisible (clockReadout);

    engageButton.setClickingTogglesState (true);
    engageButton.setTooltip ("Pattern on/off. Changes land on the next bar line, so you can hit it early. "
                             "A sustain pedal / footswitch (MIDI CC64) toggles it too - handy for guitar live. Live pads and notes always work.");
    addAndMakeVisible (engageButton);
    engageAttachment = std::make_unique<ButtonAttachment> (processor.apvts, gn::ids::engage, engageButton);
    engageStatus.setJustificationType (juce::Justification::centred);
    engageStatus.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    addAndMakeVisible (engageStatus);

    addAndMakeVisible (patternBar);
    addAndMakeVisible (grid);
    addAndMakeVisible (stepEditor);
    addAndMakeVisible (livePanel);
    addAndMakeVisible (fxPanel);

    // The editor follows whatever you clicked last: a step number or a live pad.
    grid.onStepSelected = [this] (int s)
    {
        stepEditor.setStep (s);
        livePanel.setSelectedPad (-1);
    };
    livePanel.onPadSelected = [this] (int type)
    {
        stepEditor.setLivePad (type);
        grid.setSelectedStep (-1);
    };
    grid.setSelectedStep (3);
    stepEditor.setStep (3);

    setSize (1100, 884);
    startTimerHz (10);
}

GlitchNastyEditor::~GlitchNastyEditor()
{
    setLookAndFeel (nullptr);
}

void GlitchNastyEditor::attachKnob (gn::ui::Knob& k, std::unique_ptr<SliderAttachment>& att, const char* paramId,
                                    const juce::String& name, const juce::String& help, bool bipolar)
{
    k.setup (*this, name, help);
    k.slider.getProperties().set ("bipolar", bipolar);
    att = std::make_unique<SliderAttachment> (processor.apvts, paramId, k.slider);
}

void GlitchNastyEditor::setupCombo (juce::ComboBox& box, juce::Label& label, const char* paramId, const juce::String& name,
                                    const juce::String& help, std::unique_ptr<ComboAttachment>& attachment)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (paramId)))
        box.addItemList (choice->choices, 1);
    box.setTooltip (help);
    addAndMakeVisible (box);
    attachment = std::make_unique<ComboAttachment> (processor.apvts, paramId, box);

    label.setText (name, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, dimText);
    addAndMakeVisible (label);
}

//==============================================================================
void GlitchNastyEditor::tap()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (! tapTimes.empty() && now - tapTimes.back() > 2000.0)
        tapTimes.clear(); // a long pause starts a fresh count

    tapTimes.push_back (now);
    if (tapTimes.size() > 5)
        tapTimes.erase (tapTimes.begin());

    processor.snapRequest.store (true);

    if (tapTimes.size() >= 2)
    {
        const double avgMs = (tapTimes.back() - tapTimes.front()) / (double) (tapTimes.size() - 1);
        const float newBpm = (float) juce::jlimit (40.0, 300.0, 60000.0 / avgMs);

        auto* param = processor.apvts.getParameter (gn::ids::bpm);
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (newBpm));
        param->endChangeGesture();
    }
}

void GlitchNastyEditor::timerCallback()
{
    const bool host = processor.uiHostClock.load();
    const juce::String text = (host ? "HOST  " : "INTERNAL  ") + juce::String (processor.uiBpm.load(), 1) + " BPM";
    if (clockReadout.getText() != text)
        clockReadout.setText (text, juce::dontSendNotification);
    clockReadout.setColour (juce::Label::textColourId, host ? acid : nasty);

    // The BPM knob and TAP only matter when the internal clock is running.
    bpm.slider.setEnabled (! host);
    tapButton.setEnabled (! host);

    // Engage status: what's actually happening (the footswitch can flip it).
    const bool engaged = processor.uiEngaged.load(), pending = processor.uiEngagePending.load();
    blink = (blink + 1) % 10;
    juce::String status = engaged ? "PATTERN ON" : "PATTERN OFF";
    auto colour = engaged ? acid : dimText;
    if (pending)
    {
        status = engaged ? "OFF AT NEXT BAR" : "ON AT NEXT BAR";
        colour = blink < 5 ? nasty : nasty.withAlpha (0.4f);
    }
    if (engageStatus.getText() != status)
        engageStatus.setText (status, juce::dontSendNotification);
    engageStatus.setColour (juce::Label::textColourId, colour);
}

//==============================================================================
void GlitchNastyEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    auto header = getLocalBounds().removeFromTop (52).reduced (20, 0);
    g.setColour (acid);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("GLITCH NASTY", header, juce::Justification::centredLeft);

    g.setColour (dimText);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("NORKA DSP   v" JucePlugin_VersionString, header, juce::Justification::centredRight);

    g.setColour (outline);
    g.drawHorizontalLine (52, 20.0f, (float) getWidth() - 20.0f);
}

void GlitchNastyEditor::resized()
{
    auto area = getLocalBounds().reduced (20, 0);
    area.removeFromTop (60);

    // Global controls
    auto controls = area.removeFromTop (112);
    constexpr int knobW = 78, comboW = 96;

    auto placeCombo = [&] (juce::ComboBox& box, juce::Label& label)
    {
        auto col = controls.removeFromLeft (comboW);
        label.setBounds (col.removeFromTop (18));
        box.setBounds (col.withSizeKeepingCentre (col.getWidth() - 12, 26).withY (col.getY() + 24));
    };

    placeCombo (syncBox, syncLabel);
    bpm.setBounds (controls.removeFromLeft (knobW));
    {
        auto col = controls.removeFromLeft (104);
        col.removeFromTop (18);
        tapButton.setBounds (col.removeFromTop (32).reduced (8, 0));
        col.removeFromTop (6);
        clockReadout.setBounds (col.removeFromTop (36));
    }
    controls.removeFromLeft (8);
    placeCombo (gridBox, gridLabel);
    for (auto* k : { &steps, &swing, &xfade, &mix, &density, &seed, &output })
        k->setBounds (controls.removeFromLeft (knobW));
    {
        auto col = controls.removeFromRight (110);
        col.removeFromTop (18);
        engageButton.setBounds (col.removeFromTop (34).reduced (4, 0));
        col.removeFromTop (6);
        engageStatus.setBounds (col.removeFromTop (20));
    }

    area.removeFromTop (8);
    patternBar.setBounds (area.removeFromTop (28));
    area.removeFromTop (10);
    grid.setBounds (area.removeFromTop (262));
    area.removeFromTop (12);

    auto bottom = area.removeFromTop (226);
    livePanel.setBounds (bottom.removeFromRight (360));
    bottom.removeFromRight (12);
    stepEditor.setBounds (bottom);

    area.removeFromTop (12);
    fxPanel.setBounds (area.removeFromTop (134));
}
