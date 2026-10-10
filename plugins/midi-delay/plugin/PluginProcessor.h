#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../core/ParticleEngine.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

class OzoMidiDelayProcessor final : public juce::AudioProcessor
{
public:
    OzoMidiDelayProcessor();
    ~OzoMidiDelayProcessor() override = default;

    static constexpr const char* amountParameterID = "amount";
    static constexpr const char* characterParameterID = "character";
    static constexpr const char* outputModeParameterID = "outputMode";
    static constexpr const char* stateRootType = "OZO_MIDI_DELAY_STATE";
    static constexpr const char* parameterTreeType = "PARAMETERS";
    static constexpr int stateVersion = 1;

    const juce::String getName() const override;
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return true; }
    bool supportsDoublePrecisionProcessing() const override { return true; }

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;

    // Bounds our MIDI scheduling (8 s activity + 2 s gate), not an external instrument's release.
    double getTailLengthSeconds() const override { return 10.0; }

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destination) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }
    const juce::AudioProcessorValueTreeState& getAPVTS() const noexcept { return apvts; }

    // These read published atomics only; the editor never reads the engine.
    ozo::midi::OutputMode getActiveOutputMode() const noexcept;
    bool isOutputModePending() const noexcept;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    ozo::midi::Controls readControls (bool bypassed) noexcept;
    ozo::midi::Transport readTransport() const;
    void processMidi (int numSamples, juce::MidiBuffer&, bool bypassed);
    void publishEngineStatus() noexcept;

    static constexpr std::size_t midiReserveBytes = 64u * 1024u;

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* const amountValue;
    std::atomic<float>* const characterValue;
    std::atomic<float>* const outputModeValue;

    // Only lifecycle/audio callbacks touch engine, scratchMidi, and audioControls.
    ozo::midi::ParticleEngine engine;
    juce::MidiBuffer scratchMidi;
    ozo::midi::Controls audioControls;

    // State operations may run on a host thread. No state lock is taken by the audio callback.
    juce::CriticalSection stateLock;
    std::atomic<std::uint32_t> seed { ozo::midi::Controls {}.seed };
    std::atomic<std::uint64_t> resetSerial { 0 };
    std::atomic<std::uint64_t> stateRevision { 0 };
    // Bit 0: active Particles mode. Bit 1: a mode change is pending.
    std::atomic<unsigned int> publishedStatus { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OzoMidiDelayProcessor)
};
