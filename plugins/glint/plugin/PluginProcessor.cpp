#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <limits>
#include <memory>

namespace
{
constexpr std::array<const char*, 8> laneFields {{
    "enabled", "first", "repeat", "feedback", "level", "gate", "motion", "cycle"
}};
constexpr std::array<const char*, 8> laneNames {{
    "Enable", "First", "Repeat", "Feedback", "Level", "Gate", "Motion", "Cycle"
}};
constexpr std::array<double, 8> laneMaximum {{ 1.0, 9.0, 9.0, static_cast<double> (0.98f),
                                               1.0, 9.0, 1.0, 3.0 }};
constexpr std::array<bool, 8> laneInteger {{ true, true, true, false, false, true, false, true }};
constexpr std::size_t parameterCount = 4 + ozo::midi::laneCount * laneFields.size();
static_assert (parameterCount == 60);

std::array<double, 8> laneDefaults (const ozo::midi::LaneControls& lane) noexcept
{
    return {{ lane.enabled ? 1.0 : 0.0, static_cast<double> (lane.first),
              static_cast<double> (lane.repeat), lane.feedback, lane.level,
              static_cast<double> (lane.gate), lane.motion, static_cast<double> (lane.cycle) }};
}

void appendParameter (juce::ValueTree& parameters, const juce::String& id, double value)
{
    juce::ValueTree parameter { "PARAM" };
    parameter.setProperty ("id", id, nullptr);
    parameter.setProperty ("value", value, nullptr);
    parameters.addChild (parameter, -1, nullptr);
}

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

// Borrowed for a single audio block. This observer has no GUI references, owns
// no heap storage, and cannot affect MIDI acceptance, scheduling or random draws.
class VisualNoteObserver final : public ozo::midi::GeneratedNoteObserver
{
public:
    VisualNoteObserver (ozo::nightshift::LaneVisualEventQueue& queue, std::uint64_t session,
                        std::uint64_t revision, double blockTimeMs, double millisecondsPerSample) noexcept
        : queue_ (queue), session_ (session), revision_ (revision), blockTimeMs_ (blockTimeMs),
          millisecondsPerSample_ (millisecondsPerSample) {}

    void generatedNoteOn (const ozo::midi::GeneratedNoteOn& note) noexcept override
    {
        queue_.push (session_, { note.laneIndex, note.sampleTimestamp, note.sampleOffset,
                                note.velocity, blockTimeMs_ + note.sampleOffset * millisecondsPerSample_,
                                revision_, note.onsetBeats, note.repeatBeats });
    }

private:
    ozo::nightshift::LaneVisualEventQueue& queue_;
    const std::uint64_t session_, revision_;
    const double blockTimeMs_, millisecondsPerSample_;
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
    if (! readNumberInRange (root.getProperty ("version"), 1.0,
                             OzoGlintProcessor::stateVersion, true, version)
        || ! readNumberInRange (root.getProperty ("seed"), 0.0,
                                static_cast<double> (std::numeric_limits<std::uint32_t>::max()),
                                true, seed))
        return false;

    if (version == 6.0)
    {
        const auto parameters = root.getChild (0);
        if (! parameters.hasType (OzoGlintProcessor::parameterTreeType)
            || parameters.getNumProperties() != 0
            || parameters.getNumChildren() != static_cast<int> (parameterCount))
            return false;

        std::array<juce::String, parameterCount> ids;
        ids[0] = OzoGlintProcessor::amountParameterID;
        ids[1] = OzoGlintProcessor::outputModeParameterID;
        ids[2] = OzoGlintProcessor::rootParameterID;
        ids[3] = OzoGlintProcessor::scaleParameterID;
        for (std::size_t lane = 0; lane < ozo::midi::laneCount; ++lane)
            for (std::size_t field = 0; field < laneFields.size(); ++field)
                ids[4 + lane * laneFields.size() + field]
                    = OzoGlintProcessor::laneParameterID (lane, laneFields[field]);

        std::array<bool, parameterCount> seen {};
        std::array<double, parameterCount> values {};
        constexpr std::array<double, 4> globalMaximum {{ 1.0, 1.0, 11.0, 2.0 }};
        for (const auto child : parameters)
        {
            if (! child.hasType ("PARAM") || child.getNumChildren() != 0
                || child.getNumProperties() != 2 || ! child.getProperty ("id").isString())
                return false;
            const auto id = child.getProperty ("id").toString();
            std::size_t index = 0;
            while (index < ids.size() && ids[index] != id)
                ++index;
            if (index == ids.size() || seen[index])
                return false;

            const auto field = index >= 4 ? (index - 4) % laneFields.size() : 0;
            const auto maximum = index < 4 ? globalMaximum[index] : laneMaximum[field];
            const auto integer = index < 4 ? index != 0 : laneInteger[field];
            if (! readNumberInRange (child.getProperty ("value"), 0.0, maximum,
                                     integer, values[index]))
                return false;
            seen[index] = true;
        }

        for (std::size_t index = 0; index < ids.size(); ++index)
        {
            if (! seen[index])
                return false;
            appendParameter (restored.parameters, ids[index], values[index]);
        }
        restored.seed = static_cast<std::uint32_t> (seed);
        return true;
    }

