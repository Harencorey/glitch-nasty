#include "LivePanel.h"

LivePad::LivePad (GlitchNastyProcessor& p, int t) : processor (p), type (t)
{
    setTooltip ("Hold to " + juce::String (gn::kTypeNames[t]).toLowerCase() + " right now (overrides the pattern). "
                "MIDI " + juce::MidiMessage::getMidiNoteName (gn::kLiveNoteBase + t, true, true, 3) + " does the same.");
}

void LivePad::paint (juce::Graphics& g)
{
    using namespace gn::colours;
    const bool active = pressed || processor.uiLiveType.load() == type;
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (active ? nasty : raised);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (active ? nasty.brighter (0.3f) : outline);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);

    g.setColour (active ? background : text);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (juce::String (gn::kTypeNames[type]).toUpperCase(), r.withTrimmedBottom (10.0f), juce::Justification::centred);
    g.setColour ((active ? background : dimText).withAlpha (0.8f));
    g.setFont (juce::FontOptions (9.5f));
    g.drawText (juce::MidiMessage::getMidiNoteName (gn::kLiveNoteBase + type, true, true, 3), r.withTrimmedTop (18.0f),
                juce::Justification::centred);
}

void LivePad::mouseDown (const juce::MouseEvent&)
{
    pressed = true;
    processor.livePad.store (type);
    repaint();
}

void LivePad::mouseUp (const juce::MouseEvent&)
{
    pressed = false;
    int expected = type;
    processor.livePad.compare_exchange_strong (expected, -1);
    repaint();
}

//==============================================================================
LivePanel::LivePanel (GlitchNastyProcessor& p) : processor (p)
{
    using namespace gn::colours;
    for (int t = 0; t < gn::kNumStutterTypes; ++t)
    {
        pads.push_back (std::make_unique<LivePad> (processor, t));
        addAndMakeVisible (*pads.back());
    }

    auto setupCombo = [this] (juce::ComboBox& box, juce::Label& label, const char* paramId, const juce::String& name,
                              const juce::String& help, std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>& att)
    {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (paramId)))
            box.addItemList (choice->choices, 1);
        box.setTooltip (help);
        addAndMakeVisible (box);
        att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, paramId, box);

        label.setText (name, juce::dontSendNotification);
        label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, dimText);
        label.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (label);
    };
    setupCombo (repeatBox, repeatLabel, gn::ids::liveDiv, "REPEAT", "Repeat length for live pads and MIDI notes.", repeatAttachment);
    setupCombo (quantBox, quantLabel, gn::ids::liveQuant, "QUANTIZE",
                "When a held pad/note actually starts: right away (Off), or on the next 1/16, 1/8, 1/4 or bar line.", quantAttachment);

    startTimerHz (30);
}

void LivePanel::timerCallback()
{
    const int live = processor.uiLiveType.load();
    if (live != shownLive)
    {
        shownLive = live;
        for (auto& pad : pads)
            pad->repaint();
    }
}

void LivePanel::paint (juce::Graphics& g)
{
    using namespace gn::colours;
    auto r = getLocalBounds().toFloat();
    g.setColour (panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (outline);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

    auto header = getLocalBounds().reduced (14, 8).removeFromTop (20);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.setColour (nasty);
    g.drawText ("LIVE", header, juce::Justification::centredLeft);
    g.setColour (dimText);
    g.setFont (juce::FontOptions (11.5f));
    g.drawText ("hold a pad or MIDI note to glitch now", header.withTrimmedLeft (48), juce::Justification::centredLeft);
}

void LivePanel::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    r.removeFromTop (26);

    auto bottom = r.removeFromBottom (28);
    repeatLabel.setBounds (bottom.removeFromLeft (64));
    repeatBox.setBounds (bottom.removeFromLeft (90).reduced (4, 1));
    quantLabel.setBounds (bottom.removeFromLeft (80));
    quantBox.setBounds (bottom.removeFromLeft (90).reduced (4, 1));
    r.removeFromBottom (6);

    constexpr int cols = 4;
    const int rows = (gn::kNumStutterTypes + cols - 1) / cols;
    const int w = r.getWidth() / cols, h = r.getHeight() / rows;
    for (int i = 0; i < (int) pads.size(); ++i)
        pads[(size_t) i]->setBounds (r.getX() + (i % cols) * w, r.getY() + (i / cols) * h, w, h);
}
