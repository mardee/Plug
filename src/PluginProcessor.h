#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/SignalChain.h"
#include "dsp/Spectrum.h"
#include "presets/PresetFactory.h"

namespace ozo
{

// 参数 ID —— UI 与 DSP 之间唯一的契约
namespace ParamID
{
    const inline juce::String input     { "input"     };
    const inline juce::String drive     { "drive"     };
    const inline juce::String character { "character" };
    const inline juce::String weight    { "weight"    };
    const inline juce::String air       { "air"       };
    const inline juce::String glue      { "glue"      };
    const inline juce::String output    { "output"    };
    const inline juce::String mix       { "mix"       };
    const inline juce::String match     { "match"     };
    const inline juce::String hq        { "hq"        };
    const inline juce::String wild      { "wild"      };
}

//==============================================================================
class EZampProcessor : public juce::AudioProcessor
{
public:
    EZampProcessor();
    ~EZampProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "ozo EZamp"; }
    bool acceptsMidi()  const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.2; }

    int  getNumPrograms() override { return PresetFactory::getNumPresets(); }
    int  getCurrentProgram() override { return currentPreset; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }
    const SignalChain& getChain() const noexcept { return chain; }

    // 界面只读它。音频线程写、界面线程读，中间只有 atomic，没有锁。
    const SpectrumAnalyser& getSpectrum() const noexcept { return spectrum; }

    // 由 Editor 调用：把某个预设写进所有参数（会通知宿主，可撤销、可自动化）
    void applyPreset (int index);

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    void syncParamsFromAPVTS();

    juce::AudioProcessorValueTreeState apvts;
    SignalChain chain;
    SpectrumAnalyser spectrum;
    int currentPreset = 1;   // 默认落在 Guitar —— 用户的干声素材就是吉他

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EZampProcessor)
};

} // namespace ozo
