#include "PluginEditor.h"
#include "NightShiftTimeGrid.h"
#include <BinaryData.h>
#include <cmath>

namespace
{
constexpr int designWidth = 1672, designHeight = 941;
constexpr std::array<int, 7> runwayY {{ 190, 282, 369, 461, 551, 635, 725 }};
constexpr std::array<const char*, 7> roleNames {{ "+OCT", "+5TH", "+3RD", "UNISON", "-3RD", "-5TH", "-OCT" }};
constexpr std::array<const char*, 7> roundaboutNames {{ "+OCTAVE", "+5TH", "+3RD", "UNISON", "-3RD", "-5TH", "-OCTAVE" }};
constexpr std::array<const char*, 7> roleDescriptions {{ "Upper octave", "Diatonic upper fifth", "Diatonic upper third",
                                                                      "Unison", "Diatonic lower third", "Diatonic lower fifth", "Lower octave" }};
const std::array<juce::Colour, 7> laneColours {{ juce::Colour (0xffff4b45), juce::Colour (0xff7ce6e8),
    juce::Colour (0xffffc36d), juce::Colour (0xfffff3d6), juce::Colour (0xffffc36d),
    juce::Colour (0xff7ce6e8), juce::Colour (0xffff4b45) }};
const juce::Colour bright { 0xffe9e3ce }, muted { 0xffafa995 }, warm { 0xfff5d79e };
const juce::Colour inactiveText { 0xff72786a };

juce::Image resource (const char* name)
{
    int size = 0;
    if (const auto* data = BinaryData::getNamedResource (name, size))
        return juce::ImageFileFormat::loadFrom (data, static_cast<std::size_t> (size));
    return {};
}

void dimLeftCity (juce::Image& image, bool lightOverlay)
{
    if (! image.isValid()) return;
    image = image.createCopy();
    juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readWrite);
    const int end = juce::jmin (300, image.getWidth());
    for (int x = 0; x < end; ++x)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (x - 210.0f) / 90.0f);
        const float mask = 1.0f - t * t * (3.0f - 2.0f * t);
        for (int y = 0; y < image.getHeight(); ++y)
        {
            const auto c = pixels.getPixelColour (x, y);
            if (lightOverlay)
                pixels.setPixelColour (x, y, c.withMultipliedAlpha (1.0f - mask * .55f));
            else
            {
                const float highlight = juce::jlimit (0.0f, 1.0f, (c.getBrightness() - .06f) / .50f);
                const float gain = 1.0f - mask * (.28f + .30f * highlight);
                pixels.setPixelColour (x, y, juce::Colour::fromFloatRGBA (
                    c.getFloatRed() * gain, c.getFloatGreen() * gain, c.getFloatBlue() * gain, c.getFloatAlpha()));
            }
        }
    }
}

void text (juce::Graphics& g, const juce::String& value, juce::Rectangle<float> area,
           float size = 11, juce::Colour colour = bright, bool bold = false,
           juce::Justification align = juce::Justification::left)
{
    g.setColour (colour);
    g.setFont (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
    g.drawText (value, area, align, true);
}

void lightLine (juce::Graphics& g, float x1, float y1, float x2, float y2, juce::Colour colour, float power)
{
    juce::Path p;
    p.startNewSubPath (x1, y1);
    p.lineTo (x2, y2);
    NightShiftLookAndFeel::glowPath (g, p, colour, power, 1.25f);
}

void panel (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour accent, float power)
{
    g.setColour (juce::Colour (0xa0000000));
    g.fillRoundedRectangle (r.translated (2, 3), 5);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfa2c2e27), r.getX(), r.getY(),
                                            juce::Colour (0xfa191e19), r.getRight(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 5);
    g.setColour (juce::Colour (0xff777461));
    g.drawRoundedRectangle (r, 5, 0.7f);
    lightLine (g, r.getX() + 14, r.getY() + 2, r.getRight() - 14, r.getY() + 2, accent, power);
    lightLine (g, r.getX() + 5, r.getY() + 19, r.getX() + 5, r.getBottom() - 17, accent, power * 0.72f);
    for (const auto pos : { r.getTopLeft() + juce::Point<float> (6, 6), r.getBottomRight() - juce::Point<float> (6, 6) })
    {
        g.setColour (juce::Colour (0xff11150f));
        g.fillEllipse (pos.x - 1.8f, pos.y - 1.8f, 3.6f, 3.6f);
        g.setColour (juce::Colour (0xff8d8b72));
        g.drawLine (pos.x - 0.8f, pos.y + 0.8f, pos.x + 0.8f, pos.y - 0.8f, 0.6f);
    }
}

std::size_t displayFor (std::size_t lane)
{
    const auto found = std::find (OzoGlintEditor::displayLaneOrder.begin(), OzoGlintEditor::displayLaneOrder.end(), lane);
    return static_cast<std::size_t> (std::distance (OzoGlintEditor::displayLaneOrder.begin(), found));
}
}

void OzoGlintEditor::RunwaySlider::paint (juce::Graphics& g)
{
    if (! selected && ! isMouseOverOrDragging() && ! hasKeyboardFocus (true))
        return;
    const auto left = 112.0f, right = static_cast<float> (getWidth()) - 48.0f;
    const auto x = left + static_cast<float> (valueToProportionOfLength (getValue())) * (right - left);
    const auto colour = findColour (juce::Slider::rotarySliderFillColourId);
    g.setColour (colour.withAlpha (0.8f));
    g.drawLine (x, 5, x, static_cast<float> (getHeight()) - 5, 0.8f);
    juce::Path marker;
    marker.addTriangle (x - 4, 4, x + 4, 4, x, 9);
    g.fillPath (marker);
}

void OzoGlintEditor::RunwaySlider::mouseDown (const juce::MouseEvent& e)
{
    if (select) select (e.mods.isPopupMenu());
    if (! e.mods.isPopupMenu()) juce::Slider::mouseDown (e);
}

void OzoGlintEditor::RunwaySlider::mouseDoubleClick (const juce::MouseEvent&)
{
    if (select) select (true);
}

bool OzoGlintEditor::RunwaySlider::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::returnKey)
    {
        if (select) select (true);
        return true;
    }
    return juce::Slider::keyPressed (key);
}

double OzoGlintEditor::RunwaySlider::valueToProportionOfLength (double value)
{
    const auto index = juce::jlimit (0, 9, juce::roundToInt (value));
    return ozo::midi::noteBeats[static_cast<std::size_t> (index)] / 4.0;
}

double OzoGlintEditor::RunwaySlider::proportionOfLengthToValue (double proportion)
{
    const double beats = juce::jlimit (0.0, 4.0, proportion * 4.0);
    const auto it = std::min_element (ozo::midi::noteBeats.begin(), ozo::midi::noteBeats.end(),
        [beats] (double a, double b) { return std::abs (a - beats) < std::abs (b - beats); });
    return static_cast<double> (std::distance (ozo::midi::noteBeats.begin(), it));
}

