#include "PluginEditor.h"

namespace
{
const juce::Colour background { 0xff202226 };
const juce::Colour surface { 0xff2d3035 };
const juce::Colour text { 0xffeeeff1 };
const juce::Colour secondaryText { 0xffb7bbc3 };
const juce::Colour outline { 0xff555b64 };
const juce::Colour accent { 0xffbdd5e8 };
}

OzoMidiDelayEditor::OzoMidiDelayEditor (OzoMidiDelayProcessor& owner)
    : juce::AudioProcessorEditor (owner), processor (owner)
{
    setOpaque (true);
    setResizable (false, false);

    const auto setupLabel = [this] (juce::Label& label, const juce::String& caption)
    {
        label.setText (caption, juce::dontSendNotification);
        label.setColour (juce::Label::textColourId, secondaryText);
        label.setFont (juce::FontOptions (14.0f));
        label.setBorderSize (juce::BorderSize<int> (0));
        addAndMakeVisible (label);
    };

    setupLabel (titleLabel, "ozo MIDI Delay");
    titleLabel.setFont (juce::FontOptions (25.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, text);
    setupLabel (amountLabel, "Amount");
    amountLabel.setJustificationType (juce::Justification::centred);
    setupLabel (characterLabel, "Character");
    setupLabel (outputLabel, "Output");
    setupLabel (hintLabel, "Place before an instrument. MIDI only.");
    hintLabel.setFont (juce::FontOptions (11.0f));
    setupLabel (statusLabel, {});
    statusLabel.setFont (juce::FontOptions (11.0f));
    statusLabel.setJustificationType (juce::Justification::centredRight);

    amountSlider.setName ("Amount");
    amountSlider.setTitle ("Amount");
    amountSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    amountSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 26);
    amountSlider.setRange (0.0, 1.0);
    amountSlider.setValue (0.30, juce::dontSendNotification);
    amountSlider.setDoubleClickReturnValue (true, 0.30);
    amountSlider.setWantsKeyboardFocus (true);
    amountSlider.setExplicitFocusOrder (1);
    amountSlider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    amountSlider.setColour (juce::Slider::rotarySliderOutlineColourId, surface);
    amountSlider.setColour (juce::Slider::thumbColourId, text);
    amountSlider.setColour (juce::Slider::textBoxTextColourId, text);
    amountSlider.setColour (juce::Slider::textBoxBackgroundColourId, surface);
    amountSlider.setColour (juce::Slider::textBoxOutlineColourId, outline);
    addAndMakeVisible (amountSlider);

    const auto setupCombo = [this] (juce::ComboBox& combo, const juce::String& name, int focusOrder)
    {
        combo.setName (name);
        combo.setTitle (name);
        combo.setEditableText (false);
        combo.setWantsKeyboardFocus (true);
        combo.setExplicitFocusOrder (focusOrder);
        combo.setColour (juce::ComboBox::backgroundColourId, surface);
        combo.setColour (juce::ComboBox::textColourId, text);
        combo.setColour (juce::ComboBox::arrowColourId, secondaryText);
        combo.setColour (juce::ComboBox::outlineColourId, outline);
        combo.setColour (juce::ComboBox::focusedOutlineColourId, accent);
        addAndMakeVisible (combo);
    };

    setupCombo (characterCombo, "Character", 2);
    characterCombo.addItemList (juce::StringArray { "Echo", "Bloom", "Pulse" }, 1);
    setupCombo (outputCombo, "Output", 3);
    outputCombo.addItemList (juce::StringArray { "Add", "Particles" }, 1);

    auto& state = processor.getAPVTS();
    amountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, OzoMidiDelayProcessor::amountParameterID, amountSlider);
    characterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        state, OzoMidiDelayProcessor::characterParameterID, characterCombo);
    outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        state, OzoMidiDelayProcessor::outputModeParameterID, outputCombo);

    // The parameter's text formatter supplies percentages, including the initial 30%.
    setSize (430, 300);
    timerCallback();
    startTimerHz (8);
}

OzoMidiDelayEditor::~OzoMidiDelayEditor()
{
    stopTimer();
}

void OzoMidiDelayEditor::paint (juce::Graphics& graphics)
{
    graphics.fillAll (background);
    graphics.setColour (outline.withAlpha (0.55f));
    graphics.drawHorizontalLine (64, 22.0f, static_cast<float> (getWidth() - 22));
    graphics.drawHorizontalLine (258, 22.0f, static_cast<float> (getWidth() - 22));
}

void OzoMidiDelayEditor::resized()
{
    titleLabel.setBounds (24, 20, 382, 32);
    amountLabel.setBounds (24, 82, 158, 24);
    amountSlider.setBounds (24, 108, 158, 136);
    characterLabel.setBounds (214, 84, 192, 22);
    characterCombo.setBounds (214, 112, 192, 34);
    outputLabel.setBounds (214, 166, 192, 22);
    outputCombo.setBounds (214, 194, 192, 34);
    hintLabel.setBounds (22, 268, 237, 18);
    statusLabel.setBounds (261, 268, 147, 18);
}

void OzoMidiDelayEditor::timerCallback()
{
    const auto active = processor.getActiveOutputMode();
    const auto pending = processor.isOutputModePending();
    juce::String status = active == ozo::midi::OutputMode::particles ? "Particles" : "Add";
    status += pending ? " / pending" : " / active";
    statusLabel.setText (status, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, pending ? accent : secondaryText);
}
