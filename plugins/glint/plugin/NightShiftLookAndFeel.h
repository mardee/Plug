#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

class NightShiftLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    NightShiftLookAndFeel()
    {
        const juce::Colour text { 0xffe4dfca }, dim { 0xffaba998 }, dark { 0xff1d201e };
        setColour (juce::Label::textColourId, text);
        setColour (juce::Slider::textBoxTextColourId, text);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xffffd996));
        setColour (juce::ComboBox::backgroundColourId, dark);
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff646354));
        setColour (juce::ComboBox::arrowColourId, dim);
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff20231f));
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff5b533c));
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::TextButton::buttonColourId, dark);
        setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff494736));
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff242721));
        setColour (juce::TooltipWindow::textColourId, text);
        setColour (juce::TooltipWindow::outlineColourId, dim);
    }

    static void glowPath (juce::Graphics& g, const juce::Path& path, juce::Colour colour,
                          float strength, float width = 1.2f)
    {
        strength = juce::jlimit (0.0f, 1.0f, strength);
        for (const auto spread : { 7.0f, 3.5f, 1.5f })
        {
            g.setColour (colour.withAlpha (strength * (spread > 5.0f ? 0.045f : 0.10f)));
            g.strokePath (path, juce::PathStrokeType (width + spread, juce::PathStrokeType::curved));
        }
        g.setColour (colour.withAlpha (strength));
        g.strokePath (path, juce::PathStrokeType (width, juce::PathStrokeType::curved));
        g.setColour (colour.interpolatedWith (juce::Colours::white, 0.65f).withAlpha (strength * 0.9f));
        g.strokePath (path, juce::PathStrokeType (width * 0.38f));
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float value,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        const auto cx = static_cast<float> (x) + width * 0.5f;
        const auto cy = static_cast<float> (y) + height * 0.5f;
        const auto radius = juce::jmax (6.0f, juce::jmin (width, height) * 0.5f - 7.0f);
        const bool lit = static_cast<bool> (slider.getProperties().getWithDefault ("laneLit", true));
        const float welcome = static_cast<float> (slider.getProperties().getWithDefault ("lightBoost", 0.0f));
        const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);
        const auto power = lit ? juce::jlimit (0.0f, 1.0f, 0.90f + welcome) : 0.0f;
        const auto angle = startAngle + value * (endAngle - startAngle);

        g.setColour (juce::Colour (0xbb050605));
        g.fillEllipse (cx - radius - 2, cy - radius + 1, radius * 2 + 5, radius * 2 + 5);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff656559), cx, cy - radius,
                                                 juce::Colour (0xff20231f), cx, cy + radius, false));
        g.fillEllipse (cx - radius, cy - radius, radius * 2, radius * 2);
        for (int i = 0; i < 32; ++i)
        {
            const auto a = i * juce::MathConstants<float>::twoPi / 32.0f;
            g.setColour (juce::Colour (0xa00a0c0b));
            g.drawLine (cx + std::sin (a) * (radius - 1), cy - std::cos (a) * (radius - 1),
                        cx + std::sin (a) * (radius - 3), cy - std::cos (a) * (radius - 3), 0.65f);
        }
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff393d35), cx - radius, cy - radius,
                                                 juce::Colour (0xff1b201c), cx + radius, cy + radius, false));
        g.fillEllipse (cx - radius + 3, cy - radius + 3, radius * 2 - 6, radius * 2 - 6);
        g.setColour (juce::Colour (0xff7c7b69).withAlpha (0.45f));
        g.drawEllipse (cx - radius + 2, cy - radius + 2, radius * 2 - 4, radius * 2 - 4, 0.7f);

        juce::Path rim;
        rim.addCentredArc (cx, cy, radius + 2, radius + 2, 0, startAngle, endAngle, true);
        g.setColour (juce::Colour (0xff777866).withAlpha (0.32f));
        g.strokePath (rim, juce::PathStrokeType (1.0f));
        if (value > 0.0f)
        {
            juce::Path active;
            active.addCentredArc (cx, cy, radius + 2, radius + 2, 0, startAngle, angle, true);
            glowPath (g, active, accent, power, 1.5f);
        }
        int tickCount = static_cast<int> (slider.getProperties().getWithDefault ("tickCount", 0));
        if (tickCount <= 0)
        {
            const auto range = slider.getRange();
            const auto interval = slider.getInterval();
            if (interval > 0.0 && range.getLength() > 0.0)
            {
                const auto steps = juce::roundToInt (range.getLength() / interval) + 1;
                if (steps >= 2 && steps <= 32)
                    tickCount = steps;
            }
        }
        if (tickCount < 2)
            tickCount = 10;

        for (int k = 0; k < tickCount; ++k)
        {
            const auto t = static_cast<float> (k) / static_cast<float> (tickCount - 1);
            const auto a = startAngle + t * (endAngle - startAngle);
            g.setColour (lit && t <= (value + 0.0001f) ? accent.withAlpha (power) : juce::Colour (0xff737663));
            g.drawLine (cx + std::sin (a) * (radius + 5), cy - std::cos (a) * (radius + 5),
                        cx + std::sin (a) * (radius + 7), cy - std::cos (a) * (radius + 7), 1.1f);
        }
        juce::Path pointer;
        pointer.startNewSubPath (cx + std::sin (angle) * 3, cy - std::cos (angle) * 3);
        pointer.lineTo (cx + std::sin (angle) * (radius - 5), cy - std::cos (angle) * (radius - 5));
        if (lit)
            glowPath (g, pointer, accent, power, 1.7f);
        else
        {
            g.setColour (juce::Colour (0xff929582));
            g.strokePath (pointer, juce::PathStrokeType (1.4f));
        }
        if (slider.hasKeyboardFocus (true))
        {
            g.setColour (accent.withAlpha (0.8f));
            g.drawRoundedRectangle (slider.getLocalBounds().toFloat().reduced (0.5f), 3, 0.8f);
        }
    }

    juce::Label* createSliderTextBox (juce::Slider& slider) override
    {
        auto* label = juce::LookAndFeel_V4::createSliderTextBox (slider);
        const bool isTiming = static_cast<bool> (slider.getProperties().getWithDefault ("timingKnob", false));
        label->setFont (juce::FontOptions (isTiming ? 12.0f : 13.0f));
        label->setJustificationType (juce::Justification::centred);
        label->setBorderSize (juce::BorderSize<int> (0));
        return label;
    }

    juce::Slider::SliderLayout getSliderLayout (juce::Slider& slider) override
    {
        if (static_cast<bool> (slider.getProperties().getWithDefault ("runwayControl", false)))
        {
            juce::Slider::SliderLayout result;
            result.sliderBounds = slider.getLocalBounds().withTrimmedLeft (112).withTrimmedRight (48);
            return result;
        }

        if (static_cast<bool> (slider.getProperties().getWithDefault ("detailReadout", false)))
        {
            juce::Slider::SliderLayout result;
            const int diameter = juce::jmax (0, juce::jmin (slider.getWidth(), slider.getHeight() - 22));
            result.sliderBounds = { (slider.getWidth() - diameter) / 2, 0, diameter, diameter };
            result.textBoxBounds = { 0, slider.getHeight() - 18, slider.getWidth(), 18 };
            return result;
        }
        const bool isTiming = static_cast<bool> (slider.getProperties().getWithDefault ("timingKnob", false));
        const bool alignTiming = static_cast<bool> (slider.getProperties().getWithDefault ("alignTimingKnob", false))
                              || static_cast<bool> (slider.getProperties().getWithDefault ("knobLabelBelow", false));

        if (isTiming || alignTiming)
        {
            juce::Slider::SliderLayout result;
            const int side = juce::jmin (slider.getWidth(), slider.getHeight() - 10);
            const int diameter = juce::jmax (0, side);
            const int x = (slider.getWidth() - diameter) / 2;
            result.sliderBounds = juce::Rectangle<int> (x, 0, diameter, diameter);
            if (isTiming && slider.getTextBoxPosition() != juce::Slider::NoTextBox)
                result.textBoxBounds = juce::Rectangle<int> (0, slider.getHeight() - 16, slider.getWidth(), 16);
            return result;
        }

        if (static_cast<bool> (slider.getProperties().getWithDefault ("hidePercentReadout", false)))
        {
            juce::Slider::SliderLayout result;
            const int diameter = juce::jmin (slider.getWidth(), slider.getHeight());
            result.sliderBounds = juce::Rectangle<int> (diameter, diameter).withCentre (slider.getLocalBounds().getCentre());
            return result;
        }
        return juce::LookAndFeel_V4::getSliderLayout (slider);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool hover, bool down) override
    {
        const auto area = button.getLocalBounds().toFloat();
        const auto centre = area.getCentre();
        const float side = juce::jmax (8.0f, juce::jmin (area.getWidth(), area.getHeight()) - 20.0f);
        const auto lens = juce::Rectangle<float> (side, side).withCentre (centre);
        const auto bezel = lens.expanded (2.5f);
        const auto accent = button.findColour (juce::ToggleButton::tickColourId);
        const bool on = button.getToggleState();
        if (on)
        {
            const float radius = juce::jmin (area.getWidth(), area.getHeight()) * .49f;
            juce::ColourGradient spill (accent.withAlpha (down ? .34f : .46f), centre,
                                       accent.withAlpha (0.0f), centre.translated (radius, 0), true);
            spill.addColour (.52, accent.withAlpha (.18f));
            spill.addColour (.82, accent.withAlpha (.04f));
            g.setGradientFill (spill);
            g.fillEllipse (area.reduced (.5f));
        }
        g.setColour (juce::Colour (0xc0040705));
        g.fillRoundedRectangle (bezel.translated (0, 1.5f), 3);
        g.setGradientFill (juce::ColourGradient (juce::Colour (hover ? 0xff747466 : 0xff55594d),
            bezel.getTopLeft(), juce::Colour (0xff121810), bezel.getBottomRight(), false));
        g.fillRoundedRectangle (bezel, 3);
        g.setColour (juce::Colour (0xff080e0b));
        g.fillRoundedRectangle (lens.expanded (.8f), 2.4f);
        if (on)
        {
            const auto source = centre.translated (0, -side * .08f);
            juce::ColourGradient transmitted (accent.interpolatedWith (juce::Colours::white, .94f), source,
                accent.darker (down ? 2.1f : 1.8f), source.translated (side * .68f, 0), true);
            transmitted.addColour (.18, accent.interpolatedWith (juce::Colours::white, .72f));
            transmitted.addColour (.46, accent);
            transmitted.addColour (.78, accent.darker (.85f));
            g.setGradientFill (transmitted);
            g.fillRoundedRectangle (lens, 2);
            g.setColour (accent.withAlpha (.25f));
            g.drawRoundedRectangle (lens, 2, .6f);
        }
        else
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff252d27), lens.getTopLeft(),
                                                    juce::Colour (0xff0e1712), lens.getBottomRight(), false));
            g.fillRoundedRectangle (lens, 2);
        }
        juce::ColourGradient reflection (juce::Colours::white.withAlpha (on ? .22f : .09f),
            lens.getX(), lens.getY(), juce::Colours::transparentBlack,
            lens.getX(), lens.getY() + side * .48f, false);
        g.setGradientFill (reflection);
        g.fillRoundedRectangle (lens.reduced (1).withHeight (side * .35f), 1.5f);
        if (button.hasKeyboardFocus (true))
        {
            g.setColour (accent);
            g.drawRoundedRectangle (area.reduced (0.5f), 3, 0.8f);
        }
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int,
                       juce::ComboBox& box) override
    {
        auto bounds = juce::Rectangle<float> (0, 0, static_cast<float> (width), static_cast<float> (height)).reduced (0.5f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (bounds, 2.5f);
        g.setColour (box.hasKeyboardFocus (true) ? juce::Colour (0xffffdc9b)
                                               : box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (bounds, 2.5f, 0.7f);
        juce::Path arrow;
        arrow.addTriangle (width - 10.0f, height * 0.44f, width - 4.0f, height * 0.44f,
                           width - 7.0f, height * 0.62f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId));
        g.fillPath (arrow);
    }

    juce::Font getComboBoxFont (juce::ComboBox& box) override
    {
        return juce::Font (juce::FontOptions (box.getHeight() < 24 ? 11.5f : 13.5f));
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (3, 0, juce::jmax (0, box.getWidth() - 14), box.getHeight());
        label.setFont (getComboBoxFont (box));
        label.setBorderSize (juce::BorderSize<int> (0, 2, 0, 0));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& colour,
                                bool hover, bool down) override
    {
        if (static_cast<bool> (button.getProperties().getWithDefault ("lanePanelHitTarget", false)))
        {
            if (button.hasKeyboardFocus (true))
            {
                g.setColour (juce::Colour (0xffa5a58e));
                g.drawRoundedRectangle (button.getLocalBounds().toFloat().reduced (1), 5, 0.8f);
            }
            return;
        }
        const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (down ? colour.brighter (0.3f) : hover ? colour.brighter (0.13f) : colour);
        g.fillRoundedRectangle (bounds, 3);
        g.setColour (button.hasKeyboardFocus (true) ? juce::Colour (0xffffdda0) : juce::Colour (0xff77745f));
        g.drawRoundedRectangle (bounds, 3, 0.8f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int height) override
    {
        return juce::Font (juce::FontOptions (juce::jmin (13.0f, height * 0.50f)));
    }
};