OzoGlintEditor::OzoGlintEditor (OzoGlintProcessor& owner)
    : juce::AudioProcessorEditor (owner), processor (owner)
{
    setLookAndFeel (&lookAndFeel);
    setOpaque (true);
    addAndMakeVisible (surface);
    surface.setSize (designWidth, designHeight);
    surface.setWantsKeyboardFocus (false);
    surface.addAndMakeVisible (controlPanels);
    controlPanels.setBounds (0, 0, 488, designHeight);
    airport = resource ("airport_base_jpg");
    welcomeLights = resource ("airport_welcome_png");
    dimLeftCity (airport, false);
    dimLeftCity (welcomeLights, true);
    buildLampSprites();
    surface.addChildComponent (details);
    details.setBounds (512, 764, 1138, 158);

    setupSlider (amount, "amount", "Master amount", controlPanels, warm, 1);
    amount.setBounds (37, 77, 66, 63);
    amountAttachment = std::make_unique<SliderAttachment> (processor.getAPVTS(), "amount", amount);
    setupCombo (root, "root", "Key root", controlPanels, 2);
    setupCombo (scale, "scale", "Scale", controlPanels, 3);
    setupCombo (output, "outputMode", "Output routing", controlPanels, 4);
    scale.changeItemText (1, "Unset");
    root.setBounds (109, 79, 56, 26);
    scale.setBounds (171, 79, 119, 26);
    output.setBounds (109, 111, 181, 26);
    rootAttachment = std::make_unique<ComboAttachment> (processor.getAPVTS(), "root", root);
    scaleAttachment = std::make_unique<ComboAttachment> (processor.getAPVTS(), "scale", scale);
    outputAttachment = std::make_unique<ComboAttachment> (processor.getAPVTS(), "outputMode", output);

    for (std::size_t d = 0; d < 7; ++d)
    {
        const auto lane = displayLaneOrder[d];
        auto& row = rows[lane];
        const auto cy = runwayY[d];
        const auto colour = laneColours[d];
        const auto parameterID = [lane] (const char* suffix) { return OzoGlintProcessor::laneParameterID (lane, suffix); };
        const auto role = juce::String (roleDescriptions[d]);
        const auto firstID = parameterID ("first");
        row.enabled.setName (parameterID ("enabled"));
        row.enabled.setComponentID (row.enabled.getName());
        row.enabled.setTitle (role + " enabled");
        row.enabled.setTooltip ("Enable " + role.toLowerCase() + ". Disabling cancels future echoes; existing notes retain their NoteOff.");
        row.enabled.setWantsKeyboardFocus (true);
        row.enabled.setExplicitFocusOrder (10 + static_cast<int> (d) * 10);
        row.enabled.setColour (juce::ToggleButton::tickColourId, colour);
        row.enabled.setButtonText ({});
        row.enabled.setBounds (38, cy - 17, 36, 36);
        controlPanels.addAndMakeVisible (row.enabled);
        row.enabled.addKeyListener (this);
        row.enabledAttachment = std::make_unique<ButtonAttachment> (processor.getAPVTS(), row.enabled.getName(), row.enabled);
        enabledParameters[lane] = processor.getAPVTS().getRawParameterValue (row.enabled.getName());
        row.enabled.onClick = [this] { syncLighting (juce::Time::getMillisecondCounterHiRes()); };

        setupSlider (row.first, firstID, role + " first delay", controlPanels, colour, 11 + static_cast<int> (d) * 10);
        setupSlider (row.repeat, parameterID ("repeat"), role + " repeat", controlPanels, colour, 12 + static_cast<int> (d) * 10);
        setupSlider (row.feedback, parameterID ("feedback"), role + " feedback", controlPanels, colour, 13 + static_cast<int> (d) * 10);
        setupSlider (row.level, parameterID ("level"), role + " level", controlPanels, colour, 14 + static_cast<int> (d) * 10);
        row.first.setBounds (78, cy - 11, 49, 52);
        row.repeat.setBounds (134, cy - 11, 49, 52);
        row.feedback.setBounds (190, cy - 11, 49, 52);
        row.level.setBounds (246, cy - 11, 49, 52);
        for (auto* slider : { &row.first, &row.repeat, &row.feedback, &row.level })
            slider->getProperties().set ("alignTimingKnob", true);
        row.firstAttachment = std::make_unique<SliderAttachment> (processor.getAPVTS(), firstID, row.first);
        row.repeatAttachment = std::make_unique<SliderAttachment> (processor.getAPVTS(), parameterID ("repeat"), row.repeat);
        row.feedbackAttachment = std::make_unique<SliderAttachment> (processor.getAPVTS(), parameterID ("feedback"), row.feedback);
        row.levelAttachment = std::make_unique<SliderAttachment> (processor.getAPVTS(), parameterID ("level"), row.level);

        setupSlider (row.runway, firstID, role + " runway first-delay control", surface, colour, 15 + static_cast<int> (d) * 10);
        row.runway.setComponentID (firstID + "_runway");
        row.runway.getProperties().set ("runwayControl", true);
        row.runway.setSliderStyle (juce::Slider::LinearHorizontal);
        row.runway.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        row.runway.setScrollWheelEnabled (false);
        row.runway.setSliderSnapsToMousePosition (false);
        row.runway.setMouseDragSensitivity (936);
        row.runway.setBounds (488, cy - 19, 1096, 40);
        row.runway.setTooltip (role + ": drag to set First. Double-click, right-click or press Return for lane details.");
        row.runway.select = [this, lane] (bool open) { selectLane (lane, open); };
        row.runwayAttachment = std::make_unique<SliderAttachment> (processor.getAPVTS(), firstID, row.runway);
        row.detail.setButtonText ({});
        row.detail.setName ("details_lane" + juce::String (static_cast<int> (lane)));
        row.detail.setComponentID (row.detail.getName());
        row.detail.setTitle (role + " panel details");
        row.detail.setTooltip ("Click panel background for " + role.toLowerCase() + " details: Gate, Motion and Cycle.");
        row.detail.getProperties().set ("lanePanelHitTarget", true);
        row.detail.setColour (juce::TextButton::textColourOffId, colour);
        row.detail.setBounds (28, cy - 39, 276, 80);
        row.detail.setExplicitFocusOrder (16 + static_cast<int> (d) * 10);
        row.detail.onClick = [this, lane] { selectLane (lane, true); };
        controlPanels.addAndMakeVisible (row.detail);
        row.detail.toBack();
        row.detail.addKeyListener (this);

        row.detailEnabled.setName (parameterID ("enabled"));
        row.detailEnabled.setComponentID (parameterID ("enabled") + "_detail");
        row.detailEnabled.setTitle (role + " enabled");
        row.detailEnabled.setTooltip (role + " lane enable");
        row.detailEnabled.setColour (juce::ToggleButton::tickColourId, colour);
        row.detailEnabled.setWantsKeyboardFocus (true);
        row.detailEnabled.setExplicitFocusOrder (100);
        row.detailEnabled.addKeyListener (this);
        row.detailEnabled.onClick = [this] { syncLighting (juce::Time::getMillisecondCounterHiRes()); };
        row.detailEnabled.setBounds (40, 64, 44, 44);
        details.addAndMakeVisible (row.detailEnabled);
        row.detailEnabledAttachment = std::make_unique<ButtonAttachment> (processor.getAPVTS(), parameterID ("enabled"), row.detailEnabled);
        setupSlider (row.detailFirst, firstID, role + " first delay", details, colour, 101);
        setupSlider (row.detailRepeat, parameterID ("repeat"), role + " repeat", details, colour, 102);
        setupSlider (row.detailFeedback, parameterID ("feedback"), role + " feedback", details, colour, 103);
        setupSlider (row.detailLevel, parameterID ("level"), role + " level", details, colour, 104);
        row.detailFirst.setComponentID (firstID + "_detail");
        row.detailRepeat.setComponentID (parameterID ("repeat") + "_detail");
        row.detailFeedback.setComponentID (parameterID ("feedback") + "_detail");
        row.detailLevel.setComponentID (parameterID ("level") + "_detail");
        row.detailFirstAttachment = std::make_unique<SliderAttachment> (processor.getAPVTS(), firstID, row.detailFirst);
        row.detailRepeatAttachment = std::make_unique<SliderAttachment> (processor.getAPVTS(), parameterID ("repeat"), row.detailRepeat);
        row.detailFeedbackAttachment = std::make_unique<SliderAttachment> (processor.getAPVTS(), parameterID ("feedback"), row.detailFeedback);
        row.detailLevelAttachment = std::make_unique<SliderAttachment> (processor.getAPVTS(), parameterID ("level"), row.detailLevel);
        setupSlider (row.gate, parameterID ("gate"), role + " gate length", details, colour, 105);
        setupSlider (row.motion, parameterID ("motion"), role + " motion", details, colour, 106);
        setupSlider (row.cycle, parameterID ("cycle"), role + " motion cycle", details, colour, 107);
        int detailX = 142;
        for (auto* slider : { &row.detailFirst, &row.detailRepeat, &row.detailFeedback, &row.detailLevel,
                              &row.gate, &row.motion, &row.cycle })
        {
            slider->setBounds (detailX, 57, 78, 78);
            slider->getProperties().set ("alignTimingKnob", true);
            detailX += 130;
        }
        row.gateAttachment = std::make_unique<SliderAttachment> (processor.getAPVTS(), parameterID ("gate"), row.gate);
        row.motionAttachment = std::make_unique<SliderAttachment> (processor.getAPVTS(), parameterID ("motion"), row.motion);
        row.cycleAttachment = std::make_unique<SliderAttachment> (processor.getAPVTS(), parameterID ("cycle"), row.cycle);
    }
    const auto percentText = [] (juce::Slider& slider)
    {
        slider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100)) + "%"; };
        slider.valueFromTextFunction = [] (const juce::String& s) { return s.getDoubleValue() / 100.0; };
        slider.updateText();
    };
    const auto useDragReadout = [this] (juce::Slider& slider)
    {
        slider.getProperties().set ("hidePercentReadout", true);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.addMouseListener (this, false);
        slider.onDragStart = [this, knob = &slider]
        {
            activeKnob = knob;
            restorePanelOpacity (juce::Time::getMillisecondCounterHiRes());
            updateKnobTooltip (*knob, juce::Desktop::getMousePosition());
        };
        slider.onDragEnd = [this, knob = &slider]
        {
            if (activeKnob == knob)
            {
                activeKnob = nullptr;
                knobTooltip.setVisible (false);
            }
        };
    };
    percentText (amount);
    useDragReadout (amount);
    for (auto& row : rows)
    {
        for (auto* slider : { &row.feedback, &row.level, &row.motion, &row.detailFeedback, &row.detailLevel })
            percentText (*slider);
        for (auto* slider : { &row.first, &row.repeat, &row.feedback, &row.level })
            useDragReadout (*slider);
        for (auto* slider : { &row.detailFirst, &row.detailRepeat, &row.detailFeedback, &row.detailLevel,
                              &row.gate, &row.motion, &row.cycle })
        {
            slider->getProperties().set ("detailReadout", true);
            slider->setTextBoxStyle (juce::Slider::TextBoxBelow, true, 78, 18);
        }
    }
    closeDetails.setBounds (1028, 14, 94, 28);
    closeDetails.setExplicitFocusOrder (108);
    closeDetails.setComponentID ("closeDetails");
    closeDetails.onClick = [this] { details.setVisible (false); };
    details.addAndMakeVisible (closeDetails);
    selectLane (0, false);
    details.setVisible (false);

    presetBar.setComponentID ("presetBar");
    presetBar.setTitle ("Factory and user presets");
    presetBar.setWantsKeyboardFocus (true);
    presetBar.addKeyListener (this);
    presetBar.setExplicitFocusOrder (90);
    presetBar.setBounds (39, 808, 254, 65);
    controlPanels.addAndMakeVisible (presetBar);
    presetStatus.setBounds (32, 882, 456, 23);
    presetStatus.setFont (juce::FontOptions (13.5f));
    presetStatus.setColour (juce::Label::textColourId, warm);
    controlPanels.addAndMakeVisible (presetStatus);
    const auto refresh = processor.refreshUserPresets();
    if (refresh.failed()) showPresetMessage (refresh.getErrorMessage());
    refreshPresetBar();
    presetBar.onSelect = [this] (int index) { loadPreset (index); };
    presetBar.onSave = [this] { promptSavePreset(); };
    presetBar.onDelete = [this] (int index) { promptDeletePreset (index); };
    brandTitle.setText ("ozo NIGHTSHIFT", juce::dontSendNotification);
    brandTitle.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    brandTitle.setColour (juce::Label::textColourId, bright);
    brandTitle.setBorderSize (juce::BorderSize<int> (0));
    brandTitle.setBounds (42, 29, 253, 29);
    brandTitle.setInterceptsMouseClicks (false, false);
    brandTitle.setComponentID ("nightshiftBrand");
    surface.addAndMakeVisible (brandTitle);
    brandSubtitle.setText ("MIDI PARTICLE DELAY", juce::dontSendNotification);
    brandSubtitle.setFont (juce::FontOptions (11.5f));
    brandSubtitle.setColour (juce::Label::textColourId, muted);
    brandSubtitle.setBorderSize (juce::BorderSize<int> (0));
    brandSubtitle.setBounds (43, 61, 210, 15);
    brandSubtitle.setInterceptsMouseClicks (false, false);
    surface.addAndMakeVisible (brandSubtitle);
    knobTooltip.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    knobTooltip.setJustificationType (juce::Justification::centred);
    knobTooltip.setColour (juce::Label::backgroundColourId, juce::Colour (0xf31e241c));
    knobTooltip.setColour (juce::Label::textColourId, bright);
    knobTooltip.setColour (juce::Label::outlineColourId, warm.withAlpha (0.75f));
    knobTooltip.setComponentID ("knobValueTooltip");
    knobTooltip.setInterceptsMouseClicks (false, false);
    surface.addChildComponent (knobTooltip);

    setResizable (true, true);
    setResizeLimits (1137, 640, 2006, 1129);
    if (auto* constrainer = getConstrainer()) constrainer->setFixedAspectRatio (static_cast<double> (designWidth) / designHeight);
    setSize (1203, 678);
    frameTime = juce::Time::getMillisecondCounterHiRes();
    revision = processor.visualStateRevision();
    lastMousePosition = juce::Desktop::getMousePosition();
    restorePanelOpacity (frameTime);
    syncLighting (frameTime);
    startTimerHz (60);
}