    const bool current = version == 5.0;
    const auto expectedCount = current ? 4 : (version == 1.0 ? 3 : (version == 2.0 ? 5 : (version == 3.0 ? 7 : 8)));
    double amount = 0.30, outputMode = 0.0, keyRoot = 0.0, scale = 0.0;
    const auto parameters = root.getChild (0);
    if (! parameters.hasType (OzoGlintProcessor::parameterTreeType)
        || parameters.getNumProperties() != 0
        || parameters.getNumChildren() != expectedCount)
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
        else if (id == OzoGlintProcessor::outputModeParameterID)
        {
            bit = 4u;
            integer = true;
        }
        else if (current && id == OzoGlintProcessor::rootParameterID)
        {
            bit = 2u;
            maximum = 11.0;
            integer = true;
        }
        else if (current && id == OzoGlintProcessor::scaleParameterID)
        {
            bit = 8u;
            maximum = 2.0;
            integer = true;
        }
        // These IDs exist only in the legacy parser, never as host parameters.
        else if (! current && id == "character")
        {
            bit = 2u;
            maximum = 2.0;
            integer = true;
        }
        else if (! current && version >= 2.0 && id == "direction")
        {
            bit = 8u;
            maximum = 2.0;
            integer = true;
        }
        else if (! current && version >= 2.0 && id == "rate")
        {
            bit = 16u;
            maximum = 4.0;
            integer = true;
        }
        else if (! current && version >= 3.0 && id == "octaves")
        {
            bit = 32u;
            maximum = 3.0;
            integer = true;
        }
        else if (! current && version >= 3.0 && id == "rhythm")
        {
            bit = 64u;
            integer = true;
        }
        else if (version == 4.0 && id == "shine")
        {
            bit = 128u;
            integer = true;
        }
        else
            return false;

        double value = 0.0;
        if ((found & bit) != 0
            || ! readNumberInRange (child.getProperty ("value"), 0.0, maximum, integer, value))
            return false;

        found |= bit;
        if (id == OzoGlintProcessor::amountParameterID)
            amount = value;
        else if (id == OzoGlintProcessor::outputModeParameterID)
            outputMode = value;
        else if (current && id == OzoGlintProcessor::rootParameterID)
            keyRoot = value;
        else if (current && id == OzoGlintProcessor::scaleParameterID)
            scale = value;
    }

    if (found != (current ? 15u : (version == 1.0 ? 7u : (version == 2.0 ? 31u : (version == 3.0 ? 127u : 255u)))))
        return false;

    // Validate every legacy field first. v1-v4 retain Amount/routing/seed and
    // reset the key to C / unset; v5 also retains root/scale. Lane defaults are
    // always the types table (three upper lanes enabled), never derived from
    // Amount. Discarded timing/harmonic choices mean this is not exact sound recall.
    appendParameter (restored.parameters, OzoGlintProcessor::amountParameterID, amount);
    appendParameter (restored.parameters, OzoGlintProcessor::outputModeParameterID, outputMode);
    appendParameter (restored.parameters, OzoGlintProcessor::rootParameterID, keyRoot);
    appendParameter (restored.parameters, OzoGlintProcessor::scaleParameterID, scale);
    const auto defaults = ozo::midi::defaultLanes();
    for (std::size_t lane = 0; lane < defaults.size(); ++lane)
    {
        const auto values = laneDefaults (defaults[lane]);
        for (std::size_t field = 0; field < laneFields.size(); ++field)
            appendParameter (restored.parameters,
                             OzoGlintProcessor::laneParameterID (lane, laneFields[field]), values[field]);
    }

    restored.seed = static_cast<std::uint32_t> (seed);
    return true;
}

