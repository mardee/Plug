#include "PluginProcessor.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
void require (bool condition, const char* message)
{
    if (! condition)
        throw std::runtime_error (message);
}

void set (OzoGlintProcessor& processor, const juce::String& id, float value)
{
    auto* parameter = processor.getAPVTS().getParameter (id);
    require (parameter != nullptr, "Missing test parameter");
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

void configure (OzoGlintProcessor& processor, int onlyLane = -1, float feedback = 0.0f)
{
    set (processor, "amount", .3f);
    set (processor, "root", 0);
    set (processor, "scale", 1);
    set (processor, "outputMode", 1); // Particles: every output Note On is generated.
    for (std::size_t lane = 0; lane < ozo::midi::laneCount; ++lane)
    {
        set (processor, OzoGlintProcessor::laneParameterID (lane, "enabled"),
             onlyLane < 0 || lane == static_cast<std::size_t> (onlyLane) ? 1.0f : 0.0f);
        set (processor, OzoGlintProcessor::laneParameterID (lane, "first"), 0);
        set (processor, OzoGlintProcessor::laneParameterID (lane, "repeat"), 2);
        set (processor, OzoGlintProcessor::laneParameterID (lane, "gate"), 0);
        set (processor, OzoGlintProcessor::laneParameterID (lane, "feedback"), feedback);
        set (processor, OzoGlintProcessor::laneParameterID (lane, "level"), .4f);
        set (processor, OzoGlintProcessor::laneParameterID (lane, "motion"), 0);
    }
}

void source (juce::MidiBuffer& midi, int offset = 117)
{
    midi.addEvent (juce::MidiMessage::noteOn (1, 74, static_cast<juce::uint8> (100)), offset);
    midi.addEvent (juce::MidiMessage::noteOff (1, 74), offset + 1);
}

void testProcessorEvents()
{
    constexpr int frames = 4096;
    constexpr double rate = 48000.0;
    constexpr std::array<int, 7> pitches {{ 74, 77, 81, 86, 71, 67, 62 }};
    OzoGlintProcessor observed, plain;
    for (auto* processor : { &observed, &plain })
    {
        configure (*processor);
        processor->prepareToPlay (rate, frames);
    }
    observed.beginVisualSession();
    std::array<bool, 7> seen {};
    std::size_t generated = 0;
    std::uint64_t blockStart = 0;
    juce::AudioBuffer<float> audio (2, frames), referenceAudio (2, frames);
    for (int block = 0; block < 30; ++block)
    {
        juce::MidiBuffer midi, reference;
        if (block == 0)
        {
            source (midi);
            source (reference);
        }
        const auto before = juce::Time::getMillisecondCounterHiRes();
        observed.processBlock (audio, midi);
        const auto after = juce::Time::getMillisecondCounterHiRes();
        plain.processBlock (referenceAudio, reference);
        require (midi.getNumEvents() == reference.getNumEvents(), "Telemetry changed output count");
        auto expected = reference.cbegin();
        std::size_t ons = 0;
        for (const auto metadata : midi)
        {
            const auto ref = *expected++;
            require (metadata.samplePosition == ref.samplePosition && metadata.numBytes == ref.numBytes,
                     "Telemetry changed output timing or byte count");
            require (std::equal (metadata.data, metadata.data + metadata.numBytes, ref.data),
                     "Telemetry changed MIDI bytes or Off ownership");
            const auto message = metadata.getMessage();
            if (! message.isNoteOn())
                continue;
            ++ons;
            ++generated;
            OzoGlintProcessor::LaneVisualEvent event;
            require (observed.popLaneVisualEvent (event), "Generated On lacks visual event");
            require (event.laneIndex < pitches.size() && message.getNoteNumber() == pitches[event.laneIndex],
                     "Visual event mapped to wrong engine lane");
            seen[event.laneIndex] = true;
            require (event.velocity == message.getVelocity(), "Visual velocity differs from MIDI");
            require (event.sampleOffset == metadata.samplePosition
                     && event.sampleTimestamp == blockStart + static_cast<std::uint64_t> (event.sampleOffset),
                     "Visual sample timestamp/offset is not exact");
            // No host tempo: 120 BPM, measured from the source On, including global delays.
            const double onsetBeats = (static_cast<double> (event.sampleTimestamp) - 117.0)
                                    * 120.0 / (60.0 * rate);
            require (std::abs (event.onsetBeats - onsetBeats) < 1.0e-9,
                     "Visual onset beats differ from actual source-relative MIDI timing");
            require (event.repeatBeats == .25, "Visual repeat beats differ from lane snapshot");
            const double blockClock = event.presentationTimeMs - event.sampleOffset * 1000.0 / rate;
            require (blockClock >= before - .001 && blockClock <= after + .001,
                     "Presentation clock must use HiRes monotonic time plus block offset");
            require (event.stateRevision == observed.visualStateRevision(), "Wrong visual epoch");
        }
        OzoGlintProcessor::LaneVisualEvent extra;
        require (! observed.popLaneVisualEvent (extra), "Duplicate, source or Off visual event");
        require (! plain.popLaneVisualEvent (extra), "Closed editor accumulated visual events");
        if (block == 0)
            require (ons > 0, "Fixture must contain nonzero-offset first echo in its first block");
        blockStart += frames;
    }
    require (generated == pitches.size(), "Feedback-zero fixture must emit one event per lane");
    require (std::all_of (seen.begin(), seen.end(), [] (bool value) { return value; }),
             "Not all seven engine lane identities observed");
    observed.endVisualSession();
}

void testRepeatSnapshot()
{
    constexpr int frames = 4096;
    OzoGlintProcessor processor;
    configure (processor, 3, .75f);
    processor.prepareToPlay (48000.0, frames);
    processor.beginVisualSession();
    juce::AudioBuffer<float> audio (2, frames);
    std::size_t laterEvents = 0;
    for (int block = 0; block < 4; ++block)
    {
        juce::MidiBuffer midi;
        if (block == 0)
            source (midi);
        processor.processBlock (audio, midi);
        OzoGlintProcessor::LaneVisualEvent event;
        std::size_t events = 0;
        while (processor.popLaneVisualEvent (event))
        {
            ++events;
            require (event.laneIndex == 3 && event.repeatBeats == .25,
                     "Current repeat parameter changed an existing cloud's repeat metadata");
        }
        if (block == 0)
        {
            require (events == 1, "Snapshot fixture must emit its first echo before repeat changes");
            // Drain first, then change controls: subsequent events must come from the old cloud.
            set (processor, OzoGlintProcessor::laneParameterID (3, "repeat"), 0);
        }
        else
            laterEvents += events;
    }
    require (laterEvents > 0, "Snapshot fixture did not emit an old-cloud echo after repeat changed");
    processor.endVisualSession();
}

void renderStrike (OzoGlintProcessor& processor)
{
    juce::AudioBuffer<float> audio (2, 12000);
    juce::MidiBuffer midi;
    source (midi);
    processor.processBlock (audio, midi);
    int ons = 0, offs = 0;
    for (const auto event : midi)
    {
        ons += event.getMessage().isNoteOn();
        offs += event.getMessage().isNoteOff();
    }
    require (ons == 1 && offs == 1, "Session fixture must emit one balanced actual echo");
}

void testSessionsAndRevisions()
{
    OzoGlintProcessor processor;
    configure (processor, 3);
    processor.prepareToPlay (48000.0, 12000);
    OzoGlintProcessor::LaneVisualEvent event;
    renderStrike (processor);
    require (! processor.popLaneVisualEvent (event), "Closed consumer returned an event");
    processor.beginVisualSession();
    require (! processor.popLaneVisualEvent (event), "Opening replayed pre-open MIDI");
    renderStrike (processor);
    processor.endVisualSession();
    renderStrike (processor);
    processor.beginVisualSession();
    require (! processor.popLaneVisualEvent (event), "Reopening replayed old/closed events");
    renderStrike (processor);
    processor.discardLaneVisualEvents();
    require (! processor.popLaneVisualEvent (event), "Baseline discard retained events");
    renderStrike (processor);
    require (processor.popLaneVisualEvent (event) && ! processor.popLaneVisualEvent (event),
             "Fresh session did not deliver exactly once");

    renderStrike (processor); // Queue a record to be invalidated without a GUI drain.
    auto revision = processor.visualStateRevision();
    require (! processor.importSettingsText ("invalid"), "Invalid state accepted");
    require (processor.visualStateRevision() == revision, "Invalid restore changed visual epoch");
    const auto state = processor.exportSettingsText();
    require (processor.importSettingsText (state), "Own valid state failed to restore");
    require (processor.visualStateRevision() != revision, "Restore did not invalidate visual state");
    require (! processor.popLaneVisualEvent (event), "Restore replayed pre-restore event");
    renderStrike (processor);
    require (processor.popLaneVisualEvent (event) && event.stateRevision == processor.visualStateRevision(),
             "Post-restore accepted snapshot tagged with wrong revision");

    renderStrike (processor);
    revision = processor.visualStateRevision();
    processor.reset();
    require (processor.visualStateRevision() != revision && ! processor.popLaneVisualEvent (event),
             "Reset did not discard prior visual epoch");
    renderStrike (processor);
    require (processor.popLaneVisualEvent (event), "Reset prevented fresh telemetry");

    renderStrike (processor);
    revision = processor.visualStateRevision();
    processor.prepareToPlay (96000.0, 12000);
    require (processor.visualStateRevision() != revision && ! processor.popLaneVisualEvent (event),
             "Prepare did not discard prior engine-clock epoch");
    juce::AudioBuffer<float> audio (2, 12000);
    juce::MidiBuffer midi;
    source (midi);
    const double before = juce::Time::getMillisecondCounterHiRes();
    processor.processBlock (audio, midi);
    const double after = juce::Time::getMillisecondCounterHiRes();
    require (processor.popLaneVisualEvent (event) && event.sampleTimestamp == 6117
             && event.sampleOffset == 6117, "Prepare did not reset sample timestamp at new sample rate");
    const auto blockClock = event.presentationTimeMs - event.sampleOffset * 1000.0 / 96000.0;
    require (blockClock >= before - .001 && blockClock <= after + .001,
             "New sample rate was not reflected in presentation offset");
    processor.endVisualSession();
}

void testOverflowDoesNotChangeMidi()
{
    OzoGlintProcessor observed, plain;
    for (auto* processor : { &observed, &plain })
    {
        configure (*processor, 3);
        processor->prepareToPlay (48000.0, 12000);
    }
    observed.beginVisualSession();
    juce::AudioBuffer<float> audio (2, 12000), referenceAudio (2, 12000);
    constexpr std::size_t strikes = ozo::nightshift::LaneVisualEventQueue::capacity + 40;
    std::size_t ons = 0, offs = 0;
    for (std::size_t block = 0; block < strikes; ++block)
    {
        juce::MidiBuffer midi, reference;
        source (midi);
        source (reference);
        observed.processBlock (audio, midi);
        plain.processBlock (referenceAudio, reference);
        require (midi.getNumEvents() == reference.getNumEvents(), "Full telemetry queue changed MIDI count");
        auto expected = reference.cbegin();
        for (const auto event : midi)
        {
            const auto ref = *expected++;
            require (event.samplePosition == ref.samplePosition && event.numBytes == ref.numBytes
                     && std::equal (event.data, event.data + event.numBytes, ref.data),
                     "Full telemetry queue changed MIDI output");
            ons += event.getMessage().isNoteOn();
            offs += event.getMessage().isNoteOff();
        }
    }
    require (ons == strikes && offs == strikes, "Overflow lost real MIDI or generated Offs");
    std::size_t received = 0;
    OzoGlintProcessor::LaneVisualEvent event;
    while (observed.popLaneVisualEvent (event))
    {
        require (event.laneIndex == 3 && event.sampleTimestamp == received * 12000 + 3117,
                 "Overflow corrupted, reordered or duplicated an event");
        ++received;
    }
    require (received == ozo::nightshift::LaneVisualEventQueue::capacity,
             "Queue capacity/full-drop contract changed");
    renderStrike (observed);
    require (observed.popLaneVisualEvent (event), "Full queue did not recover after drain");
    observed.endVisualSession();
}

void testQueueConcurrency()
{
    // Exercise the exact channel used by the processor without host/GUI allocations.
    using Queue = ozo::nightshift::LaneVisualEventQueue;
    Queue queue;
    queue.beginSession();
    const auto oldSession = queue.producerSession();
    queue.discard();
    ozo::nightshift::LaneVisualEvent event { 3, 1, 1, 100, 1.25, 7, .375, .625 };
    require (! queue.push (oldSession, event), "Pre-baseline in-flight block was accepted");
    queue.endSession();
    require (! queue.push (queue.producerSession(), event), "Closed queue accepted a record");
    queue.beginSession();
    const auto session = queue.producerSession();
    std::atomic<bool> done { false };
    std::atomic<std::uint64_t> accepted { 0 };
    std::thread producer ([&] {
        for (std::uint64_t index = 1; index <= 100000; ++index)
        {
            const ozo::nightshift::LaneVisualEvent value {
                static_cast<std::size_t> (index % 7), index, static_cast<int> (index % 512),
                static_cast<int> (index % 127 + 1), static_cast<double> (index) + .25, 7,
                static_cast<double> (index) + .5, static_cast<double> (index) + .75
            };
            if (queue.push (session, value))
                accepted.fetch_add (1, std::memory_order_relaxed);
        }
        done.store (true, std::memory_order_release);
    });
    std::uint64_t consumed = 0, last = 0;
    bool valid = true;
    const auto consume = [&] {
        ++consumed;
        const auto index = event.sampleTimestamp;
        valid = valid && index > last && event.laneIndex == index % 7
            && event.sampleOffset == static_cast<int> (index % 512)
            && event.velocity == static_cast<int> (index % 127 + 1)
            && event.presentationTimeMs == static_cast<double> (index) + .25
            && event.onsetBeats == static_cast<double> (index) + .5
            && event.repeatBeats == static_cast<double> (index) + .75;
        last = index;
    };
    while (! done.load (std::memory_order_acquire))
    {
        if (queue.pop (event, 7)) consume();
        else std::this_thread::yield();
    }
    producer.join();
    while (queue.pop (event, 7)) consume();
    require (valid && consumed > 0 && consumed == accepted.load(),
             "SPSC fields tore, reordered, duplicated or lost an accepted event");
    queue.endSession();
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    try
    {
        testProcessorEvents();
        std::cout << "PASS exact processor lane/time/velocity; unchanged MIDI; no duplicates\n";
        testRepeatSnapshot();
        std::cout << "PASS old-cloud repeat metadata survives current repeat changes\n";
        testSessionsAndRevisions();
        std::cout << "PASS close/reopen, baseline discard, restore/reset/prepare revisions\n";
        testOverflowDoesNotChangeMidi();
        std::cout << "PASS overflow drops visuals only; preserves MIDI and Offs\n";
        testQueueConcurrency();
        std::cout << "PASS bounded SPSC session filtering and concurrent whole-event handoff\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