OzoGlintEditor::~OzoGlintEditor()
{
    stopTimer();
    if (presetDialog)
    {
        presetDialog->setLookAndFeel (nullptr);
        presetDialog.reset();
    }
    if (sessionActive) processor.endVisualSession();
    setLookAndFeel (nullptr);
}

void OzoGlintEditor::setupSlider (juce::Slider& slider, const juce::String& id, const juce::String& title,
                                 juce::Component& parent, juce::Colour colour, int focus)
{
    slider.setName (id);
    slider.setComponentID (id);
    slider.setTitle (title);
    slider.setDescription (title);
    slider.setTooltip (title);
    slider.setWantsKeyboardFocus (true);
    slider.addKeyListener (this);
    slider.setExplicitFocusOrder (focus);
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2666667f,
                               juce::MathConstants<float>::pi * 2.7333333f, true);
    const bool timing = dynamic_cast<juce::AudioParameterChoice*> (processor.getAPVTS().getParameter (id)) != nullptr;
    slider.getProperties().set ("timingKnob", timing);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, timing ? 78 : 65, timing ? 16 : 19);
    slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
    slider.setVelocityBasedMode (false);
    slider.setMouseDragSensitivity (180);
    slider.setPopupDisplayEnabled (false, false, nullptr);
    if (auto* parameter = processor.getAPVTS().getParameter (id))
    {
        slider.setRange (parameter->getNormalisableRange().start, parameter->getNormalisableRange().end,
                         parameter->getNormalisableRange().interval);
        slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (parameter))
        {
            const auto names = choice->choices;
            slider.textFromValueFunction = [names] (double v) { return names[juce::jlimit (0, names.size() - 1, juce::roundToInt (v))]; };
            slider.valueFromTextFunction = [names] (const juce::String& s)
            {
                const int found = names.indexOf (s.trim(), true);
                return found >= 0 ? static_cast<double> (found) : 0.0;
            };
        }
        else
        {
            slider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100)) + "%"; };
            slider.valueFromTextFunction = [] (const juce::String& s) { return s.getDoubleValue() / 100.0; };
        }
    }
    parent.addAndMakeVisible (slider);
}

