#pragma once

#include <cstdint>

namespace ozo::midi
{
enum class Character : int { echo, bloom, pulse };
enum class OutputMode : int { add, particles };

struct Controls
{
    float amount = 0.30f;
    Character character = Character::bloom;
    OutputMode outputMode = OutputMode::add;
    bool bypass = false;
    std::uint32_t seed = 0x6f7a6f31u;
    std::uint64_t resetSerial = 0;
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
    // The receiver must copy the bytes before returning.
    virtual void push (int sampleOffset, const std::uint8_t* data, int size) noexcept = 0;
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