// Settings have only numeric/id attributes and three element levels. Reject
// declarations/entities before JUCE sees them, and bound nesting independently
// of the byte cap. There is no DTD, file input source, or network resolution.
std::unique_ptr<juce::XmlElement> parseSettingsXml (const juce::String& text)
{
    if (text.isEmpty() || text.getNumBytesAsUTF8() > 64u * 1024u)
        return {};
    auto body = text.trim();
    if (body.startsWith ("<?xml "))
    {
        const auto end = body.indexOf ("?>");
        if (end < 0)
            return {};
        body = body.substring (end + 2).trim();
    }
    if (body.contains ("<!") || body.contains ("<?") || body.containsChar ('&'))
        return {};

    int depth = 0;
    bool sawRoot = false;
    auto cursor = body.toRawUTF8();
    while (*cursor != '\0')
    {
        if (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n')
        {
            ++cursor;
            continue;
        }
        if (*cursor++ != '<')
            return {};
        const bool closing = *cursor == '/';
        if (! closing && depth == 0)
        {
            if (sawRoot)
                return {};
            sawRoot = true;
        }
        char quote = 0;
        char previous = 0;
        while (*cursor != '\0' && (quote != 0 || *cursor != '>'))
        {
            const char c = *cursor++;
            if (quote != 0)
            {
                if (c == quote)
                    quote = 0;
            }
            else if (c == '\'' || c == '"')
                quote = c;
            previous = c;
        }
        if (*cursor == '\0')
            return {};
        ++cursor;
        if (closing)
            --depth;
        else if (previous != '/')
            ++depth;
        if (depth < 0 || depth > 3)
            return {};
    }
    if (! sawRoot || depth != 0)
        return {};
    return juce::parseXML (body);
}

float safeLaneFloat (float value, float maximum, float fallback) noexcept
{
    return std::isfinite (value) ? juce::jlimit (0.0f, maximum, value) : fallback;
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
    : juce::AudioProcessor (juce::PluginHostType::getPluginLoadedAs() == wrapperType_AudioUnit
                                ? BusesProperties {}
                                : BusesProperties {}.withOutput ("Silent Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, parameterTreeType, createParameterLayout()),
      userPresets (ozo::UserPresets::defaultDirectory(), [] (const juce::String& text)
      {
          const auto xml = parseSettingsXml (text);
          RestoredState restored;
          return xml != nullptr && xml->getIntAttribute ("version") == stateVersion
              && validateState (juce::ValueTree::fromXml (*xml), restored);
      }),
      amountValue (apvts.getRawParameterValue (amountParameterID)),
      outputModeValue (apvts.getRawParameterValue (outputModeParameterID)),
      rootValue (apvts.getRawParameterValue (rootParameterID)),
      scaleValue (apvts.getRawParameterValue (scaleParameterID))
{
    jassert (amountValue != nullptr && outputModeValue != nullptr
             && rootValue != nullptr && scaleValue != nullptr);
    for (std::size_t lane = 0; lane < laneValues.size(); ++lane)
        for (std::size_t field = 0; field < laneFields.size(); ++field)
        {
            laneValues[lane][field] = apvts.getRawParameterValue (laneParameterID (lane, laneFields[field]));
            jassert (laneValues[lane][field] != nullptr);
        }
    audioControls.manualLanes = true;
    for (auto* parameter : getParameters())
        if (auto* identified = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
            apvts.addParameterListener (identified->paramID, this);
}

OzoGlintProcessor::~OzoGlintProcessor()
{
    for (auto* parameter : getParameters())
        if (auto* identified = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
            apvts.removeParameterListener (identified->paramID, this);
}

juce::String OzoGlintProcessor::laneParameterID (std::size_t lane, const char* field)
{
    return "lane" + juce::String (static_cast<int> (lane)) + "_" + field;
}

const juce::String OzoGlintProcessor::getName() const
{
    return "ozo NIGHTSHIFT";
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
        juce::ParameterID { outputModeParameterID, 1 }, "Output",
        juce::StringArray { "Add", "Particles" }, 0,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { rootParameterID, 5 }, "Key Root",
        juce::StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { scaleParameterID, 5 }, "Scale",
        juce::StringArray { "Not set (octaves)", "Major", "Minor" }, 0));

    const juce::StringArray divisions { "1/32", "1/32 D", "1/16", "1/16 D", "1/8", "1/8 D",
                                        "1/4", "1/4 D", "1/2", "1/2 D" };
    const juce::StringArray cycles { "1 beat", "2 beats", "4 beats", "8 beats" };
    const auto defaults = ozo::midi::defaultLanes();
    for (std::size_t lane = 0; lane < defaults.size(); ++lane)
    {
        const auto values = laneDefaults (defaults[lane]);
        const auto prefix = "L" + juce::String (static_cast<int> (lane) + 1) + " ";
        for (std::size_t field = 0; field < laneFields.size(); ++field)
        {
            const juce::ParameterID id { laneParameterID (lane, laneFields[field]), 6 };
            const auto name = prefix + laneNames[field];
            if (field == 0)
                layout.add (std::make_unique<juce::AudioParameterBool> (
                    id, name, defaults[lane].enabled));
            else if (laneInteger[field])
                layout.add (std::make_unique<juce::AudioParameterChoice> (
                    id, name, field == 7 ? cycles : divisions, static_cast<int> (values[field])));
            else
            {
                const auto maximum = static_cast<float> (laneMaximum[field]);
                const auto attributes = juce::AudioParameterFloatAttributes()
                    .withStringFromValueFunction ([] (float value, int)
                    {
                        return juce::String (juce::roundToInt (value * 100.0f)) + "%";
                    })
                    .withValueFromStringFunction ([maximum] (const juce::String& text)
                    {
                        return safeLaneFloat (text.trim().trimCharactersAtEnd ("% ").getFloatValue()
                                              / 100.0f, maximum, 0.0f);
                    });
                layout.add (std::make_unique<juce::AudioParameterFloat> (
                    id, name, juce::NormalisableRange<float> { 0.0f, maximum },
                    static_cast<float> (values[field]), attributes));
            }
        }
    }
    return layout;
}

void OzoGlintProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    juce::ignoreUnused (maximumExpectedSamplesPerBlock);
    scratchMidi.clear();
    scratchMidi.ensureSize (midiReserveBytes);
    const double effectiveRate = std::isfinite (sampleRate) && sampleRate >= 1.0 && sampleRate <= 768000.0
        ? sampleRate : 44100.0; // Match the core's accepted sample-rate range.
    engine.prepare (effectiveRate);
    visualMillisecondsPerSample = 1000.0 / effectiveRate;
    visualRevision.fetch_add (1, std::memory_order_acq_rel);
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
    visualRevision.fetch_add (1, std::memory_order_acq_rel);
}

bool OzoGlintProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (! layouts.inputBuses.isEmpty())
        return false;
    if (isMidiEffect())
        return layouts.outputBuses.isEmpty();
    return layouts.outputBuses.size() == 1
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void OzoGlintProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    buffer.clear();
    processMidi (buffer.getNumSamples(), midi, false);
}

void OzoGlintProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midi)
{
    buffer.clear();
    processMidi (buffer.getNumSamples(), midi, false);
}

void OzoGlintProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer,
                                                juce::MidiBuffer& midi)
{
    buffer.clear();
    processMidi (buffer.getNumSamples(), midi, true);
}