void OzoGlintEditor::setupCombo (juce::ComboBox& box, const juce::String& id, const juce::String& title,
                                juce::Component& parent, int focus)
{
    box.setName (id);
    box.setComponentID (id);
    box.setTitle (title);
    box.setTooltip (title);
    box.setWantsKeyboardFocus (true);
    box.addKeyListener (this);
    box.setExplicitFocusOrder (focus);
    box.setEditableText (false);
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (processor.getAPVTS().getParameter (id)))
        box.addItemList (p->choices, 1);
    parent.addAndMakeVisible (box);
}

void OzoGlintEditor::selectLane (std::size_t lane, bool showDetails)
{
    restorePanelOpacity (juce::Time::getMillisecondCounterHiRes());
    selectedLane = lane;
    for (std::size_t i = 0; i < rows.size(); ++i)
    {
        auto& row = rows[i];
        for (auto* component : std::array<juce::Component*, 8> {{ &row.detailEnabled, &row.detailFirst, &row.detailRepeat,
                &row.detailFeedback, &row.detailLevel, &row.gate, &row.motion, &row.cycle }})
            component->setVisible (i == lane);
        row.runway.selected = i == lane;
        row.runway.repaint();
    }
    if (showDetails)
    {
        details.setVisible (true);
        details.toFront (false);
    }
    details.repaint();
}

void OzoGlintEditor::Details::paint (juce::Graphics& g)
{
    const auto display = displayFor (owner.selectedLane);
    const auto accent = laneColours[display];
    panel (g, getLocalBounds().toFloat().reduced (1), accent, 0.8f);
    text (g, roleDescriptions[display], { 20, 10, 940, 25 }, 17, accent, true);
    text (g, "ENABLE", { 24, 40, 76, 17 }, 13, muted, false, juce::Justification::centred);
    const auto& row = owner.rows[owner.selectedLane];
    const std::array<const juce::Slider*, 7> knobs {{ &row.detailFirst, &row.detailRepeat, &row.detailFeedback,
        &row.detailLevel, &row.gate, &row.motion, &row.cycle }};
    constexpr std::array<const char*, 7> captions {{ "FIRST", "REPEAT", "FDBK", "LEVEL", "GATE", "MOTION", "CYCLE" }};
    for (std::size_t i = 0; i < knobs.size(); ++i)
        text (g, captions[i], { static_cast<float> (knobs[i]->getX()), 40,
               static_cast<float> (knobs[i]->getWidth()), 17 }, 13, muted, false, juce::Justification::centred);
    text (g, "Timing applies to new input strikes. Main-panel controls stay in sync.", { 24, 139, 1040, 16 }, 12, muted);
}

bool OzoGlintEditor::hasAirportResources() const noexcept
{
    return airport.isValid() && welcomeLights.isValid()
        && std::all_of (lampSprites.begin(), lampSprites.end(), [] (const auto& sprites)
            { return std::all_of (sprites.begin(), sprites.end(), [] (const auto& image) { return image.isValid(); }); });
}

