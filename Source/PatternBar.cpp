#include "PatternBar.h"

PatternBar::PatternBar (GlitchNastyProcessor& p) : processor (p)
{
    using namespace gn::colours;

    for (int i = 0; i < gn::kNumPatterns; ++i)
    {
        auto& t = tabs[(size_t) i];
        t.setButtonText (gn::kPatternNames[i]);
        t.setClickingTogglesState (false);
        t.setTooltip ("Pattern " + juce::String (gn::kPatternNames[i]) + ". Click to play and edit it (automatable). "
                      "MIDI " + juce::MidiMessage::getMidiNoteName (gn::kPatternNoteBase + i, true, true, 3) + " also switches to it.");
        t.onClick = [this, i] { selectPattern (i); };
        t.setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i < gn::kNumPatterns - 1 ? juce::Button::ConnectedOnRight : 0));
        addAndMakeVisible (t);
    }

    auto tool = [this] (juce::TextButton& b, const juce::String& tip, std::function<void()> fn)
    {
        b.setTooltip (tip);
        b.onClick = std::move (fn);
        addAndMakeVisible (b);
    };
    tool (copyButton, "Copy this pattern.", [this] { processor.copyPattern (processor.getEditPattern()); });
    tool (pasteButton, "Paste the copied pattern over this one.", [this] { processor.pastePattern (processor.getEditPattern()); });
    tool (leftButton, "Shift the whole pattern one step earlier.", [this] { processor.shiftPattern (processor.getEditPattern(), -1); });
    tool (rightButton, "Shift the whole pattern one step later.", [this] { processor.shiftPattern (processor.getEditPattern(), +1); });
    tool (reverseButton, "Reverse the order of the steps.", [this] { processor.reversePattern (processor.getEditPattern()); });
    tool (clearButton, "Switch every step off and reset it.", [this] { processor.clearPattern (processor.getEditPattern()); });
    tool (randomButton, "Roll a new random pattern. Anything locked (to the right) is kept.", [this]
    {
        GlitchNastyProcessor::RandomLocks l;
        l.onOff  = locks[0].getToggleState();
        l.type   = locks[1].getToggleState();
        l.repeat = locks[2].getToggleState();
        l.chance = locks[3].getToggleState();
        l.shape  = locks[4].getToggleState();
        processor.randomizePattern (processor.getEditPattern(), l);
    });
    randomButton.setColour (juce::TextButton::textColourOffId, nasty);

    const char* lockNames[5] = { "ON/OFF", "TYPE", "REPEAT", "CHANCE", "SHAPE" };
    const char* lockTips[5] = { "Keep which steps are on.", "Keep each step's type.", "Keep each step's repeat length.",
                                "Keep each step's chance.", "Keep each step's controls, length, level, pan and filter." };
    for (int i = 0; i < 5; ++i)
    {
        auto& l = locks[(size_t) i];
        l.setButtonText (lockNames[i]);
        l.setClickingTogglesState (true);
        l.setTooltip ("Lock: " + juce::String (lockTips[i]) + " RANDOM won't change it.");
        l.setColour (juce::TextButton::buttonOnColourId, nasty);
        l.setColour (juce::TextButton::textColourOnId, background);
        l.getProperties().set ("small", true);
        addAndMakeVisible (l);
    }

    startTimerHz (15);
    timerCallback();
}

void PatternBar::selectPattern (int p)
{
    auto* param = processor.apvts.getParameter (gn::ids::pattern);
    param->beginChangeGesture();
    param->setValueNotifyingHost (param->convertTo0to1 ((float) p));
    param->endChangeGesture();
}

void PatternBar::timerCallback()
{
    const int edit = processor.getEditPattern();
    const int playing = processor.uiPattern.load();
    if (edit != shownPattern || playing != shownPlaying)
    {
        for (int i = 0; i < gn::kNumPatterns; ++i)
            tabs[(size_t) i].setToggleState (i == edit, juce::dontSendNotification);
        shownPattern = edit;
        shownPlaying = playing;
        repaint();
    }
    pasteButton.setEnabled (processor.hasClipboard());
}

void PatternBar::paint (juce::Graphics& g)
{
    using namespace gn::colours;
    g.setColour (dimText);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("PATTERN", getLocalBounds().removeFromLeft (64), juce::Justification::centredLeft);

    const auto lockLabel = locks[0].getBounds().withWidth (40).translated (-44, 0);
    g.drawText ("LOCK", lockLabel, juce::Justification::centredRight);

    // A pink dot under the tab that MIDI switched to (when it isn't the one you're editing).
    if (shownPlaying >= 0 && shownPlaying != shownPattern)
    {
        const auto b = tabs[(size_t) shownPlaying].getBounds().toFloat();
        g.setColour (nasty);
        g.fillEllipse (b.getCentreX() - 2.5f, b.getBottom() - 6.0f, 5.0f, 5.0f);
    }
}

void PatternBar::resized()
{
    auto r = getLocalBounds();
    r.removeFromLeft (64);
    for (auto& t : tabs)
        t.setBounds (r.removeFromLeft (30));

    r.removeFromLeft (16);
    for (auto* b : { &copyButton, &pasteButton, &leftButton, &rightButton, &reverseButton, &clearButton })
    {
        b->setBounds (r.removeFromLeft (b == &leftButton || b == &rightButton ? 34 : 56).reduced (2, 0));
    }

    r.removeFromLeft (14);
    randomButton.setBounds (r.removeFromLeft (80).reduced (2, 0));

    r.removeFromLeft (44);
    for (auto& l : locks)
        l.setBounds (r.removeFromLeft (60).reduced (2, 0));
}
