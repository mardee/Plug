#pragma once

#include "MidiTypes.h"

#include <array>
#include <cstdint>

namespace ozo::midi
{
class ParticleEngine
{
public:
    // Call only while processing is suspended; this forgets, rather than emits, old Offs.
    void prepare (double sampleRate) noexcept;

    // Input is ordered and its offsets are in [0, numSamples). Output copies immediately.
    // All three methods belong to the processing thread (or require external serialization).
    void process (int numSamples, const Transport&, const Controls&, MidiInput&, MidiOutput&) noexcept;
    EngineStatus getStatus() const noexcept;

private:
    using Tick = std::uint64_t;
    static constexpr int keyCount = 16 * 128;
    static constexpr int voiceCount = 24;
    static constexpr int tailCapacity = 64;
    static constexpr Tick never = ~Tick { 0 };

    struct SourceKey
    {
        std::uint64_t presses = 0;
        Tick lastStrike = 0;
        std::uint64_t order = 0;
        std::uint8_t velocity = 0;
        bool sounding = false;
        bool sostenuto = false;
        bool eligible = false;
    };

    struct Channel
    {
        bool sustain = false;
        bool sostenuto = false;
    };

    struct SourceChoice
    {
        int key = -1;
        int velocity = 0;
        Tick lastStrike = 0;
        std::uint64_t order = 0;
    };

    struct Voice
    {
        long double endBeat = 0.0L;
        Tick hardEnd = 0;
        int key = -1;
        bool down = false;
        bool sostenuto = false;
    };

    struct Activity
    {
        long double startBeat = 0.0L;
        long double endBeat = 0.0L;
        Tick hardEnd = 0;
        double peak = 0.0;
        bool active = false;
    };

    struct Candidate
    {
        long double beat = 0.0L;
        Tick earliest = 0;
        bool valid = false;
    };

    struct Policy
    {
        double rate = 1.0;
        double span = 2.0;
        double velocity = 0.45;
        double upperOctave = 0.1;
        double secondOctave = 0.0;
        double pulseStep = 0.5;
    };

    static Tick addTicks (Tick, Tick) noexcept;
    static void increment (std::uint64_t&) noexcept;
    static Policy mapAmount (double) noexcept;
    Tick durationSamples (double seconds) const noexcept;
    long double beatAt (Tick) const noexcept;
    Tick sampleAtBeat (long double, Tick notBefore) const noexcept;
    bool updateClock (const Transport&, int numSamples) noexcept;
    double randomUnit() noexcept;
    void reseed() noexcept;

    void cancelActivity() noexcept;
    void cancelGeneration (Tick, Tick blockStart, MidiOutput&) noexcept;
    double activityAt (Tick) const noexcept;
    void expireActivity (Tick) noexcept;
    void strike (int velocity, Tick) noexcept;
    void scheduleCandidate (Tick, bool first) noexcept;
    Tick candidateTime (Tick) const noexcept;
    Tick nextScheduledTime (Tick) const noexcept;
    void runBefore (Tick limit, Tick& cursor, Tick blockStart, MidiOutput&) noexcept;
    void runAt (Tick, Tick blockStart, MidiOutput&) noexcept;
    void generate (Tick, Tick blockStart, MidiOutput&) noexcept;

    void handleInput (const MidiEventView&, Tick, Tick blockStart, MidiOutput&) noexcept;
    void noteOn (int key, int velocity, Tick, Tick blockStart, MidiOutput&) noexcept;
    void noteOff (int key, Tick, Tick blockStart, MidiOutput&) noexcept;
    void controller (int channel, int cc, int value, Tick, Tick blockStart, MidiOutput&) noexcept;
    void refreshSources (int channel) noexcept;
    void rememberTail (int key) noexcept;
    void removeChannelTail (int channel) noexcept;
    bool hasSource() const noexcept;
    SourceChoice selectSource (double draw, Tick) noexcept;
    bool poolChoice (int index, bool tail, bool physicalOnly, SourceChoice&) const noexcept;
    bool keyAvailable (int key) const noexcept;

    static void emitNote (int offset, int key, bool on, int velocity, MidiOutput&) noexcept;
    void releaseVoice (Voice&, Tick, Tick blockStart, MidiOutput&, bool emit = true) noexcept;
    void releaseDue (Tick, Tick blockStart, MidiOutput&) noexcept;
    void refreshVoices (int channel) noexcept;
    bool rateAvailable (Tick) noexcept;
    void recordNoteOn (Tick) noexcept;
    void applyPendingMode() noexcept;

    std::array<SourceKey, keyCount> sources_ {};
    std::array<Channel, 16> channels_ {};
    std::array<SourceChoice, tailCapacity> tail_ {};
    std::array<Voice, voiceCount> voices_ {};
    std::array<Tick, voiceCount> recentOns_ {};
    Activity activity_ {};
    Candidate candidate_ {};
    Policy policy_ {};
    int tailSize_ = 0;
    int rateHead_ = 0;
    int rateSize_ = 0;
    int lastSource_ = -1;
    std::uint64_t sourceOrder_ = 0;
    std::uint64_t echoOrder_ = 0;
    std::uint64_t generatedNoteOns_ = 0;
    std::uint64_t skippedCandidates_ = 0;

    double sampleRate_ = 44100.0;
    double bpm_ = 120.0;
    double lastValidBpm_ = 120.0;
    long double beatsPerSample_ = 120.0L / (60.0L * 44100.0L);
    long double anchorBeat_ = 0.0L;
    Tick anchorSample_ = 0;
    Tick sampleClock_ = 0;
    Transport previousTransport_ {};
    int previousBlockSize_ = 0;
    bool transportSeen_ = false;
    bool previousPpqValid_ = false;

    double amount_ = 0.30;
    Character character_ = Character::bloom;
    OutputMode activeMode_ = OutputMode::add;
    OutputMode requestedMode_ = OutputMode::add;
    std::uint32_t seed_ = 0x6f7a6f31u;
    std::uint32_t randomState_ = 0x6f7a6f31u;
    std::uint64_t resetSerial_ = 0;
    bool enabled_ = false;
    bool controlsSeen_ = false;
};
}