void OzoGlintEditor::buildLampSprites()
{
    struct LightProfile
    {
        float coreRadius, coreAlpha, nearRadius, nearAlpha, midRadius, midAlpha, farRadius, farAlpha;
        float groundX, groundY, groundAlpha, surfaceGain;
    };
    constexpr std::array<LightProfile, 5> profiles {{
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 },
        { 1.65f, .78f, 4.5f, .36f, 9.0f, .15f, 14.0f, .060f, 18, 12, .085f, 1.75f },
        { 2.15f, .96f, 6.8f, .62f, 13.5f, .26f, 22.0f, .115f, 30, 20, .19f, 2.25f },
        { 2.70f, 1.0f, 9.0f, .82f, 18.0f, .40f, 28.0f, .19f, 44, 28, .32f, 2.85f },
        { 3.20f, 1.0f, 11.0f, .96f, 21.0f, .53f, 31.0f, .26f, 46, 30, .40f, 3.35f }
    }};
    for (std::size_t d = 0; d < lampSprites.size(); ++d)
        for (std::size_t tier = 0; tier < profiles.size(); ++tier)
        {
            auto& image = lampSprites[d][tier];
            image = juce::Image (juce::Image::ARGB, 128, 128, true);
            if (tier == 0) continue;
            const auto& profile = profiles[tier];
            const auto colour = laneColours[d];
            const auto pale = colour.interpolatedWith (juce::Colour (0xfffff7e7), .76f);
            juce::Graphics light (image);
            light.addTransform (juce::AffineTransform::scale (2.0f));
            const auto bloom = [&] (float radius, float alpha, juce::Colour tint)
            {
                juce::ColourGradient gradient (tint.withAlpha (alpha), 32, 32,
                                               tint.withAlpha (0.0f), 32 + radius, 32, true);
                gradient.addColour (.30, tint.withAlpha (alpha * .58f));
                gradient.addColour (.68, tint.withAlpha (alpha * .16f));
                light.setGradientFill (gradient);
                light.fillEllipse (32 - radius, 32 - radius, radius * 2, radius * 2);
            };
            bloom (profile.farRadius, profile.farAlpha, pale);
            bloom (profile.midRadius, profile.midAlpha, colour.interpolatedWith (pale, .45f));
            bloom (profile.nearRadius, profile.nearAlpha, colour);
            const float radius = profile.coreRadius;
            light.setColour (colour.interpolatedWith (juce::Colours::white, .30f).withAlpha (profile.coreAlpha));
            light.fillEllipse (32 - radius, 32 - radius, radius * 2, radius * 2);
            const float hotRadius = .55f + .17f * static_cast<float> (tier);
            light.setColour (colour.interpolatedWith (juce::Colours::white, .88f).withAlpha (profile.coreAlpha));
            light.fillEllipse (32 - hotRadius, 32 - hotRadius, hotRadius * 2, hotRadius * 2);
            if (tier >= 3)
            {
                light.setColour (pale.withAlpha (tier == 4 ? .82f : .55f));
                const float length = tier == 4 ? 8.0f : 5.5f;
                light.drawLine (32 - length, 32, 32 + length, 32, .45f);
                light.drawLine (32, 32 - length * .55f, 32, 32 + length * .55f, .30f);
            }

            if (! airport.isValid()) continue;
            for (std::size_t cell = 0; cell < groundLightSprites[d].size(); ++cell)
            {
                auto& ground = groundLightSprites[d][cell][tier];
                ground = juce::Image (juce::Image::ARGB, 96, 64, true);
                juce::Image::BitmapData pixels (ground, juce::Image::BitmapData::writeOnly);
                const int left = juce::roundToInt (600.0 + 936.0 * cell / 64.0) - 48;
                const int top = runwayY[d] - 32;
                // Cache illuminated terrain itself: the ground pass keeps asphalt and paint texture.
                for (int y = 0; y < 64; ++y)
                    for (int x = 0; x < 96; ++x)
                    {
                        const float dx = (x - 48.0f) / profile.groundX;
                        const float dy = (y - 32.0f) / profile.groundY;
                        const float edge = juce::jlimit (0.0f, 1.0f,
                            std::min ({ x, 95 - x, y, 63 - y }) / 5.0f);
                        const float alpha = profile.groundAlpha * std::exp (-2.0f * (dx * dx + dy * dy)) * edge;
                        if (alpha < .002f) continue;
                        const auto surfaceColour = airport.getPixelAt (left + x, top + y);
                        const auto channel = [&] (float value, float tint)
                        {
                            return juce::jlimit (0.0f, 1.0f, value * profile.surfaceGain + tint * .018f);
                        };
                        pixels.setPixelColour (x, y, juce::Colour::fromFloatRGBA (
                            channel (surfaceColour.getFloatRed(), pale.getFloatRed()),
                            channel (surfaceColour.getFloatGreen(), pale.getFloatGreen()),
                            channel (surfaceColour.getFloatBlue(), pale.getFloatBlue()), alpha));
                    }
            }
        }
}

void OzoGlintEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff080b0a));
}

void OzoGlintEditor::resized()
{
    const float s = juce::jmin (getWidth() / static_cast<float> (designWidth), getHeight() / static_cast<float> (designHeight));
    surface.setTransform (juce::AffineTransform::scale (s).translated ((getWidth() - designWidth * s) * 0.5f,
                                                                    (getHeight() - designHeight * s) * 0.5f));
}

void OzoGlintEditor::updateLampAfterglow (double now)
{
    using Level = glint::NightShiftLighting::Level;
    const auto tierIndex = [] (Level level)
    {
        switch (level)
        {
            case Level::base: return 1.0f;
            case Level::mark: return 2.0f;
            case Level::echo: return 3.0f;
            case Level::peak: return 4.0f;
            default: return 0.0f;
        }
    };
    const double elapsed = lastLampUpdate > 0 ? juce::jmax (0.0, now - lastLampUpdate) : 16.0;
    lastLampUpdate = now;
    for (std::size_t lane = 0; lane < rows.size(); ++lane)
    {
        const auto state = lighting.sample (lane, now);
        const bool switchedOff = previousLampEnabled[lane] && ! state.enabled;
        previousLampEnabled[lane] = state.enabled;
        if (switchedOff) lampAfterglow[lane] = {};
        for (int cell = 0; cell < glint::NightShiftTimeGrid::positionCount; ++cell)
        {
            const auto levels = glint::NightShiftLighting::cellLevels (state, cell,
                juce::roundToInt (rows[lane].first.getValue()), juce::roundToInt (rows[lane].repeat.getValue()));
            auto& visual = lampAfterglow[lane][static_cast<std::size_t> (cell)];
            const auto follow = [elapsed] (float current, float target)
            {
                if (elapsed > 1200.0) return target;
                const double response = target > current ? 35.0 : 300.0;
                const float value = current + (target - current) * static_cast<float> (1.0 - std::exp (-elapsed / response));
                return std::abs (value - target) < .025f ? target : value;
            };
            visual.edge = follow (visual.edge, tierIndex (levels.edge));
            visual.center = follow (visual.center, tierIndex (levels.center));
        }
    }
}

