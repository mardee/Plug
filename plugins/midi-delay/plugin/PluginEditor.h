#pragma once

#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

class OzoMidiDelayEditor final : public juce::AudioProcessorEditor,
                                 private juce::Timer
{
public:
    explicit OzoMidiDelayEditor (OzoMidiDelayProcessor&);
    ~OzoMidiDelayEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    OzoMidiDelayProcessor& processor;
    juce::Label titleLabel;
    juce::Label amountLabel;
    juce::Label characterLabel;
    juce::Label outputLabel;
    juce::Label hintLabel;
    juce::Label statusLabel;
    juce::Slider amountSlider;
    juce::ComboBox characterCombo;
    juce::ComboBox outputCombo;

    // Destroy attachments before the controls they observe.
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> characterAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> outputAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OzoMidiDelayEditor)
};
