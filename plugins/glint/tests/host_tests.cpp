#include <juce_audio_processors_headless/juce_audio_processors_headless.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 128;

void require (bool condition, const char* message)
{
    if (! condition)
        throw std::runtime_error (message);
}

struct Event
{
    std::int64_t sample;
    juce::MidiMessage message;
};

class PlayHead final : public juce::AudioPlayHead
{
public:
    std::int64_t sample = 0;
    bool playing = true;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (120.0);
        info.setTimeInSamples (sample);
        info.setTimeInSeconds (static_cast<double> (sample) / sampleRate);
        info.setPpqPosition (static_cast<double> (sample) / (sampleRate * 0.5));
        info.setIsPlaying (playing);
        return info;
    }
};

void setParameter (juce::AudioPluginInstance& plugin, const juce::String& name, float value)
{
    for (auto* parameter : plugin.getParameters())
        if (parameter->getName (128).startsWithIgnoreCase (name))
        {
            parameter->setValueNotifyingHost (value);
            return;
        }
    throw std::runtime_error ("Missing host parameter: " + name.toStdString());
}

class Host
{
public:
    explicit Host (const juce::String& path, const juce::String& expectedName = "ozo GLINT")
    {
        formats.addFormat (std::make_unique<juce::VST3PluginFormatHeadless>());
        juce::OwnedArray<juce::PluginDescription> descriptions;
        formats.getFormat (0)->findAllTypesForFile (descriptions, path);
        require (! descriptions.isEmpty(), "VST3 scan returned no descriptions");
        require (descriptions[0]->name == expectedName, "VST3 scan exposes an unexpected product name");
        juce::String error;
        plugin = formats.createPluginInstance (*descriptions[0], sampleRate, blockSize, error);
        if (plugin == nullptr)
            throw std::runtime_error ("VST3 load failed: " + error.toStdString());
        plugin->setPlayHead (&clock);
        plugin->prepareToPlay (sampleRate, blockSize);
        audio.setSize (std::max (plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels()),
                       blockSize);
    }

    ~Host()
    {
        plugin->setPlayHead (nullptr);
        plugin->releaseResources();
    }

    std::vector<Event> render (const std::vector<Event>& input, std::int64_t length)
    {
        std::vector<Event> result;
        std::size_t cursor = 0;
        for (std::int64_t position = 0; position < length; position += blockSize)
        {
            const auto samples = static_cast<int> (std::min<std::int64_t> (blockSize, length - position));
            audio.setSize (audio.getNumChannels(), samples, false, false, true);
            audio.clear();
            midi.clear();
            while (cursor < input.size() && input[cursor].sample < position + samples)
            {
                require (input[cursor].sample >= position, "Unsorted host input");
                midi.addEvent (input[cursor].message, static_cast<int> (input[cursor].sample - position));
                ++cursor;
            }
            clock.sample = position;
            plugin->processBlock (audio, midi);
            for (const auto metadata : midi)
            {
                require (metadata.samplePosition >= 0 && metadata.samplePosition < samples,
                         "Host returned an out-of-block MIDI offset");
                result.push_back ({ position + metadata.samplePosition, metadata.getMessage() });
            }
        }
        return result;
    }

    juce::AudioPluginFormatManager formats;
    PlayHead clock;
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::AudioBuffer<float> audio;
    juce::MidiBuffer midi;
};

bool sameMessage (const juce::MidiMessage& a, const juce::MidiMessage& b)
{
    return a.getRawDataSize() == b.getRawDataSize()
        && std::equal (a.getRawData(), a.getRawData() + a.getRawDataSize(), b.getRawData());
}

void checkBalanced (const std::vector<Event>& events)
{
    std::array<int, 16 * 128> keys {};
    int ons = 0;
    for (const auto& event : events)
    {
        const auto& message = event.message;
        if (! message.isNoteOnOrOff())
            continue;
        auto& count = keys[static_cast<std::size_t> ((message.getChannel() - 1) * 128 + message.getNoteNumber())];
        if (message.isNoteOn())
        {
            ++ons;
            require (count == 0, "Generated same-key overlap crossed the VST3 wrapper");
            ++count;
        }
        else
        {
            require (count > 0, "Unowned Note Off crossed the VST3 wrapper");
            --count;
        }
    }
    require (ons > 0, "VST3 generated no notes");
    require (std::all_of (keys.begin(), keys.end(), [] (int n) { return n == 0; }),
             "VST3 left generated keys down after the tail");
}
}