void OzoGlintEditor::paintLights (juce::Graphics& g, double now)
{
    using Grid = glint::NightShiftTimeGrid;
    using Level = glint::NightShiftLighting::Level;
    const auto blendTiers = [&] (float value, float opacity, const auto& draw)
    {
        value = juce::jlimit (0.0f, 4.0f, value);
        const int upper = static_cast<int> (std::ceil (value));
        if (upper == 0) return;
        const int lower = upper - 1;
        const float mix = value - lower;
        if (lower > 0 && mix < 1.0f)
        {
            g.setOpacity (opacity * (1.0f - mix));
            draw (static_cast<std::size_t> (lower));
        }
        g.setOpacity (opacity * mix);
        draw (static_cast<std::size_t> (upper));
        g.setOpacity (1.0f);
    };
    for (std::size_t d = 0; d < 7; ++d)
    {
        const auto lane = displayLaneOrder[d];
        const auto state = lighting.sample (lane, now);
        const auto cy = static_cast<float> (runwayY[d]);
        const auto& row = rows[lane];
        const int first = juce::roundToInt (row.first.getValue()), repeat = juce::roundToInt (row.repeat.getValue());
        for (int cell = 0; cell < Grid::positionCount; ++cell)
        {
            const auto& visual = lampAfterglow[lane][static_cast<std::size_t> (cell)];
            const int x = juce::roundToInt (600.0f + 936.0f * Grid::normalizedX (cell));
            blendTiers (juce::jmax (visual.edge, visual.center), 1.0f, [&] (std::size_t tier)
            {
                g.drawImageAt (groundLightSprites[d][static_cast<std::size_t> (cell)][tier], x - 48, runwayY[d] - 32);
            });
        }
        const auto drawLamp = [&] (float x, float y, float visualTier, float width, float height, float opacity = 1.0f)
        {
            const float paddedWidth = width * 3.2f, paddedHeight = height * 3.2f;
            blendTiers (visualTier, opacity, [&] (std::size_t tier)
            {
                g.drawImage (lampSprites[d][tier],
                             juce::Rectangle<float> (x - paddedWidth * .5f, y - paddedHeight * .5f,
                                                     paddedWidth, paddedHeight));
            });
        };
        for (int cell = 0; cell < Grid::positionCount; ++cell)
        {
            const float x = 600.0f + 936.0f * Grid::normalizedX (cell);
            const auto levels = glint::NightShiftLighting::cellLevels (state, cell, first, repeat);
            const bool beatBoundary = cell % Grid::stepsPerBeat == 0;
            const bool sixteenth = cell % (Grid::stepsPerBeat / 4) == 0;
            const float edgeWidth = sixteenth ? 18.0f : 9.0f;
            const float edgeHeight = beatBoundary ? 30.0f : sixteenth ? 13.0f : 9.0f;
            const auto& visual = lampAfterglow[lane][static_cast<std::size_t> (cell)];
            drawLamp (x, cy - 12, visual.edge, edgeWidth, edgeHeight);
            drawLamp (x, cy + 12, visual.edge, edgeWidth, edgeHeight);

            const bool firstTarget = cell == Grid::choiceCell (first);
            const float centreWidth = levels.echoActualCell ? 23.0f : firstTarget ? 20.0f : 24.0f;
            const float centreHeight = levels.echoActualCell ? 23.0f : firstTarget ? 24.0f : 16.0f;
            drawLamp (x, cy, visual.center, centreWidth, centreHeight);
        }
        // Entry acknowledgement is separate from the time-grid echo position.
        float entryFlash = 0.0f;
        const auto release = [] (double age)
        {
            const float t = juce::jlimit (0.0f, 1.0f, static_cast<float> ((age - 90.0) / 610.0));
            return 1.0f - t * t * (3.0f - 2.0f * t);
        };
        if (state.enabled && state.enableProgress >= 0.0f)
            entryFlash = release (state.enableProgress * glint::NightShiftLighting::enableMs);
        if (state.enabled)
            for (std::size_t echo = 0; echo < state.echoCount; ++echo)
                entryFlash = juce::jmax (entryFlash, release (state.echoes[echo].progress * glint::NightShiftLighting::echoMs));
        if (entryFlash > 0.0f)
        {
            g.setOpacity (entryFlash);
            g.drawImageAt (groundLightSprites[d][0][4], 552, runwayY[d] - 32);
            drawLamp (600, cy - 12, 4.0f, 25, 34, entryFlash);
            drawLamp (600, cy, 4.0f, 27, 30, entryFlash);
            drawLamp (600, cy + 12, 4.0f, 25, 34, entryFlash);
            g.setOpacity (1.0f);
        }
    }
    g.setOpacity (1.0f);
    const auto progress = lighting.welcomeProgress (now);
    if (progress >= 0 && welcomeLights.isValid())
    {
        const float centre = static_cast<float> (progress) * designWidth;
        for (int x = 0; x < designWidth; x += 24)
        {
            const auto a = 0.72f * std::exp (-0.5f * std::pow ((x + 12.0f - centre) / 105.0f, 2.0f));
            if (a < 0.005f) continue;
            const int width = juce::jmin (24, designWidth - x);
            g.setOpacity (a);
            g.drawImage (welcomeLights, x, 0, width, designHeight, x, 0, width, designHeight);
        }
    }
    g.setOpacity (1.0f);
}

void OzoGlintEditor::paintScene (juce::Graphics& g)
{
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    if (airport.isValid()) g.drawImageAt (airport, 0, 0);
    else g.fillAll (juce::Colour (0xff101611));
    paintLights (g, frameTime);
    const auto mask = enabledMask();
    for (std::size_t d = 0; d < runwayY.size(); ++d)
    {
        if ((mask & (1u << displayLaneOrder[d])) == 0) continue;
        const float cy = static_cast<float> (runwayY[d]);
        const auto bounds = juce::Rectangle<float> (494, cy - 10, 90, 20);
        const auto colour = laneColours[d];
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.setColour (colour.withAlpha (.055f));
        for (const auto offset : { juce::Point<float> (-1, 0), juce::Point<float> (1, 0),
                                   juce::Point<float> (0, -1), juce::Point<float> (0, 1) })
            g.drawText (roundaboutNames[d], bounds.translated (offset.x, offset.y), juce::Justification::centred);
        g.setColour (colour.withAlpha (.52f));
        g.drawText (roundaboutNames[d], bounds, juce::Justification::centred);
    }
    panel (g, { 28, 22, 276, 55 }, warm, 0.78f);
    if (! details.isVisible())
    {
        g.setColour (juce::Colour (0xc5101713));
        g.fillRoundedRectangle (1260, 846, 358, 66, 4);
        text (g, "NIGHT OPERATIONS", { 1275, 853, 325, 20 }, 13, bright);
        text (g, "Click role / double-click runway for details", { 1275, 877, 325, 15 }, 10, muted);
        text (g, processor.isOutputModePending() ? "ROUTING CHANGE PENDING / STOP TO APPLY"
              : "IDLE BEACON + MIDI-DRIVEN ECHO SWEEP", { 1275, 895, 325, 13 }, 9, muted);
    }
}

void OzoGlintEditor::paintControlPanels (juce::Graphics& g)
{
    const auto welcome = lighting.welcomeProgress (frameTime);
    const float panelWelcome = welcome >= 0 ? std::exp (-0.5f * std::pow ((static_cast<float> (welcome) * designWidth - 120) / 120, 2.0f)) : 0;
    panel (g, { 28, 77, 276, 65 }, warm, 0.70f + 0.30f * panelWelcome);
    const auto mask = enabledMask();
    for (std::size_t d = 0; d < 7; ++d)
    {
        const auto lane = displayLaneOrder[d];
        const bool enabled = (mask & (1u << lane)) != 0;
        const auto cy = static_cast<float> (runwayY[d]);
        const auto colour = enabled ? laneColours[d] : inactiveText;
        const auto labelColour = enabled ? bright : inactiveText;
        panel (g, { 28, cy - 39, 276, 80 }, laneColours[d], enabled ? 0.84f + lightBoost[lane] * 0.16f : 0.0f);
        text (g, d == 3 ? "UNI" : roleNames[d], { 34, cy - 35, 40, 19 }, 12, colour, false, juce::Justification::centred);
        text (g, "FIRST", { 78, cy - 35, 49, 15 }, 12, labelColour, false, juce::Justification::centred);
        text (g, "REPEAT", { 132, cy - 35, 53, 15 }, 11.5f, labelColour, false, juce::Justification::centred);
        text (g, "FDBK", { 190, cy - 35, 49, 15 }, 12, labelColour, false, juce::Justification::centred);
        text (g, "LEVEL", { 246, cy - 35, 49, 15 }, 12, labelColour, false, juce::Justification::centred);
    }
    panel (g, { 28, 783, 276, 97 }, warm, 0.75f);
    text (g, "PRESETS", { 41, 791, 243, 17 }, 12.5f, muted);
}

