#include "PluginEditor.h"

using namespace gn::colours;

//==============================================================================
GlitchNastyLookAndFeel::GlitchNastyLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, text);
    setColour (juce::ComboBox::backgroundColourId, panel);
    setColour (juce::ComboBox::outlineColourId, outline);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, acid);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, acid);
    setColour (juce::PopupMenu::highlightedTextColourId, background);
    setColour (juce::TextButton::buttonColourId, panel);
    setColour (juce::TextButton::buttonOnColourId, acid);
    setColour (juce::TextButton::textColourOffId, acid);
    setColour (juce::TextButton::textColourOnId, background);
    setColour (juce::TooltipWindow::backgroundColourId, panel.brighter (0.15f));
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, outline);
}

void GlitchNastyLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                               float sliderPos, float startAngle, float endAngle, juce::Slider& slider)
{
    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const float alpha = slider.isEnabled() ? 1.0f : 0.35f;

    juce::Path track, value;
    track.addCentredArc (centre.x, centre.y, radius - 3.0f, radius - 3.0f, 0.0f, startAngle, endAngle, true);
    value.addCentredArc (centre.x, centre.y, radius - 3.0f, radius - 3.0f, 0.0f, startAngle, angle, true);

    const juce::PathStrokeType stroke (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour (outline.withMultipliedAlpha (alpha));
    g.strokePath (track, stroke);
    g.setColour (acid.withMultipliedAlpha (alpha));
    g.strokePath (value, stroke);

    g.setColour (panel);
    g.fillEllipse (juce::Rectangle<float> (radius * 1.3f, radius * 1.3f).withCentre (centre));
    const auto tip = centre.getPointOnCircumference (radius * 0.6f, angle);
    g.setColour (text.withMultipliedAlpha (alpha));
    g.drawLine ({ centre.getPointOnCircumference (radius * 0.2f, angle), tip }, 2.0f);
}

//==============================================================================
GlitchNastyEditor::GlitchNastyEditor (GlitchNastyProcessor& p)
    : AudioProcessorEditor (&p), processor (p), grid (p)
{
    setLookAndFeel (&lookAndFeel);

    setupCombo (syncBox, syncLabel, gn::ids::sync, "SYNC",
                "Host: follow the DAW's tempo and position (passes audio straight through while the transport is stopped). "
                "Internal: run on the BPM knob, always. In Standalone there's no host, so it always runs Internal.",
                syncAttachment);
    setupCombo (gridBox, gridLabel, gn::ids::grid, "GRID",
                "How long each step is. 16 steps of 1/16 = one bar of 4/4.", gridAttachment);

    setupKnob (bpm, gn::ids::bpm, "BPM", "Internal tempo. Used in Standalone, or with Sync set to Internal. Tap the TAP button to set it.");
    setupKnob (steps, gn::ids::steps, "STEPS", "Pattern length. Steps past this are skipped (shown dim). Odd lengths drift against the bar for polyrhythms.");
    setupKnob (xfade, gn::ids::xfade, "X-FADE", "Crossfade at every loop point. ~3 ms is clean on almost anything; 0 ms is hard cuts with clicks (sometimes that's the point); longer gets smeary.");
    setupKnob (mix, gn::ids::mix, "MIX", "How much of the glitch you hear while a step is firing. Between glitches you always hear the dry signal untouched.");
    setupKnob (output, gn::ids::output, "OUTPUT", "Output trim.");

    tapButton.setTooltip ("Tap on the beat to set the internal tempo (averages your last 4 taps). Each tap also snaps the grid to the nearest beat, so you can line it up with a live band.");
    tapButton.onClick = [this] { tap(); };
    addAndMakeVisible (tapButton);

    clockReadout.setJustificationType (juce::Justification::centred);
    clockReadout.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    clockReadout.setTooltip ("The clock actually driving the grid right now.");
    addAndMakeVisible (clockReadout);

    addAndMakeVisible (grid);

    setSize (960, 460);
    startTimerHz (10);
}

GlitchNastyEditor::~GlitchNastyEditor()
{
    setLookAndFeel (nullptr);
}

void GlitchNastyEditor::setupKnob (Knob& k, const char* paramId, const juce::String& name, const juce::String& help)
{
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxTextColourId, text);
    k.slider.setTooltip (help);
    addAndMakeVisible (k.slider);
    k.attachment = std::make_unique<SliderAttachment> (processor.apvts, paramId, k.slider);

    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    k.label.setColour (juce::Label::textColourId, dimText);
    addAndMakeVisible (k.label);
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

        auto* p = processor.apvts.getParameter (gn::ids::bpm);
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (newBpm));
        p->endChangeGesture();
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

    auto controls = area.removeFromTop (118);
    const int colW = 92;

    auto placeKnob = [&] (Knob& k)
    {
        auto col = controls.removeFromLeft (colW);
        k.label.setBounds (col.removeFromTop (18));
        k.slider.setBounds (col.reduced (4, 0));
    };
    auto placeCombo = [&] (juce::ComboBox& box, juce::Label& label)
    {
        auto col = controls.removeFromLeft (colW + 8);
        label.setBounds (col.removeFromTop (18));
        box.setBounds (col.withSizeKeepingCentre (col.getWidth() - 12, 26).withY (col.getY() + 24));
    };

    placeCombo (syncBox, syncLabel);
    placeKnob (bpm);
    {
        auto col = controls.removeFromLeft (colW + 20);
        col.removeFromTop (18);
        tapButton.setBounds (col.removeFromTop (34).reduced (8, 0));
        col.removeFromTop (8);
        clockReadout.setBounds (col.removeFromTop (40));
    }
    controls.removeFromLeft (16);
    placeCombo (gridBox, gridLabel);
    placeKnob (steps);
    controls.removeFromLeft (16);
    placeKnob (xfade);
    placeKnob (mix);
    placeKnob (output);

    area.removeFromTop (14);
    grid.setBounds (area.removeFromTop (area.getHeight() - 18));
}
