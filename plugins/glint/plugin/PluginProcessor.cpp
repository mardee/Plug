#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <limits>
#include <memory>

namespace
{
class BufferMidiInput final : public ozo::midi::MidiInput
{
public:
    explicit BufferMidiInput (const juce::MidiBuffer& buffer) noexcept
        : current (buffer.cbegin()), end (buffer.cend()) {}

    bool next (ozo::midi::MidiEventView& event) noexcept override
    {
        if (current == end)
            return false;

        const auto metadata = *current++;
        event = { metadata.samplePosition, metadata.data, metadata.numBytes };
        return true;
    }

private:
    juce::MidiBufferIterator current, end;
};

class BufferMidiOutput final : public ozo::midi::MidiOutput
{
public:
    explicit BufferMidiOutput (juce::MidiBuffer& buffer) noexcept : destination (buffer) {}

    void push (int sampleOffset, const std::uint8_t* data, int size) noexcept override
    {
        if (data != nullptr && size > 0)
            destination.addEvent (data, size, sampleOffset); // Copies before the view expires.
    }

private:
    juce::MidiBuffer& destination;
};

// XML stores ValueTree numbers as strings. Validate the entire decimal spelling,
// rather than accepting getDoubleValue()'s permissive prefix conversion.
bool readFiniteNumber (const juce::var& property, double& result)
{
    if (property.isInt() || property.isInt64() || property.isDouble())
    {
        result = static_cast<double> (property);
        return std::isfinite (result);
    }

    if (! property.isString())
        return false;

    const auto text = property.toString().trim();
    if (text.isEmpty() || text.length() > 64)
        return false;

    auto cursor = text.toRawUTF8();
    if (*cursor == '+' || *cursor == '-')
        ++cursor;

    const auto isDigit = [] (char c) { return c >= '0' && c <= '9'; };
    bool hasDigits = false;
    while (isDigit (*cursor))
    {
        hasDigits = true;
        ++cursor;
    }

    if (*cursor == '.')
    {
        ++cursor;
        while (isDigit (*cursor))
        {
            hasDigits = true;
            ++cursor;
        }
    }

    if (! hasDigits)
        return false;

    if (*cursor == 'e' || *cursor == 'E')
    {
        ++cursor;
        if (*cursor == '+' || *cursor == '-')
            ++cursor;
        if (! isDigit (*cursor))
            return false;
        while (isDigit (*cursor))
            ++cursor;
    }

    if (*cursor != '\0')
        return false;

    result = text.getDoubleValue();
    return std::isfinite (result);
}

bool readNumberInRange (const juce::var& property, double minimum, double maximum,
                        bool mustBeInteger, double& result)
{
    return readFiniteNumber (property, result)
        && result >= minimum && result <= maximum
        && (! mustBeInteger || result == std::floor (result));
}

struct RestoredState
{
    juce::ValueTree parameters { OzoGlintProcessor::parameterTreeType };
    std::uint32_t seed = ozo::midi::Controls {}.seed;
};

bool validateState (const juce::ValueTree& root, RestoredState& restored)
{
    if (! root.hasType (OzoGlintProcessor::stateRootType)
        || root.getNumProperties() != 2 || root.getNumChildren() != 1)
        return false;

    double version = 0.0, seed = 0.0;
    if (! readNumberInRange (root.getProperty ("version"), 1.0, 1.0, true, version)
        || ! readNumberInRange (root.getProperty ("seed"), 0.0,
                                static_cast<double> (std::numeric_limits<std::uint32_t>::max()),
                                true, seed))
        return false;

    const auto parameters = root.getChild (0);
    if (! parameters.hasType (OzoGlintProcessor::parameterTreeType)
        || parameters.getNumProperties() != 0 || parameters.getNumChildren() != 3)
        return false;

    unsigned int found = 0;
    for (const auto child : parameters)
    {
        if (! child.hasType ("PARAM") || child.getNumChildren() != 0
            || child.getNumProperties() != 2 || ! child.getProperty ("id").isString())
            return false;

        const auto id = child.getProperty ("id").toString();
        unsigned int bit = 0;
        double maximum = 1.0;
        bool integer = false;
        if (id == OzoGlintProcessor::amountParameterID)
            bit = 1u;
        else if (id == OzoGlintProcessor::characterParameterID)
        {
            bit = 2u;
            maximum = 2.0;
            integer = true;
        }
        else if (id == OzoGlintProcessor::outputModeParameterID)
        {
            bit = 4u;
            integer = true;
        }
        else
            return false;

        double value = 0.0;
        if ((found & bit) != 0
            || ! readNumberInRange (child.getProperty ("value"), 0.0, maximum, integer, value))
            return false;

        found |= bit;
        // Rebuild a whitelisted, typed tree; never import unknown state or voice data.
        juce::ValueTree parameter { "PARAM" };
        parameter.setProperty ("id", id, nullptr);
        parameter.setProperty ("value", value, nullptr);
        restored.parameters.addChild (parameter, -1, nullptr);
    }

    restored.seed = static_cast<std::uint32_t> (seed);
    return found == 7u;
}

float safeAmount (float value) noexcept
{
    return std::isfinite (value) ? juce::jlimit (0.0f, 1.0f, value) : 0.30f;
}

int safeChoice (float value, int maximum, int fallback) noexcept
{
    if (! std::isfinite (value) || value < 0.0f || value > static_cast<float> (maximum))
        return fallback;

    return static_cast<int> (std::round (value));
}
}