void OzoGlintEditor::showPresetMessage (const juce::String& message)
{
    presetStatus.setText (message, juce::dontSendNotification);
    presetMessageUntil = juce::Time::getMillisecondCounterHiRes() + 5000;
    restorePanelOpacity (juce::Time::getMillisecondCounterHiRes());
}

void OzoGlintEditor::refreshPresetBar()
{
    std::vector<ozo::PresetEntry> entries;
    entries.reserve (static_cast<std::size_t> (ozo::PresetFactory::kFactoryCount + processor.getUserPresets().size()));
    for (int index = 0; index < ozo::PresetFactory::kFactoryCount; ++index)
        entries.push_back ({ ozo::PresetFactory::getName (index), true, ozo::PresetFactory::isWild (index) });
    for (int index = 0; index < processor.getUserPresets().size(); ++index)
        entries.push_back ({ processor.getUserPresets().get (index).name, false, false });
    lastPresetIndex = processor.getPresetIndex();
    presetBar.setEntries (std::move (entries), lastPresetIndex);
}

void OzoGlintEditor::loadPreset (int index)
{
    const auto result = processor.applyPreset (index);
    if (result.failed())
    {
        refreshPresetBar();
        showPresetMessage (result.getErrorMessage());
        return;
    }
    syncLighting (juce::Time::getMillisecondCounterHiRes());
    refreshPresetBar();
    showPresetMessage (index < ozo::PresetFactory::kFactoryCount ? ozo::PresetFactory::getHint (index) : "User preset loaded");
}

void OzoGlintEditor::promptSavePreset()
{
    if (presetDialog && presetDialog->isCurrentlyModal()) return;
    restorePanelOpacity (juce::Time::getMillisecondCounterHiRes());
    presetDialog = std::make_unique<juce::AlertWindow> ("Save preset", "Save all lane and global settings on this computer.",
                                                       juce::MessageBoxIconType::NoIcon, this);
    presetDialog->setLookAndFeel (&lookAndFeel);
    presetDialog->addTextEditor ("name", "", "Preset name");
    presetDialog->getTextEditor ("name")->setInputRestrictions (64);
    presetDialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    presetDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    const juce::Component::SafePointer<OzoGlintEditor> safe (this);
    presetDialog->enterModalState (true, juce::ModalCallbackFunction::create ([safe] (int result)
    {
        if (! safe || ! safe->presetDialog) return;
        const auto name = safe->presetDialog->getTextEditorContents ("name").trim();
        safe->presetDialog->setVisible (false);
        if (result == 1) safe->savePreset (name, false);
    }), false);
}

void OzoGlintEditor::savePreset (const juce::String& name, bool overwrite)
{
    if (! overwrite)
    {
        const auto result = processor.refreshUserPresets();
        if (result.failed()) { showPresetMessage (result.getErrorMessage()); return; }
        for (int index = 0; index < ozo::PresetFactory::kFactoryCount; ++index)
            if (juce::String (ozo::PresetFactory::getName (index)).equalsIgnoreCase (name))
            {
                showPresetMessage ("Choose another name; factory presets are read-only");
                return;
            }
        if (processor.getUserPresets().indexOfName (name) >= 0)
        {
            const juce::Component::SafePointer<OzoGlintEditor> safe (this);
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Replace preset?",
                "Replace the saved preset \"" + name + "\"?", "Replace", "Cancel", this,
                juce::ModalCallbackFunction::create ([safe, name] (int choice)
                {
                    if (safe && choice == 1) safe->savePreset (name, true);
                }));
            return;
        }
    }
    const auto result = processor.saveUserPreset (name, overwrite);
    if (result.failed()) { showPresetMessage (result.getErrorMessage()); return; }
    refreshPresetBar();
    showPresetMessage ("Saved " + name);
}

void OzoGlintEditor::promptDeletePreset (int index)
{
    const int user = index - ozo::PresetFactory::kFactoryCount;
    if (user < 0 || user >= processor.getUserPresets().size()) return;
    const auto name = processor.getUserPresets().get (user).name;
    const juce::Component::SafePointer<OzoGlintEditor> safe (this);
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Delete preset?",
        "Delete \"" + name + "\" from user presets? Current settings will not change.", "Delete", "Cancel", this,
        juce::ModalCallbackFunction::create ([safe, name] (int choice)
        {
            if (! safe || choice != 1) return;
            auto result = safe->processor.refreshUserPresets();
            if (result.failed()) { safe->showPresetMessage (result.getErrorMessage()); return; }
            const int current = safe->processor.getUserPresets().indexOfName (name);
            if (current < 0) { safe->showPresetMessage ("Preset no longer exists"); safe->refreshPresetBar(); return; }
            result = safe->processor.deleteUserPreset (current + ozo::PresetFactory::kFactoryCount);
            if (result.failed()) { safe->showPresetMessage (result.getErrorMessage()); return; }
            safe->refreshPresetBar();
            safe->showPresetMessage ("Deleted " + name);
        }));
}

void OzoGlintEditor::updateKnobTooltip (juce::Slider& slider, juce::Point<int> screenPosition)
{
    const auto mouse = surface.getLocalPoint (nullptr, screenPosition);
    constexpr int width = 76, height = 28;
    int x = mouse.x + 16, y = mouse.y - height - 12;
    if (x + width > designWidth - 8) x = mouse.x - width - 16;
    if (y < 8) y = mouse.y + 18;
    knobTooltip.setBounds (juce::jlimit (8, designWidth - width - 8, x),
                           juce::jlimit (8, designHeight - height - 8, y), width, height);
    knobTooltip.setText (slider.getTextFromValue (slider.getValue()), juce::dontSendNotification);
    knobTooltip.setVisible (true);
    knobTooltip.toFront (false);
}

void OzoGlintEditor::mouseDrag (const juce::MouseEvent& event)
{
    if (activeKnob != nullptr) updateKnobTooltip (*activeKnob, event.getScreenPosition());
}

void OzoGlintEditor::mouseUp (const juce::MouseEvent&)
{
    activeKnob = nullptr;
    knobTooltip.setVisible (false);
}

void OzoGlintEditor::restorePanelOpacity (double now)
{
    lastPanelActivity = lastPanelUpdate = now;
    panelOpacity = 1.0f;
    controlPanels.setAlpha (panelOpacity);
}

bool OzoGlintEditor::keyPressed (const juce::KeyPress&, juce::Component*)
{
    restorePanelOpacity (juce::Time::getMillisecondCounterHiRes());
    return false;
}