int main (int argc, char** argv)
{
    if (argc != 2 && argc != 3)
    {
        std::cerr << "Usage: ozoGlintHostTests /path/to/GLINT.vst3 [/path/to/legacy-MIDI-Delay.vst3]\n";
        return 2;
    }
    const juce::String path (argv[1]);
    try
    {
        {
            Host host (path);
            require (host.plugin->acceptsMidi() && host.plugin->producesMidi(), "MIDI ports are not exposed");
            require (host.plugin->getLatencySamples() == 0, "Unexpected plugin latency");
            setParameter (*host.plugin, "Amount", 0.0f);
            const std::vector<Event> input {
                { 17, juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (83)) },
                { 127, juce::MidiMessage::noteOn (2, 67, static_cast<juce::uint8> (51)) },
                { 300, juce::MidiMessage::controllerEvent (1, 64, 127) },
                { 481, juce::MidiMessage::noteOff (1, 60) },
                { 511, juce::MidiMessage::noteOff (2, 67) },
                { 600, juce::MidiMessage::controllerEvent (1, 64, 0) }
            };
            const auto output = host.render (input, 1024);
            require (output.size() == input.size(), "MIDI pass-through changed the event count");
            for (std::size_t i = 0; i < input.size(); ++i)
                require (input[i].sample == output[i].sample && sameMessage (input[i].message, output[i].message),
                         "MIDI pass-through changed a timestamp or payload");
            std::cout << "PASS VST3 note / sustain pass-through with sample offsets\n";
        }
        for (int character = 0; character < 3; ++character)
        {
            Host host (path);
            setParameter (*host.plugin, "Amount", 0.8f);
            setParameter (*host.plugin, "Character", static_cast<float> (character) * 0.5f);
            setParameter (*host.plugin, "Output", 1.0f);
            const std::vector<Event> input {
                { 23, juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (90)) },
                { 23, juce::MidiMessage::noteOn (1, 64, static_cast<juce::uint8> (80)) },
                { 23, juce::MidiMessage::noteOn (1, 67, static_cast<juce::uint8> (75)) },
                { 48000, juce::MidiMessage::noteOff (1, 60) },
                { 48000, juce::MidiMessage::noteOff (1, 64) },
                { 48000, juce::MidiMessage::noteOff (1, 67) }
            };
            const auto output = host.render (input, 12 * 48000);
            checkBalanced (output);
            for (const auto& event : output)
                if (event.message.isNoteOn())
                {
                    const auto note = event.message.getNoteNumber();
                    require ((note >= 60 && note <= 84 && (note - 60) % 12 == 0)
                          || (note >= 64 && note <= 88 && (note - 64) % 12 == 0)
                          || (note >= 67 && note <= 91 && (note - 67) % 12 == 0),
                             "VST3 generated an unrelated pitch");
                    require (event.sample > 23, "Particles mode leaked original Note On");
                }
            std::cout << "PASS VST3 character " << character << " generated balanced particles ("
                      << output.size() << " events)\n";
        }
        {
            Host host (path);
            setParameter (*host.plugin, "Amount", 0.63f);
            setParameter (*host.plugin, "Character", 1.0f);
            setParameter (*host.plugin, "Output", 1.0f);
            juce::MemoryBlock saved;
            host.plugin->getStateInformation (saved);
            require (saved.getSize() > 0, "Empty VST3 state");
            setParameter (*host.plugin, "Amount", 0.0f);
            host.plugin->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
            const auto output = host.render ({
                { 0, juce::MidiMessage::noteOn (1, 62, static_cast<juce::uint8> (96)) },
                { 24000, juce::MidiMessage::noteOff (1, 62) }
            }, 12 * 48000);
            checkBalanced (output);
            require (output.front().sample > 0, "Restored Particles mode leaked original input");
            std::cout << "PASS VST3 state restore without an editor\n";
        }
        if (argc == 3)
        {
            juce::MemoryBlock legacyState;
            const std::vector<Event> input {
                { 17, juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (92)) },
                { 17, juce::MidiMessage::noteOn (1, 64, static_cast<juce::uint8> (87)) },
                { 24000, juce::MidiMessage::noteOff (1, 60) },
                { 24000, juce::MidiMessage::noteOff (1, 64) }
            };
            std::vector<Event> legacyOutput;
            {
                Host legacy (juce::String (argv[2]), "ozo MIDI Delay");
                setParameter (*legacy.plugin, "Amount", 0.82f);
                setParameter (*legacy.plugin, "Character", 0.5f);
                setParameter (*legacy.plugin, "Output", 1.0f);
                legacy.plugin->getStateInformation (legacyState);
                legacyOutput = legacy.render (input, 12 * 48000);
            }
            require (legacyState.getSize() > 0, "Legacy plugin returned no state");
            Host renamed (path);
            renamed.plugin->setStateInformation (legacyState.getData(), static_cast<int> (legacyState.getSize()));
            const auto renamedOutput = renamed.render (input, 12 * 48000);
            checkBalanced (renamedOutput);
            require (legacyOutput.size() == renamedOutput.size(), "Rename changed legacy-state event count");
            for (std::size_t i = 0; i < legacyOutput.size(); ++i)
                require (legacyOutput[i].sample == renamedOutput[i].sample
                         && sameMessage (legacyOutput[i].message, renamedOutput[i].message),
                         "Rename changed MIDI generated from actual legacy-plugin state");
            std::cout << "PASS actual pre-GLINT VST3 state restored with identical MIDI output\n";
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