OzoGlintProcessor::OzoGlintProcessor()
    : juce::AudioProcessor (BusesProperties {}),
      apvts (*this, nullptr, parameterTreeType, createParameterLayout()),
      amountValue (apvts.getRawParameterValue (amountParameterID)),
      characterValue (apvts.getRawParameterValue (characterParameterID)),
      outputModeValue (apvts.getRawParameterValue (outputModeParameterID))
{
    jassert (amountValue != nullptr && characterValue != nullptr && outputModeValue != nullptr);
}

const juce::String OzoGlintProcessor::getName() const
{
    return "ozo GLINT";
}

juce::AudioProcessorValueTreeState::ParameterLayout OzoGlintProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto amountAttributes = juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float value, int)
        {
            return juce::String (juce::roundToInt (safeAmount (value) * 100.0f)) + "%";
        })
        .withValueFromStringFunction ([] (const juce::String& text)
        {
            return safeAmount (text.trim().trimCharactersAtEnd ("% ").getFloatValue() / 100.0f);
        });

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { amountParameterID, 1 }, "Amount",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.30f, amountAttributes));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { characterParameterID, 1 }, "Character",
        juce::StringArray { "Echo", "Bloom", "Pulse" }, 1));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { outputModeParameterID, 1 }, "Output",
        juce::StringArray { "Add", "Particles" }, 0,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));
    return layout;
}

void OzoGlintProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    juce::ignoreUnused (maximumExpectedSamplesPerBlock);
    scratchMidi.clear();
    scratchMidi.ensureSize (midiReserveBytes);
    engine.prepare (std::isfinite (sampleRate) && sampleRate > 0.0 ? sampleRate : 44100.0);
    publishEngineStatus();
}

void OzoGlintProcessor::releaseResources()
{
    // There is no output buffer here. Ask the next non-empty callback to release old notes.
    reset();
}

void OzoGlintProcessor::reset()
{
    resetSerial.fetch_add (1, std::memory_order_release);
}

bool OzoGlintProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.inputBuses.isEmpty() && layouts.outputBuses.isEmpty();
}

void OzoGlintProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    processMidi (buffer.getNumSamples(), midi, false);
}

void OzoGlintProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midi)
{
    processMidi (buffer.getNumSamples(), midi, false);
}

void OzoGlintProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer,
                                                juce::MidiBuffer& midi)
{
    processMidi (buffer.getNumSamples(), midi, true);
}

void OzoGlintProcessor::processBlockBypassed (juce::AudioBuffer<double>& buffer,
                                                juce::MidiBuffer& midi)
{
    processMidi (buffer.getNumSamples(), midi, true);
}

ozo::midi::Controls OzoGlintProcessor::readControls (bool bypassed) noexcept
{
    const auto revision = stateRevision.load (std::memory_order_acquire);
    if ((revision & 1u) == 0)
    {
        auto next = audioControls;
        next.resetSerial = resetSerial.load (std::memory_order_acquire);
        next.seed = seed.load (std::memory_order_relaxed);
        next.amount = safeAmount (amountValue->load (std::memory_order_relaxed));
        next.character = static_cast<ozo::midi::Character> (
            safeChoice (characterValue->load (std::memory_order_relaxed), 2, 1));
        next.outputMode = static_cast<ozo::midi::OutputMode> (
            safeChoice (outputModeValue->load (std::memory_order_relaxed), 1, 0));

        // A restore never blocks/spins the audio thread or publishes half of a preset.
        // Use the previous block's snapshot if a state transaction overlaps this read.
        std::atomic_thread_fence (std::memory_order_acquire);
        if (revision == stateRevision.load (std::memory_order_acquire))
            audioControls = next;
    }

    audioControls.bypass = bypassed;
    return audioControls;
}