void OzoGlintEditor::updatePanelOpacity (double now)
{
    const auto mousePosition = juce::Desktop::getMousePosition();
    const auto local = surface.getLocalPoint (nullptr, mousePosition).toFloat();
    const bool moved = mousePosition != lastMousePosition;
    lastMousePosition = mousePosition;
    const bool nearPanel = juce::Rectangle<float> (28, 22, 276, 852).expanded (90).contains (local);
    const bool menuOpen = root.isPopupActive() || scale.isPopupActive() || output.isPopupActive() || presetBar.isPopupActive();
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    const bool editingText = dynamic_cast<juce::TextEditor*> (focused) != nullptr
        && (controlPanels.isParentOf (focused) || details.isParentOf (focused));
    if (! sessionActive || moved || nearPanel || details.isVisible() || menuOpen || editingText
        || juce::ModifierKeys::getCurrentModifiersRealtime().isAnyMouseButtonDown()
        || (presetDialog && presetDialog->isVisible()) || presetMessageUntil > now || now < lastPanelUpdate)
    {
        restorePanelOpacity (now);
        return;
    }
    const float progress = juce::jlimit (0.0f, 1.0f, static_cast<float> ((now - lastPanelActivity - 2000.0) / 1100.0));
    const float next = 1.0f - .65f * progress * progress * (3.0f - 2.0f * progress);
    lastPanelUpdate = now;
    if (std::abs (next - panelOpacity) > .001f)
    {
        panelOpacity = next;
        controlPanels.setAlpha (panelOpacity);
    }
}

std::uint8_t OzoGlintEditor::enabledMask() const noexcept
{
    std::uint8_t mask = 0;
    for (std::size_t i = 0; i < enabledParameters.size(); ++i)
        if (enabledParameters[i] != nullptr && enabledParameters[i]->load (std::memory_order_relaxed) > 0.5f)
            mask = static_cast<std::uint8_t> (mask | (1u << i));
    return mask;
}

void OzoGlintEditor::updateVisibilitySession()
{
    const bool showing = isShowing();
    if (showing && ! sessionActive)
    {
        processor.beginVisualSession();
        sessionActive = true;
        revision = processor.visualStateRevision();
        hasPendingEvent = false;
        frameTime = juce::Time::getMillisecondCounterHiRes();
        lampAfterglow = {};
        previousLampEnabled = {};
        lastLampUpdate = 0;
        lighting.start (frameTime, enabledMask(), revision);
        lastMousePosition = juce::Desktop::getMousePosition();
        restorePanelOpacity (frameTime);
    }
    else if (! showing && sessionActive)
    {
        processor.endVisualSession();
        sessionActive = false;
        hasPendingEvent = false;
        lighting.stop();
        lampAfterglow = {};
        lastLampUpdate = 0;
        activeKnob = nullptr;
        knobTooltip.setVisible (false);
    }
}

void OzoGlintEditor::visibilityChanged()
{
    updateVisibilitySession();
}

void OzoGlintEditor::syncLighting (double now)
{
    const auto next = processor.visualStateRevision();
    if (next != revision)
    {
        revision = next;
        hasPendingEvent = false;
        lastVelocity.fill (0);
        lastOnsetBeats.fill (0);
        lampAfterglow = {};
        previousLampEnabled = {};
        lastLampUpdate = 0;
        processor.discardLaneVisualEvents();
    }
    frameTime = now;
    const auto currentMask = enabledMask();
    for (std::size_t lane = 0; lane < rows.size(); ++lane)
    {
        const bool enabled = (currentMask & (1u << lane)) != 0;
        for (auto* knob : { &rows[lane].first, &rows[lane].repeat, &rows[lane].gate, &rows[lane].cycle,
                            &rows[lane].detailFirst, &rows[lane].detailRepeat, &rows[lane].detailFeedback,
                            &rows[lane].detailLevel, &rows[lane].motion })
        {
            knob->getProperties().set ("laneLit", enabled);
            const auto colour = enabled ? bright : inactiveText;
            if (knob->findColour (juce::Slider::textBoxTextColourId) == colour) continue;
            knob->setColour (juce::Slider::textBoxTextColourId, colour);
            knob->repaint();
        }
    }
    if (sessionActive)
    {
        lighting.syncEnabled (enabledMask(), revision, now);
        lighting.update (now);
        const auto mask = enabledMask();
        for (std::size_t lane = 0; lane < rows.size(); ++lane)
            if ((mask & (1u << lane)) == 0 && previousLampEnabled[lane])
            {
                lampAfterglow[lane] = {};
                previousLampEnabled[lane] = false;
            }
    }
}

void OzoGlintEditor::timerCallback()
{
    updateVisibilitySession();
    ++editorFrameCount;
    const auto nextFrameTime = juce::Time::getMillisecondCounterHiRes();
    if (nextFrameTime < frameTime)
    {
        processor.discardLaneVisualEvents();
        hasPendingEvent = false;
    }
    frameTime = nextFrameTime;
    syncLighting (frameTime);
    if (sessionActive)
    {
        for (int n = 0; n < 256; ++n)
        {
            if (! hasPendingEvent) hasPendingEvent = processor.popLaneVisualEvent (pendingEvent);
            if (! hasPendingEvent) break;
            if (pendingEvent.stateRevision != revision) { hasPendingEvent = false; continue; }
            if (pendingEvent.presentationTimeMs > frameTime) break;
            if (pendingEvent.laneIndex < 7)
            {
                const auto lane = static_cast<std::size_t> (pendingEvent.laneIndex);
                if (lighting.echo (lane, pendingEvent.velocity / 127.0f, pendingEvent.presentationTimeMs,
                                   frameTime, pendingEvent.onsetBeats))
                {
                    lastVelocity[lane] = pendingEvent.velocity;
                    lastOnsetBeats[lane] = pendingEvent.onsetBeats;
                }
            }
            hasPendingEvent = false;
        }
    }
    // A host can restore state during the drain; invalidate that frame as well.
    syncLighting (frameTime);
    if (sessionActive) updateLampAfterglow (frameTime);
    const auto mask = enabledMask();
    const auto wp = lighting.welcomeProgress (frameTime);
    const float welcome = wp >= 0 ? std::exp (-0.5f * std::pow ((static_cast<float> (wp) * designWidth - 130) / 120, 2.0f)) : 0;
    for (std::size_t lane = 0; lane < 7; ++lane)
    {
        const bool enabled = (mask & (1u << lane)) != 0;
        const auto state = lighting.sample (lane, frameTime);
        lightBoost[lane] = juce::jmax (state.welcomeProgress >= 0 ? welcome : 0.0f,
                                      state.enableProgress >= 0 ? 1.0f - state.enableProgress : 0.0f);
        for (auto* slider : { &rows[lane].first, &rows[lane].repeat, &rows[lane].feedback, &rows[lane].level,
                              &rows[lane].gate, &rows[lane].motion, &rows[lane].cycle, &rows[lane].detailFirst,
                              &rows[lane].detailRepeat, &rows[lane].detailFeedback, &rows[lane].detailLevel })
        {
            slider->getProperties().set ("laneLit", enabled);
            slider->getProperties().set ("lightBoost", lightBoost[lane]);
            if (slider->isShowing()) slider->repaint();
        }
    }
    amount.getProperties().set ("lightBoost", welcome);
    if (presetMessageUntil > 0 && frameTime >= presetMessageUntil)
    {
        presetStatus.setText ({}, juce::dontSendNotification);
        presetMessageUntil = 0;
    }
    if (processor.getPresetIndex() != lastPresetIndex)
        refreshPresetBar();
    updatePanelOpacity (frameTime);
    surface.repaint();
}
