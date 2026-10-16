#pragma once

#include "MidiTypes.h"

#include <array>
#include <cstdint>

namespace ozo::midi
{
class ParticleEngine
{
public:
    // Processing must be suspended: prepare forgets, rather than emits, old Offs.
    void prepare (double sampleRate) noexcept;
    // Ordered input offsets are in [0, numSamples); output copies immediately.
    // All methods require the processing thread or external serialization.
    // observer is borrowed only for this call; nullptr preserves the unobserved path.
    void process (int numSamples, const Transport&, const Controls&, MidiInput&, MidiOutput&,
                  GeneratedNoteObserver* observer = nullptr) noexcept;
    EngineStatus getStatus() const noexcept;

private:
    using Tick = std::uint64_t;
    static constexpr int keyCount = 16 * 128;
    static constexpr int voiceCount = 32;
    static constexpr int rateCapacity = 48;
    static constexpr Tick never = ~Tick { 0 };

    struct SourceKey
    {
        std::uint64_t presses = 0;
        bool sounding = false;
        bool sostenuto = false;
    };
    struct Channel { bool sustain = false; bool sostenuto = false; };
    struct SourceNote { int key = -1; int velocity = 0; };
    struct Voice
    {
        long double endBeat = 0.0L;
        Tick earliestEnd = 0;
        Tick hardEnd = 0;
        int key = -1;
        bool down = false;
        bool sostenuto = false;
    };
    enum class IntervalRole { unison, upThird, upFifth, upOctave, downThird, downFifth, downOctave };
    struct Lane
    {
        IntervalRole intervalRole = IntervalRole::upOctave;
        long double nextBeat = 0.0L;
        long double firstBeat = 0.0L;
        Tick earliest = 0;
        Tick priorityCap = 0;
        std::uint32_t randomState = 1;
        bool active = false;
        int emitted = 0;
        int shortRun = 0;
        int lastPitch = -1;
        int lastSource = -1;
        double firstGain = 0.40;
        double decay = 0.18;
        double phase = 0.0;
        double wavePeriod = 2.7;
        // Timing is snapshotted at strike; expressive controls are read live.
        LaneControls settings {};
    };
    struct Cloud
    {
        std::array<SourceNote, 8> notes {};
        std::array<Lane, 7> lanes {};
        long double triggerBeat = 0.0L;
        Tick triggerTime = 0;
        Tick hardEnd = 0;
        std::uint64_t order = 0;
        int noteCount = 0;
        int activeLanes = 0;
        int step = 0;
        int newerStrikes = 0;
        bool manual = false;
    };

    static Tick addTicks (Tick, Tick) noexcept;
    static void increment (std::uint64_t&) noexcept;
    static double randomUnit (std::uint32_t&) noexcept;
    Tick durationSamples (double) const noexcept;
    long double beatAt (Tick) const noexcept;
    Tick sampleAtBeat (long double, Tick notBefore) const noexcept;
    bool updateClock (const Transport&, int) noexcept;
    void cancelClouds() noexcept;
    void cancelGeneration (Tick, Tick, MidiOutput&) noexcept;
    void strike (int key, int velocity, Tick) noexcept;
    long double nextGap (Lane&) noexcept;
    Tick cloudTime (const Cloud&, const Lane&, Tick) const noexcept;
    int scaleDegree (int pitch) const noexcept;
    Tick voiceTime (const Voice&, Tick) const noexcept;
    Tick nextScheduledTime (Tick) const noexcept;
    void runBefore (Tick limit, Tick& cursor, Tick blockStart, MidiOutput&, GeneratedNoteObserver*) noexcept;
    void runAt (Tick, Tick blockStart, MidiOutput&, GeneratedNoteObserver*) noexcept;
    double decayUpper (const Cloud&, const Lane&, long double onsetBeat) const noexcept;
    static long double onsetAt (const Cloud&, long double beat) noexcept;
    void updateManualControls (const Controls&) noexcept;
    bool generate (Cloud&, Lane&, Tick, Tick blockStart, MidiOutput&, GeneratedNoteObserver*) noexcept;
    int selectPitch (const Cloud&, Lane&, SourceNote&) noexcept;

    void handleInput (const MidiEventView&, Tick, Tick, MidiOutput&) noexcept;
    void noteOn (int key, int velocity, Tick, Tick, MidiOutput&) noexcept;
    void noteOff (int key, Tick, Tick, MidiOutput&) noexcept;
    void controller (int channel, int cc, int value, Tick, Tick, MidiOutput&) noexcept;
    void refreshSources (int channel) noexcept;
    bool keyAvailable (int key) const noexcept;
    static void emitNote (int offset, int key, bool on, int velocity, MidiOutput&) noexcept;
    void releaseVoice (Voice&, Tick, Tick, MidiOutput&, bool emit = true) noexcept;
    void releaseDue (Tick, Tick, MidiOutput&) noexcept;
    void refreshVoices (int channel) noexcept;
    bool rateAvailable (Tick) noexcept;
    void recordNoteOn (Tick) noexcept;
    void applyPendingMode() noexcept;

    std::array<SourceKey, keyCount> sources_ {};
    std::array<Channel, 16> channels_ {};
    std::array<Voice, voiceCount> voices_ {};
    // The rolling rate budget is deliberately independent of the voice ledger.
    std::array<Tick, rateCapacity> recentOns_ {};
    std::array<Cloud, 8> clouds_ {};
    Tick priorityUntil_ = 0;
    Tick nextEmit_ = 0;
    int rateHead_ = 0;
    int rateSize_ = 0;
    std::uint64_t cloudOrder_ = 0;
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
    bool manualLanes_ = false;
    std::array<LaneControls, laneCount> manualControls_ = defaultLanes();
    int root_ = 0;
    Scale scale_ = Scale::unset;
    OutputMode activeMode_ = OutputMode::add;
    OutputMode requestedMode_ = OutputMode::add;
    std::uint32_t seed_ = 0x6f7a6f31u;
    std::uint32_t randomState_ = 0x6f7a6f31u;
    std::uint64_t resetSerial_ = 0;
    bool enabled_ = false;
    bool controlsSeen_ = false;
};
}
