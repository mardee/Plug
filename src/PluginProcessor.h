#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/SignalChain.h"
#include "dsp/Spectrum.h"
#include "presets/PresetFactory.h"
#include "presets/UserPresets.h"
#include <atomic>

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

    const juce::String getName() const override { return "ozo PRISM"; }
    bool acceptsMidi()  const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.2; }

    // 宿主的程序列表只露出厂预设。用户预设走自己的目录，不进这里，
    // 否则换台电脑打开工程时下标会对不上。
    int  getNumPrograms() override { return PresetFactory::kFactoryCount; }
    int  getCurrentProgram() override { return juce::jmax (0, currentPreset.load()); }
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

    // 由 Editor 调用：把某个预设写进所有参数（会通知宿主，可撤销、可自动化）。
    // index < kFactoryCount 是出厂预设，其余是用户预设。
    void applyPreset (int index);

    // 把当前参数存成用户预设。重名覆盖，满了返回 false。
    bool saveUserPreset (const juce::String& name);
    bool deleteUserPreset (int index);

    // 当前参数的快照，供"存为预设"用
    ChainParams captureParams() const;

    const UserPresets& getUserPresets() const noexcept { return userPresets; }

    // 当前选中的预设。-1 = 手动改过参数，不再对应任何预设。
    // atomic：清掉选中态的回调来自音频线程，读它的是界面线程。
    int  getPresetIndex() const noexcept { return currentPreset.load(); }
    void clearPresetIndex() noexcept { currentPreset.store (-1); }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    void syncParamsFromAPVTS();

    juce::AudioProcessorValueTreeState apvts;
    SignalChain chain;
    SpectrumAnalyser spectrum;
    UserPresets userPresets;

    // -1 = 没有选中任何预设（参数被手动改过）。
    // 默认 -1 而不是某一档：挂上插件时参数是默认值，不一定等于某个预设。
    std::atomic<int> currentPreset { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EZampProcessor)
};

} // namespace ozo
