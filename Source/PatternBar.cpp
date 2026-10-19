#include "PatternBar.h"

namespace
{
const char* const kLockNames[5] = { "On/off", "Type", "Repeat", "Chance", "Shape" };
const char* const kLockTips[5] = { "which steps are on", "each step's type", "each step's repeat length",
                                   "each step's chance", "each step's controls, length, level, pan and filter" };
} // namespace

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

    barsBox.addItem ("Chance", 1);
    barsBox.addSeparator();
    barsBox.addSectionHeading ("Locks (per step)");
    for (int t = 0; t < gn::kNumModTargets; ++t)
        barsBox.addItem (gn::kModTargetNames[t], t + 2);
    barsBox.setTooltip ("What the bars under the grid draw. Chance: how often each step fires. Anything else: a parameter lock - "
                        "draw that parameter step by step and it changes the glitch playing on each step, even in the middle of "
                        "a stretched one (e.g. Ctl 1 on a long Pitch step = a pitch melody). Ctl 1 / Ctl 2 are the step type's "
                        "own two controls. Double-click a bar to clear its lock.");
    barsBox.onChange = [this] { processor.setBarsTarget (barsBox.getSelectedId() - 2); };
    addAndMakeVisible (barsBox);

    auto tool = [this] (juce::TextButton& b, const juce::String& tip, std::function<void()> fn)
    {
        b.setTooltip (tip);
        b.onClick = std::move (fn);
        addAndMakeVisible (b);
    };
    auto pat = [this] { return processor.getEditPattern(); };
    auto lane = [this] { return processor.getActiveLane(); };
    tool (copyButton, "Copy this pattern (both lanes and their locks).", [this, pat] { processor.copyPattern (pat()); });
    tool (pasteButton, "Paste the copied pattern over this one.", [this, pat] { processor.pastePattern (pat()); });
    tool (leftButton, "Shift the lane you're editing one step earlier (locks move with it).", [this, pat, lane] { processor.shiftPattern (pat(), lane(), -1); });
    tool (rightButton, "Shift the lane you're editing one step later (locks move with it).", [this, pat, lane] { processor.shiftPattern (pat(), lane(), +1); });
    tool (reverseButton, "Reverse the order of the steps in the lane you're editing.", [this, pat, lane] { processor.reversePattern (pat(), lane()); });
    tool (clearButton, "Switch every step of the lane you're editing off and clear its locks.", [this, pat, lane] { processor.clearPattern (pat(), lane()); });
    tool (randomButton, "Roll new random steps for the lane you're editing. Anything ticked under LOCKS is kept.", [this, pat, lane]
    {
        GlitchNastyProcessor::RandomLocks l;
        l.onOff  = locks[0];
        l.type   = locks[1];
        l.repeat = locks[2];
        l.chance = locks[3];
        l.shape  = locks[4];
        processor.randomizePattern (pat(), lane(), l);
    });
    randomButton.setColour (juce::TextButton::textColourOffId, nasty);
    tool (locksButton, "What RANDOM keeps as it is.", [this] { showLocksMenu(); });

    startTimerHz (15);
    timerCallback();
}

void PatternBar::showLocksMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("RANDOM keeps...");
    for (int i = 0; i < 5; ++i)
        menu.addItem (i + 1, juce::String (kLockNames[i]) + "  (" + kLockTips[i] + ")", true, locks[(size_t) i]);

    juce::Component::SafePointer<PatternBar> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&locksButton),
                        [safe] (int r)
                        {
                            if (safe == nullptr || r <= 0)
                                return;
                            safe->locks[(size_t) r - 1] = ! safe->locks[(size_t) r - 1];
                            int n = 0;
                            for (bool b : safe->locks) n += b ? 1 : 0;
                            safe->locksButton.setButtonText (n > 0 ? "LOCKS " + juce::String (n) : juce::String ("LOCKS"));
                            safe->showLocksMenu(); // stay open-ish: tick several in a row
                        });
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
    if (processor.getBarsTarget() != shownBars)
    {
        shownBars = processor.getBarsTarget();
        barsBox.setSelectedId (shownBars + 2, juce::dontSendNotification);
    }
    pasteButton.setEnabled (processor.hasClipboard());
}

void PatternBar::paint (juce::Graphics& g)
{
    using namespace gn::colours;
    g.setColour (dimText);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("PATTERN", getLocalBounds().removeFromLeft (64), juce::Justification::centredLeft);
    g.drawText ("BARS", barsBox.getBounds().withWidth (40).translated (-42, 0), juce::Justification::centredRight);

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
        t.setBounds (r.removeFromLeft (28));

    r.removeFromLeft (60);
    barsBox.setBounds (r.removeFromLeft (100).reduced (0, 1));

    // Tools on the right.
    auto right = r;
    locksButton.setBounds (right.removeFromRight (66).reduced (2, 0));
    randomButton.setBounds (right.removeFromRight (76).reduced (2, 0));
    right.removeFromRight (8);
    for (auto* b : { &clearButton, &reverseButton, &rightButton, &leftButton, &pasteButton, &copyButton })
        b->setBounds (right.removeFromRight (b == &leftButton || b == &rightButton ? 30 : 54).reduced (2, 0));
}