ozo::midi::Transport OzoGlintProcessor::readTransport() const
{
    ozo::midi::Transport transport;
    if (const auto* playHead = getPlayHead())
    {
        if (const auto position = playHead->getPosition())
        {
            if (const auto tempo = position->getBpm(); tempo && std::isfinite (*tempo) && *tempo > 0.0)
            {
                transport.bpm = *tempo;
                transport.hasTempo = true;
            }
            if (const auto ppq = position->getPpqPosition(); ppq && std::isfinite (*ppq))
            {
                transport.ppqPosition = *ppq;
                transport.hasPpq = true;
            }
            if (const auto samples = position->getTimeInSamples())
            {
                transport.samplePosition = *samples;
                transport.hasSamplePosition = true;
            }
            transport.isPlaying = position->getIsPlaying();
        }
    }
    return transport;
}

void OzoGlintProcessor::processMidi (int numSamples, juce::MidiBuffer& midi, bool bypassed)
{
    // A zero-channel AudioBuffer still carries time. Only a zero-length block is inert:
    // do not consume input, change engine time, swap buffers, or acknowledge a reset.
    if (numSamples <= 0)
        return;

    scratchMidi.clear();
    scratchMidi.ensureSize (midiReserveBytes);
    BufferMidiInput input (midi);
    BufferMidiOutput output (scratchMidi);
    const auto controls = readControls (bypassed);
    engine.process (numSamples, readTransport(), controls, input, output);
    publishEngineStatus();

    // Core owns bypass cleanup and routing: Add passes input, Particles suppresses it
    // (necessary old-note releases may still be emitted). Never bypass the core call.
    midi.swapWith (scratchMidi);
    scratchMidi.clear();
    // The first swap may hand us a small host buffer. Warm that side too and retain
    // both capacities. A host replacing its buffer, or oversized MIDI/SysEx, can
    // still allocate here/in addEvent: this adapter is not an unbounded RT guarantee.
    scratchMidi.ensureSize (midiReserveBytes);
}

void OzoGlintProcessor::publishEngineStatus() noexcept
{
    const auto status = engine.getStatus();
    const auto bits = (status.activeMode == ozo::midi::OutputMode::particles ? 1u : 0u)
                    | (status.modeChangePending ? 2u : 0u);
    publishedStatus.store (bits, std::memory_order_relaxed);
}

ozo::midi::OutputMode OzoGlintProcessor::getActiveOutputMode() const noexcept
{
    return (publishedStatus.load (std::memory_order_relaxed) & 1u) != 0
        ? ozo::midi::OutputMode::particles : ozo::midi::OutputMode::add;
}

bool OzoGlintProcessor::isOutputModePending() const noexcept
{
    return (publishedStatus.load (std::memory_order_relaxed) & 2u) != 0;
}

juce::AudioProcessorEditor* OzoGlintProcessor::createEditor()
{
    return new OzoGlintEditor (*this);
}

void OzoGlintProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    const juce::ScopedLock lock (stateLock);
    juce::ValueTree root { stateRootType };
    root.setProperty ("version", stateVersion, nullptr);
    root.setProperty ("seed", static_cast<juce::int64> (seed.load (std::memory_order_relaxed)), nullptr);
    root.addChild (apvts.copyState(), -1, nullptr);

    if (const auto xml = root.createXml())
        copyXmlToBinary (*xml, destination);
}

void OzoGlintProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // A v1 state is a tiny tree; reject missing or implausibly large data before parsing.
    if (data == nullptr || sizeInBytes <= 0 || sizeInBytes > 64 * 1024)
        return;

    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    RestoredState restored;
    if (! validateState (juce::ValueTree::fromXml (*xml), restored))
        return;

    const juce::ScopedLock lock (stateLock);
    stateRevision.fetch_add (1, std::memory_order_acq_rel);
    apvts.replaceState (restored.parameters);
    seed.store (restored.seed, std::memory_order_relaxed);
    resetSerial.fetch_add (1, std::memory_order_release);
    stateRevision.fetch_add (1, std::memory_order_release);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OzoGlintProcessor();
}
