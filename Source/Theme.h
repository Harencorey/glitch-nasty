#pragma once
//==============================================================================
// Glitch Nasty - colours and the look-and-feel shared by every UI component.
//==============================================================================

#include <juce_gui_basics/juce_gui_basics.h>

namespace gn::colours
{
const juce::Colour background { 0xff0e0f11 };
const juce::Colour panel      { 0xff191a1e };
const juce::Colour raised     { 0xff22242a };
const juce::Colour outline    { 0xff2b2d33 };
const juce::Colour text       { 0xffd8dade };
const juce::Colour dimText    { 0xff6c6f78 };
const juce::Colour acid       { 0xffb6ff3b };
const juce::Colour nasty      { 0xffff2e88 };
} // namespace gn::colours

class GlitchNastyLookAndFeel : public juce::LookAndFeel_V4
{
public:
    GlitchNastyLookAndFeel()
    {
        using namespace gn::colours;
        setColour (juce::ResizableWindow::backgroundColourId, background);
        setColour (juce::Slider::textBoxTextColourId, text);
        setColour (juce::Slider::rotarySliderFillColourId, acid);
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
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextButton::textColourOnId, background);
        setColour (juce::TooltipWindow::backgroundColourId, panel.brighter (0.15f));
        setColour (juce::TooltipWindow::textColourId, text);
        setColour (juce::TooltipWindow::outlineColourId, outline);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle, juce::Slider& slider) override
    {
        using namespace gn::colours;
        auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float angle = startAngle + sliderPos * (endAngle - startAngle);
        const float alpha = slider.isEnabled() ? 1.0f : 0.35f;
        const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);

        // Bipolar knobs (pan, filter, pitch ...) draw their arc from the top.
        const bool bipolar = slider.getProperties()["bipolar"];
        const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

        juce::Path track, value;
        track.addCentredArc (centre.x, centre.y, radius - 3.0f, radius - 3.0f, 0.0f, startAngle, endAngle, true);
        value.addCentredArc (centre.x, centre.y, radius - 3.0f, radius - 3.0f, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);

        const juce::PathStrokeType stroke (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.setColour (outline.withMultipliedAlpha (alpha));
        g.strokePath (track, stroke);
        g.setColour (accent.withMultipliedAlpha (alpha));
        g.strokePath (value, stroke);

        g.setColour (panel);
        g.fillEllipse (juce::Rectangle<float> (radius * 1.3f, radius * 1.3f).withCentre (centre));
        const auto tip = centre.getPointOnCircumference (radius * 0.6f, angle);
        g.setColour (text.withMultipliedAlpha (alpha));
        g.drawLine ({ centre.getPointOnCircumference (radius * 0.2f, angle), tip }, 2.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton& b, int buttonHeight) override
    {
        const float size = b.getProperties()["small"] ? 11.0f : 13.0f;
        return juce::FontOptions (juce::jmin (size, (float) buttonHeight * 0.55f), juce::Font::bold);
    }
};

namespace gn::ui
{
// A knob with a label above it.
struct Knob
{
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label label;

    void setup (juce::Component& parent, const juce::String& name, const juce::String& help)
    {
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 84, 18);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::textBoxTextColourId, colours::text);
        slider.setTooltip (help);
        parent.addAndMakeVisible (slider);

        label.setText (name, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, colours::dimText);
        label.setTooltip (help);
        parent.addAndMakeVisible (label);
    }

    void setBounds (juce::Rectangle<int> r)
    {
        label.setBounds (r.removeFromTop (18));
        slider.setBounds (r.reduced (2, 0));
    }
};
} // namespace gn::ui
