#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace
{
void require (bool condition, const char* message)
{
    if (! condition)
        throw std::runtime_error (message);
}

float value (OzoMidiDelayProcessor& processor, const char* id)
{
    return processor.getAPVTS().getRawParameterValue (id)->load();
}

void setValue (OzoMidiDelayProcessor& processor, const char* id, float actual)
{
    auto* parameter = processor.getAPVTS().getParameter (id);
    require (parameter != nullptr, "Missing processor parameter");
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (actual));
}

void tick (OzoMidiDelayProcessor& processor)
{
    juce::AudioBuffer<float> audio (0, 128);
    juce::MidiBuffer midi;
    processor.processBlock (audio, midi);
}

juce::MemoryBlock serialise (const juce::XmlElement& xml)
{
    juce::MemoryBlock data;
    juce::AudioProcessor::copyXmlToBinary (xml, data);
    return data;
}

void saveSnapshot (juce::AudioProcessorEditor& editor, const juce::File& file)
{
    require (! file.exists(), "Snapshot path already exists; refusing to overwrite it");
    require (file.getParentDirectory().createDirectory().wasOk(), "Cannot create snapshot directory");
    const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 2.0f);
    auto stream = file.createOutputStream();
    require (stream != nullptr && stream->openedOk(), "Cannot open snapshot output");
    require (juce::PNGImageFormat().writeImageToStream (image, *stream), "Cannot encode UI snapshot");
    std::cout << "SNAPSHOT " << file.getFullPathName() << '\n';
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    try
    {
        OzoMidiDelayProcessor processor;
        processor.prepareToPlay (48000.0, 128);
        require (processor.isMidiEffect() && processor.acceptsMidi() && processor.producesMidi(),
                 "Processor MIDI capabilities are incorrect");
        require (processor.getTotalNumInputChannels() == 0 && processor.getTotalNumOutputChannels() == 0,
                 "Unexpected processor audio buses");
        require (std::abs (value (processor, "amount") - 0.3f) < 0.001f, "Wrong default Amount");
        require (value (processor, "character") == 1.0f && value (processor, "outputMode") == 0.0f,
                 "Wrong default Character or Output");
        std::cout << "PASS processor defaults and MIDI buses\n";

        {
            setValue (processor, "amount", 0.0f);
            juce::AudioBuffer<float> zeroAudio (0, 0);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (90)), 0);
            processor.processBlock (zeroAudio, midi);
            require (midi.getNumEvents() == 1 && (*midi.begin()).getMessage().isNoteOn(),
                     "Zero-length processor callback consumed MIDI");
            midi.clear();
            juce::AudioBuffer<double> doubleAudio (0, 128);
            midi.addEvent (juce::MidiMessage::noteOn (2, 62, static_cast<juce::uint8> (77)), 17);
            midi.addEvent (juce::MidiMessage::noteOff (2, 62), 93);
            processor.processBlock (doubleAudio, midi);
            require (midi.getNumEvents() == 2, "Double-precision MIDI adapter dropped events");
            auto iterator = midi.begin();
            require ((*iterator).samplePosition == 17 && (*iterator).getMessage().isNoteOn(),
                     "Double-precision Note On changed");
            ++iterator;
            require ((*iterator).samplePosition == 93 && (*iterator).getMessage().isNoteOff(),
                     "Double-precision Note Off changed");
            midi.clear();
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, 99), 31);
            processor.processBlockBypassed (doubleAudio, midi);
            require (midi.getNumEvents() == 1 && (*midi.begin()).getMessage().isController()
                     && (*midi.begin()).samplePosition == 31, "Add bypass is not transparent");
            std::cout << "PASS zero-length, double-precision and bypass adapters\n";
        }

        setValue (processor, "amount", 0.71f);
        setValue (processor, "character", 2.0f);
        setValue (processor, "outputMode", 1.0f);
        tick (processor);
        require (processor.getActiveOutputMode() == ozo::midi::OutputMode::particles,
                 "Idle output mode change was not applied");
        juce::MemoryBlock state;
        processor.getStateInformation (state);
        const auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
        require (xml != nullptr && xml->hasTagName ("OZO_MIDI_DELAY_STATE"), "Unexpected state envelope");
        require (xml->getIntAttribute ("version") == 1, "Missing state version");
        setValue (processor, "amount", 0.0f);
        setValue (processor, "character", 0.0f);
        setValue (processor, "outputMode", 0.0f);
        processor.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        tick (processor);
        require (std::abs (value (processor, "amount") - 0.71f) < 0.001f
                 && value (processor, "character") == 2.0f && value (processor, "outputMode") == 1.0f,
                 "State roundtrip did not preserve parameters");
        std::cout << "PASS processor state restore without an editor\n";

        const auto checkRejected = [&] (const juce::XmlElement& invalid)
        {
            const auto bytes = serialise (invalid);
            processor.setStateInformation (bytes.getData(), static_cast<int> (bytes.getSize()));
            require (std::abs (value (processor, "amount") - 0.71f) < 0.001f
                     && value (processor, "character") == 2.0f && value (processor, "outputMode") == 1.0f,
                     "Invalid state partially changed processor parameters");
        };
        {
            auto invalid = std::make_unique<juce::XmlElement> (*xml);
            invalid->setAttribute ("version", 999);
            checkRejected (*invalid);
        }
        for (const char* badValue : { "nan", "inf", "-0.1", "1.1", "invalid" })
        {
            auto invalid = std::make_unique<juce::XmlElement> (*xml);
            auto* parameters = invalid->getChildByName ("PARAMETERS");
            require (parameters != nullptr, "Missing APVTS state");
            for (auto* parameter : parameters->getChildIterator())
                if (parameter->getStringAttribute ("id") == "amount")
                    parameter->setAttribute ("value", badValue);
            checkRejected (*invalid);
        }
        {
            auto invalid = std::make_unique<juce::XmlElement> (*xml);
            auto* parameters = invalid->getChildByName ("PARAMETERS");
            parameters->addChildElement (new juce::XmlElement (*parameters->getFirstChildElement()));
            checkRejected (*invalid);
        }
        {
            const unsigned char garbage[] { 0x0, 0xff, 0x3, 0x2 };
            processor.setStateInformation (garbage, sizeof (garbage));
            require (std::abs (value (processor, "amount") - 0.71f) < 0.001f, "Corrupt state changed Amount");
        }
        std::cout << "PASS malformed, nonfinite, duplicate and future-version state rejection\n";

        setValue (processor, "amount", 0.3f);
        setValue (processor, "character", 1.0f);
        setValue (processor, "outputMode", 0.0f);
        tick (processor);
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
        require (editor != nullptr, "Editor creation failed");
        require (editor->getWidth() >= 300 && editor->getHeight() >= 200, "Editor has invalid dimensions");
        int sliders = 0;
        int choices = 0;
        for (auto* child : editor->getChildren())
        {
            require (editor->getLocalBounds().contains (child->getBounds()), "A control extends outside the editor");
            if (auto* slider = dynamic_cast<juce::Slider*> (child))
            {
                ++sliders;
                slider->setValue (slider->getMinimum() + 0.64 * (slider->getMaximum() - slider->getMinimum()),
                                  juce::sendNotificationSync);
                require (std::abs (value (processor, "amount") - 0.64f) < 0.015f,
                         "Amount control is not connected to the host parameter");
            }
            if (auto* combo = dynamic_cast<juce::ComboBox*> (child))
            {
                ++choices;
                if (combo->getNumItems() == 3)
                {
                    combo->setSelectedItemIndex (2, juce::sendNotificationSync);
                    require (value (processor, "character") == 2.0f, "Character attachment failed");
                    combo->setSelectedItemIndex (1, juce::sendNotificationSync);
                }
                else if (combo->getNumItems() == 2)
                {
                    combo->setSelectedItemIndex (1, juce::sendNotificationSync);
                    require (value (processor, "outputMode") == 1.0f, "Output attachment failed");
                    combo->setSelectedItemIndex (0, juce::sendNotificationSync);
                }
            }
        }
        require (sliders == 1 && choices == 2, "Minimal editor should contain one slider and two choices");
        std::cout << "PASS minimal editor controls and parameter attachments\n";
        editor.reset();
        setValue (processor, "amount", 0.3f);
        tick (processor);
        editor.reset (processor.createEditor());
        if (argc == 2)
            saveSnapshot (*editor, juce::File (juce::String (argv[1])));
        editor.reset();
        processor.releaseResources();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