void OzoGlintProcessor::processBlockBypassed (juce::AudioBuffer<double>& buffer,
                                                juce::MidiBuffer& midi)
{
    buffer.clear();
    processMidi (buffer.getNumSamples(), midi, true);
}

ozo::midi::Controls OzoGlintProcessor::readControls (bool bypassed) noexcept
{
    const auto visual = visualRevision.load (std::memory_order_acquire);
    const auto revision = stateRevision.load (std::memory_order_acquire);
    if ((revision & 1u) == 0)
    {
        auto next = audioControls;
        next.resetSerial = resetSerial.load (std::memory_order_acquire);
        next.seed = seed.load (std::memory_order_relaxed);
        next.amount = safeAmount (amountValue->load (std::memory_order_relaxed));
        next.outputMode = static_cast<ozo::midi::OutputMode> (
            safeChoice (outputModeValue->load (std::memory_order_relaxed), 1, 0));
        next.root = safeChoice (rootValue->load (std::memory_order_relaxed), 11, 0);
        next.scale = static_cast<ozo::midi::Scale> (
            safeChoice (scaleValue->load (std::memory_order_relaxed), 2, 0));
        next.manualLanes = true;
        const auto defaults = ozo::midi::defaultLanes();
        for (std::size_t lane = 0; lane < laneValues.size(); ++lane)
        {
            const auto& raw = laneValues[lane];
            const auto& fallback = defaults[lane];
            auto& controls = next.lanes[lane];
            controls.enabled = safeChoice (raw[0]->load (std::memory_order_relaxed), 1,
                                           fallback.enabled ? 1 : 0) != 0;
            controls.first = safeChoice (raw[1]->load (std::memory_order_relaxed), 9, fallback.first);
            controls.repeat = safeChoice (raw[2]->load (std::memory_order_relaxed), 9, fallback.repeat);
            controls.feedback = safeLaneFloat (raw[3]->load (std::memory_order_relaxed), 0.98f, fallback.feedback);
            controls.level = safeLaneFloat (raw[4]->load (std::memory_order_relaxed), 1.0f, fallback.level);
            controls.gate = safeChoice (raw[5]->load (std::memory_order_relaxed), 9, fallback.gate);
            controls.motion = safeLaneFloat (raw[6]->load (std::memory_order_relaxed), 1.0f, fallback.motion);
            controls.cycle = safeChoice (raw[7]->load (std::memory_order_relaxed), 3, fallback.cycle);
        }

        // A restore never blocks/spins the audio thread or publishes half of a preset.
        // Use the previous block's snapshot if a state transaction overlaps this read.
        std::atomic_thread_fence (std::memory_order_acquire);
        if (revision == stateRevision.load (std::memory_order_acquire))
        {
            audioControls = next;
            // Keep the previous visual tag if invalidation overlapped this read.
            // In particular, old controls during an odd restore cannot acquire
            // the new revision and masquerade as post-restore events.
            if (visual == visualRevision.load (std::memory_order_acquire))
                audioVisualRevision = visual;
        }
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

    const auto visualSession = visualEvents.producerSession();
    const double blockTimeMs = visualSession != 0 ? juce::Time::getMillisecondCounterHiRes() : 0.0;
    scratchMidi.clear();
    scratchMidi.ensureSize (midiReserveBytes);
    BufferMidiInput input (midi);
    BufferMidiOutput output (scratchMidi);
    const auto controls = readControls (bypassed);
    VisualNoteObserver observer (visualEvents, visualSession, audioVisualRevision,
                                 blockTimeMs, visualMillisecondsPerSample);
    engine.process (numSamples, readTransport(), controls, input, output,
                    visualSession != 0 ? &observer : nullptr);
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

void OzoGlintProcessor::beginVisualSession() noexcept
{
    visualEvents.beginSession();
}

void OzoGlintProcessor::endVisualSession() noexcept
{
    visualEvents.endSession();
}

void OzoGlintProcessor::discardLaneVisualEvents() noexcept
{
    visualEvents.discard();
}

bool OzoGlintProcessor::popLaneVisualEvent (LaneVisualEvent& event) noexcept
{
    const auto revision = visualStateRevision();
    return visualEvents.pop (event, revision) && revision == visualStateRevision();
}

std::uint64_t OzoGlintProcessor::visualStateRevision() const noexcept
{
    return visualRevision.load (std::memory_order_acquire);
}

juce::AudioProcessorEditor* OzoGlintProcessor::createEditor()
{
    return new OzoGlintEditor (*this);
}

juce::String OzoGlintProcessor::selectedUserPresetName() const
{
    const int index = getPresetIndex() - ozo::PresetFactory::kFactoryCount;
    return index >= 0 && index < userPresets.size() ? userPresets.get (index).name : juce::String();
}

void OzoGlintProcessor::remapUserPreset (const juce::String& name)
{
    const int index = userPresets.indexOfName (name);
    if (name.isNotEmpty() && getPresetIndex() >= ozo::PresetFactory::kFactoryCount)
        currentPreset.store (index >= 0 ? ozo::PresetFactory::kFactoryCount + index : -1, std::memory_order_relaxed);
}

juce::Result OzoGlintProcessor::refreshUserPresets()
{
    const auto selected = selectedUserPresetName();
    const auto result = userPresets.refresh();
    remapUserPreset (selected);
    return result;
}

juce::Result OzoGlintProcessor::applyPreset (int index)
{
    juce::String xml;
    const int userIndex = index - ozo::PresetFactory::kFactoryCount;
    if (index >= 0 && index < ozo::PresetFactory::kFactoryCount)
        xml = ozo::PresetFactory::createXml (index, exportSettingsText());
    else if (userIndex >= 0 && userIndex < userPresets.size())
        xml = userPresets.get (userIndex).xml;
    else
        return juce::Result::fail ("Unknown preset index.");

    const juce::ScopedLock lock (stateLock);
    if (! importSettingsText (xml)) return juce::Result::fail ("Preset contains invalid NIGHTSHIFT state.");
    // replaceState synchronously notifies all APVTS listeners; select only after those callbacks finish.
    currentPreset.store (index, std::memory_order_relaxed);
    return juce::Result::ok();
}

juce::Result OzoGlintProcessor::saveUserPreset (const juce::String& name, bool overwrite)
{
    for (int i = 0; i < ozo::PresetFactory::kFactoryCount; ++i)
        if (name.equalsIgnoreCase (ozo::PresetFactory::getName (i)))
            return juce::Result::fail ("Factory presets are read-only; choose another name.");
    const auto selected = selectedUserPresetName();
    const auto result = userPresets.save (name, exportSettingsText(), overwrite);
    if (result.wasOk())
    {
        const int index = userPresets.indexOfName (name);
        currentPreset.store (index >= 0 ? ozo::PresetFactory::kFactoryCount + index : -1, std::memory_order_relaxed);
    }
    else remapUserPreset (selected);
    return result;
}

juce::Result OzoGlintProcessor::deleteUserPreset (int mergedIndex)
{
    if (mergedIndex < ozo::PresetFactory::kFactoryCount) return juce::Result::fail ("Factory presets are read-only.");
    const auto selected = selectedUserPresetName();
    const auto result = userPresets.remove (mergedIndex - ozo::PresetFactory::kFactoryCount);
    remapUserPreset (selected);
    return result;
}

juce::ValueTree OzoGlintProcessor::createStateTree()
{
    const juce::ScopedLock lock (stateLock);
    juce::ValueTree root { stateRootType };
    root.setProperty ("version", stateVersion, nullptr);
    root.setProperty ("seed", static_cast<juce::int64> (seed.load (std::memory_order_relaxed)), nullptr);
    root.addChild (apvts.copyState(), -1, nullptr);

    RestoredState canonical;
    if (! validateState (root, canonical))
        return {};
    root.removeAllChildren (nullptr);
    root.addChild (canonical.parameters, -1, nullptr);
    return root;
}

void OzoGlintProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    const auto root = createStateTree();
    if (root.isValid())
        if (const auto xml = root.createXml())
            copyXmlToBinary (*xml, destination);
}

juce::String OzoGlintProcessor::exportSettingsText()
{
    const auto root = createStateTree();
    if (root.isValid())
        if (const auto xml = root.createXml())
            return xml->toString();
    return {};
}

bool OzoGlintProcessor::importSettingsText (const juce::String& text)
{
    const auto xml = parseSettingsXml (text);
    return xml != nullptr && applyStateTree (juce::ValueTree::fromXml (*xml));
}

void OzoGlintProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // Decode JUCE's XML envelope ourselves so the same bounded, DTD-free parser
    // protects host restores and clipboard imports before any XML is parsed.
    if (data == nullptr || sizeInBytes <= 8 || sizeInBytes > 64 * 1024
        || juce::ByteOrder::littleEndianInt (data) != 0x21324356u)
        return;
    const auto* bytes = static_cast<const char*> (data);
    const auto length = juce::ByteOrder::littleEndianInt (bytes + 4);
    if (length == 0 || length > static_cast<juce::uint32> (sizeInBytes - 8))
        return;
    for (juce::uint32 index = 0; index < length; ++index)
        if (bytes[8 + index] == '\0')
            return;
    importSettingsText (juce::String::fromUTF8 (bytes + 8, static_cast<int> (length)));
}

bool OzoGlintProcessor::applyStateTree (const juce::ValueTree& root)
{
    RestoredState restored;
    if (! validateState (root, restored))
        return false;

    const juce::ScopedLock lock (stateLock);
    clearPresetIndex(); // A host/clipboard restore has no persistent preset identity in the strict v6 XML.
    stateRevision.fetch_add (1, std::memory_order_acq_rel);
    apvts.replaceState (restored.parameters);
    seed.store (restored.seed, std::memory_order_relaxed);
    // The next non-empty audio block emits old-note releases; Output still uses
    // the core's held-note-safe pending-mode policy, never a GUI-side override.
    resetSerial.fetch_add (1, std::memory_order_release);
    visualRevision.fetch_add (1, std::memory_order_acq_rel);
    stateRevision.fetch_add (1, std::memory_order_release);
    return true;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OzoGlintProcessor();
}
