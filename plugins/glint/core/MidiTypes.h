#pragma once

#include <cstdint>
#include <array>
#include <cstddef>

namespace ozo::midi
{
enum class OutputMode : int { add, particles };
enum class Scale : int { unset, major, minor };

inline constexpr std::size_t laneCount = 7;
inline constexpr std::array<double, 10> noteBeats {{ 0.125, 0.1875, 0.25, 0.375, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0 }};
// root, upper third/fifth/octave, lower third/fifth/octave.
struct LaneControls
{
    bool enabled = true;
    int first = 4;
    int repeat = 6;
    float feedback = 0.80f;
    float level = 0.40f;
    int gate = 6;
    float motion = 0.40f;
    int cycle = 2; // 1, 2, 4, 8 beats
};
inline std::array<LaneControls, laneCount> defaultLanes() noexcept
{
    return {{
        {false,6,6,.80f,.22f,6,.40f,2},
        {true,5,6,.82f,.38f,5,.40f,2},
        {true,7,5,.80f,.38f,5,.40f,2},
        {true,4,6,.84f,.40f,7,.40f,2},
        {false,8,7,.80f,.28f,5,.40f,2},
        {false,9,6,.78f,.28f,5,.40f,2},
        {false,9,7,.82f,.28f,7,.40f,2}
    }};
}

struct Controls
{
    float amount = 0.30f;
    OutputMode outputMode = OutputMode::add;
    bool bypass = false;
    std::uint32_t seed = 0x6f7a6f31u;
    std::uint64_t resetSerial = 0;
    int root = 0;
    Scale scale = Scale::unset;
    bool manualLanes = false;
    std::array<LaneControls, laneCount> lanes = defaultLanes();
};

struct Transport
{
    double bpm = 120.0;
    double ppqPosition = 0.0;
    std::int64_t samplePosition = 0;
    bool hasTempo = false;
    bool hasPpq = false;
    bool hasSamplePosition = false;
    bool isPlaying = false;
};

struct MidiEventView
{
    int sampleOffset = 0;
    const std::uint8_t* data = nullptr;
    int size = 0;
};

class MidiInput
{
public:
    virtual ~MidiInput() = default;
    virtual bool next (MidiEventView&) noexcept = 0;
};

class MidiOutput
{
public:
    virtual ~MidiOutput() = default;
    // The receiver copies bytes before returning.
    virtual void push (int sampleOffset, const std::uint8_t* data, int size) noexcept = 0;
};

// A successfully emitted generated Note On, never an input note or a candidate.
// sampleTimestamp is the engine's uint64 render clock (reset by prepare), not
// host transport time. sampleOffset belongs to the current process() block.
struct GeneratedNoteOn
{
    std::size_t laneIndex = 0; // root, +third, +fifth, +octave, -third, -fifth, -octave
    std::uint64_t sampleTimestamp = 0;
    int sampleOffset = 0;
    int velocity = 0; // Exact emitted MIDI velocity, 1..127.
    double onsetBeats = 0.0; // Actual output time relative to its source strike.
    double repeatBeats = 0.0; // Timing snapshot for that source cloud.
};

class GeneratedNoteObserver
{
public:
    virtual ~GeneratedNoteObserver() = default;
    // Synchronous processing-thread observation, after MidiOutput::push().
    // Copy the value if needed; do not allocate, block, re-enter, or alter MIDI.
    virtual void generatedNoteOn (const GeneratedNoteOn&) noexcept = 0;
};

struct EngineStatus
{
    OutputMode activeMode = OutputMode::add;
    bool modeChangePending = false;
    int generatedKeysDown = 0;
    int generatedHeld = 0;
    std::uint64_t generatedNoteOns = 0;
    std::uint64_t skippedCandidates = 0;
};
}
