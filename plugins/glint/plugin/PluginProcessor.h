#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../core/ParticleEngine.h"
#include "NightShiftTelemetry.h"
#include "../presets/PresetFactory.h"
#include "../presets/UserPresets.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

class OzoGlintProcessor final : public juce::AudioProcessor,
                                private juce::AudioProcessorValueTreeState::Listener
{
public:
    OzoGlintProcessor();
    ~OzoGlintProcessor() override;

    static constexpr const char* amountParameterID = "amount";
    static constexpr const char* outputModeParameterID = "outputMode";
    static constexpr const char* rootParameterID = "root";
    static constexpr const char* scaleParameterID = "scale";
    static constexpr const char* stateRootType = "OZO_MIDI_DELAY_STATE";
    static constexpr const char* parameterTreeType = "PARAMETERS";
    static constexpr int stateVersion = 6;
    static juce::String laneParameterID (std::size_t lane, const char* field);

    const juce::String getName() const override;
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return wrapperType == wrapperType_AudioUnit; }
    bool supportsDoublePrecisionProcessing() const override { return true; }

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;

    // Bounds MIDI scheduling (48 s safety cap + final gate), not the instrument's release.
    double getTailLengthSeconds() const override { return 52.0; }

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destination) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Clipboard XML only; these methods neither read files nor access the network.
    juce::String exportSettingsText();
    bool importSettingsText (const juce::String& text);

    // PRISM-style merged indices: factory first, then the name-sorted user catalogue.
    // File/catalogue operations are message-thread only; selection survives editor reopening.
    juce::Result applyPreset (int index);
    juce::Result saveUserPreset (const juce::String& name, bool overwrite = false);
    juce::Result deleteUserPreset (int mergedIndex);
    const ozo::UserPresets& getUserPresets() const noexcept { return userPresets; }
    juce::Result refreshUserPresets();
    int getPresetIndex() const noexcept { return currentPreset.load (std::memory_order_relaxed); }
    void clearPresetIndex() noexcept { currentPreset.store (-1, std::memory_order_relaxed); }

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }
    const juce::AudioProcessorValueTreeState& getAPVTS() const noexcept { return apvts; }

    // These read published atomics only; the editor never reads the engine.
    ozo::midi::OutputMode getActiveOutputMode() const noexcept;
    bool isOutputModePending() const noexcept;

    using LaneVisualEvent = ozo::nightshift::LaneVisualEvent;
    // SINGLE message-thread editor consumer only. Begin in the editor constructor,
    // end in its destructor; discard rotates the baseline without closing it.
    // None of these calls touches engine state or affects MIDI/Off scheduling.
    void beginVisualSession() noexcept;
    void endVisualSession() noexcept;
    void discardLaneVisualEvents() noexcept;
    bool popLaneVisualEvent (LaneVisualEvent&) noexcept;
    // Any thread may read this single atomic. A change (prepare/reset/restore)
    // invalidates ALL local animation state. Clear it and discard queued events.
    // pop filters old revisions; recheck the revision after a GUI drain if a host
    // restore may overlap it. Age events with Time::getMillisecondCounterHiRes(),
    // and do not fire future-dated events before their presentationTimeMs.
    std::uint64_t visualStateRevision() const noexcept;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    ozo::midi::Controls readControls (bool bypassed) noexcept;
    ozo::midi::Transport readTransport() const;
    void processMidi (int numSamples, juce::MidiBuffer&, bool bypassed);
    void publishEngineStatus() noexcept;
    juce::ValueTree createStateTree();
    bool applyStateTree (const juce::ValueTree& root);
    void parameterChanged (const juce::String&, float) override { clearPresetIndex(); }
    juce::String selectedUserPresetName() const;
    void remapUserPreset (const juce::String& name);

    static constexpr std::size_t midiReserveBytes = 64u * 1024u;

    juce::AudioProcessorValueTreeState apvts;
    ozo::UserPresets userPresets;
    std::atomic<int> currentPreset { -1 }; // UI selection only; never added to the strict v6 state.
    std::atomic<float>* const amountValue;
    std::atomic<float>* const outputModeValue;
    std::atomic<float>* const rootValue;
    std::atomic<float>* const scaleValue;
    // enabled, first, repeat, feedback, level, gate, motion, cycle; resolved once.
    std::array<std::array<std::atomic<float>*, 8>, ozo::midi::laneCount> laneValues {};

    // Only lifecycle/audio callbacks touch engine, scratchMidi, and audioControls.
    ozo::midi::ParticleEngine engine;
    juce::MidiBuffer scratchMidi;
    ozo::midi::Controls audioControls;
    ozo::nightshift::LaneVisualEventQueue visualEvents;
    double visualMillisecondsPerSample = 1000.0 / 44100.0;
    std::uint64_t audioVisualRevision = 0; // Tags the last accepted controls snapshot.
    std::atomic<std::uint64_t> visualRevision { 0 };

    // State operations may run on a host thread. No state lock is taken by the audio callback.
    juce::CriticalSection stateLock;
    std::atomic<std::uint32_t> seed { ozo::midi::Controls {}.seed };
    std::atomic<std::uint64_t> resetSerial { 0 };
    std::atomic<std::uint64_t> stateRevision { 0 };
    // Bit 0: active Particles mode. Bit 1: a mode change is pending.
    std::atomic<unsigned int> publishedStatus { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OzoGlintProcessor)
};
