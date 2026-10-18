#include "FxPanel.h"

namespace
{
const juce::Colour kCrushColour { 0xff53d7ff };
const juce::Colour kDelayColour { 0xffffb03b };
const juce::Colour kVerbColour  { 0xffb48cff };
} // namespace

FxPanel::FxPanel (GlitchNastyProcessor& p) : processor (p)
{
    using namespace gn::ids;
    knob (crushBits, gn::ids::crushBits, "BITS", "Global crusher bit depth: off, then 16 bits down to 1. Crushes everything coming out (dry included) - for per-glitch crush use BITS in the step editor.", kCrushColour);
    knob (crushRate, gn::ids::crushRate, "RATE", "Global sample-rate reduction: off, then down to 1/64 of the sample rate. Aliasing included, on purpose.", kCrushColour);
    knob (crushMix, gn::ids::crushMix, "MIX", "How much of the crushed signal you hear.", kCrushColour);

    combo (dlyTime, dlyTimeLabel, gn::ids::dlyTime, "TIME", "Delay time, locked to the tempo.");
    knob (dlyFb, gn::ids::dlyFb, "FEEDBACK", "How many repeats. Soft-clipped in the loop, so high settings stay musical.", kDelayColour);
    knob (dlyTone, gn::ids::dlyTone, "TONE", "Filter in the feedback loop: left = each repeat darker (tape echo), right = thinner.", kDelayColour, true);
    knob (dlyMix, gn::ids::dlyMix, "MIX", "Delay return level. 0 = off.", kDelayColour);
    pingButton.setClickingTogglesState (true);
    pingButton.setTooltip ("Repeats bounce between left and right.");
    pingButton.setColour (juce::TextButton::buttonOnColourId, kDelayColour);
    pingButton.getProperties().set ("small", true);
    addAndMakeVisible (pingButton);
    pingAtt = std::make_unique<ButtonAttachment> (processor.apvts, gn::ids::dlyPing, pingButton);

    knob (revSize, gn::ids::revSize, "SIZE", "Room to huge hall (decay about 0.4 to 9 seconds).", kVerbColour);
    knob (revDamp, gn::ids::revDamp, "DAMP", "How quickly the highs die away: left bright, right dark.", kVerbColour);
    knob (revMix, gn::ids::revMix, "MIX", "Reverb return level. 0 = off.", kVerbColour);

    combo (source, sourceLabel, gn::ids::fxSource, "FEED",
           "What goes into the delay and reverb. Glitches: only the glitched audio (scaled by each step's SEND), so tails "
           "spill out of the glitches while the straight signal stays dry. Everything: the whole output.");

    sections = { Section { "CRUSH", {} }, Section { "DELAY", {} }, Section { "REVERB", {} }, Section { "SEND", {} } };
}

void FxPanel::knob (gn::ui::Knob& k, const char* id, const juce::String& name, const juce::String& help, juce::Colour accent, bool bipolar)
{
    k.setup (*this, name, help);
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    k.slider.getProperties().set ("bipolar", bipolar);
    sliderAtts.push_back (std::make_unique<SliderAttachment> (processor.apvts, id, k.slider));
}

void FxPanel::combo (juce::ComboBox& box, juce::Label& label, const char* id, const juce::String& name, const juce::String& help)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (id)))
        box.addItemList (choice->choices, 1);
    box.setTooltip (help);
    addAndMakeVisible (box);
    comboAtts.push_back (std::make_unique<ComboAttachment> (processor.apvts, id, box));

    label.setText (name, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, gn::colours::dimText);
    addAndMakeVisible (label);
}

void FxPanel::paint (juce::Graphics& g)
{
    using namespace gn::colours;
    const juce::Colour accents[4] = { kCrushColour, kDelayColour, kVerbColour, dimText };
    for (int i = 0; i < 4; ++i)
    {
        const auto& s = sections[(size_t) i];
        auto r = s.bounds.toFloat();
        g.setColour (panel);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (outline);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        g.setColour (accents[i]);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (s.title, s.bounds.reduced (12, 6).removeFromTop (18), juce::Justification::centredLeft);
    }
}

void FxPanel::resized()
{
    auto r = getLocalBounds();
    constexpr int gap = 12, knobW = 74;

    auto take = [&] (int idx, int width)
    {
        sections[(size_t) idx].bounds = r.removeFromLeft (width);
        r.removeFromLeft (gap);
        auto inner = sections[(size_t) idx].bounds.reduced (8, 6);
        inner.removeFromTop (20);
        return inner;
    };

    auto crush = take (0, 3 * knobW + 16);
    for (auto* k : { &crushBits, &crushRate, &crushMix })
        k->setBounds (crush.removeFromLeft (knobW));

    auto dly = take (1, 5 * knobW + 22);
    {
        auto col = dly.removeFromLeft (knobW + 10);
        dlyTimeLabel.setBounds (col.removeFromTop (18));
        dlyTime.setBounds (col.removeFromTop (28).reduced (4, 2));
        col.removeFromTop (8);
        pingButton.setBounds (col.removeFromTop (26).reduced (4, 0));
    }
    for (auto* k : { &dlyFb, &dlyTone, &dlyMix })
        k->setBounds (dly.removeFromLeft (knobW));

    auto rev = take (2, 3 * knobW + 16);
    for (auto* k : { &revSize, &revDamp, &revMix })
        k->setBounds (rev.removeFromLeft (knobW));

    sections[3].bounds = r;
    auto src = r.reduced (8, 6);
    src.removeFromTop (20);
    sourceLabel.setBounds (src.removeFromTop (18));
    source.setBounds (src.removeFromTop (28).reduced (4, 2));
}
