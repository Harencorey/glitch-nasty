#include "LivePanel.h"

LivePad::LivePad (GlitchNastyProcessor& p, int t) : processor (p), type (t)
{
    setTooltip ("Hold to " + juce::String (gn::kTypeNames[t]).toLowerCase() + " right now (overrides the pattern). "
                "Shift-click to latch it on hands-free (Shift-click again to let go); Shift+Cmd-click to stack it with other "
                "latched pads (up to 4, each processing the one before). Clicking also opens this pad's own settings on the left. "
                "MIDI " + juce::MidiMessage::getMidiNoteName (gn::kLiveNoteBase + t, true, true, 3) + " plays it too.");
}

void LivePad::paint (juce::Graphics& g)
{
    using namespace gn::colours;
    const uint32_t bit = 1u << type;
    const bool playing = (processor.uiLiveMask.load() & bit) != 0;
    const bool latched = (processor.liveLatch.load() & bit) != 0;
    const bool active = pressed || playing;
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (active ? nasty : (latched ? nasty.withAlpha (0.35f) : raised)); // latched but waiting for the quantize point = dim pink
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (selected ? acid : (active ? nasty.brighter (0.3f) : outline));
    g.drawRoundedRectangle (r, 5.0f, selected ? 2.0f : 1.0f);

    if (latched) // a little latch mark top-right
    {
        auto mark = r.removeFromTop (14.0f).removeFromRight (40.0f).reduced (3.0f, 2.0f);
        g.setColour (active ? background : nasty);
        g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
        g.drawText ("LATCH", mark, juce::Justification::centredRight);
        r = getLocalBounds().toFloat().reduced (2.0f);
    }

    g.setColour (active ? background : text);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (juce::String (gn::kTypeNames[type]).toUpperCase(), r.withTrimmedBottom (10.0f), juce::Justification::centred);
    g.setColour ((active ? background : dimText).withAlpha (0.8f));
    g.setFont (juce::FontOptions (9.5f));
    g.drawText (juce::MidiMessage::getMidiNoteName (gn::kLiveNoteBase + type, true, true, 3), r.withTrimmedTop (18.0f),
                juce::Justification::centred);
}

void LivePad::mouseDown (const juce::MouseEvent& e)
{
    const uint32_t bit = 1u << type;
    if (e.mods.isShiftDown())
    {
        auto latch = processor.liveLatch.load();
        if (e.mods.isCommandDown())
        {
            // Stack: toggle this pad in the set (up to 4 at once - one per lane engine).
            if ((latch & bit) != 0)                                          latch &= ~bit;
            else if (juce::countNumberOfBits (latch) < gn::kMaxLanes)       latch |= bit;
        }
        else
        {
            // Latch just this pad (Shift-click it again to let go).
            latch = latch == bit ? 0u : bit;
        }
        processor.liveLatch.store (latch);
    }
    else
    {
        pressed = true;
        processor.livePad.store (type);
    }

    if (onPressed)
        onPressed (type);
    getParentComponent()->repaint();
}

void LivePad::mouseUp (const juce::MouseEvent&)
{
    if (! pressed)
        return; // a latch click: nothing to let go of
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
        pads.back()->onPressed = [this] (int type)
        {
            setSelectedPad (type);
            if (onPadSelected)
                onPadSelected (type);
        };
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

void LivePanel::setSelectedPad (int type)
{
    for (int t = 0; t < (int) pads.size(); ++t)
    {
        pads[(size_t) t]->selected = (t == type);
        pads[(size_t) t]->repaint();
    }
}

void LivePanel::timerCallback()
{
    const auto live = processor.uiLiveMask.load(), latch = processor.liveLatch.load();
    if (live != shownLive || latch != shownLatch)
    {
        shownLive = live;
        shownLatch = latch;
        for (auto& pad : pads)
            pad->repaint();
        repaint();
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
    const int latched = juce::countNumberOfBits (processor.liveLatch.load());
    g.drawText (latched > 0 ? juce::String (latched) + (latched == 1 ? " latched" : " stacked") + " - shift-click to let go"
                            : juce::String ("hold to glitch - shift-click latch - shift+cmd-click stack"),
                header.withTrimmedLeft (48), juce::Justification::centredLeft);
}

void LivePanel::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    r.removeFromTop (26);

    auto bottom = r.removeFromBottom (28);
    const int w4 = bottom.getWidth() / 4;
    repeatLabel.setBounds (bottom.removeFromLeft (w4 - 6));
    repeatBox.setBounds (bottom.removeFromLeft (w4 + 6).reduced (4, 1));
    quantLabel.setBounds (bottom.removeFromLeft (w4 + 6));
    quantBox.setBounds (bottom.reduced (4, 1));
    r.removeFromBottom (6);

    constexpr int cols = 4;
    const int rows = (gn::kNumStutterTypes + cols - 1) / cols;
    const int w = r.getWidth() / cols, h = r.getHeight() / rows;
    for (int i = 0; i < (int) pads.size(); ++i)
        pads[(size_t) i]->setBounds (r.getX() + (i % cols) * w, r.getY() + (i / cols) * h, w, h);
}
