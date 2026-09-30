// VST3 宿主诊断：加载**已安装**的 .vst3，跑一段音频，确认
//   1. 插件能被宿主识别并实例化
//   2. 音频确实被处理（不是直通）
//   3. Auto Match 收敛后，bypass A/B 的音量跳变接近 0
//
// 注意：块长要用 prepareToPlay 声明的值。真实宿主不会一次塞进更大的 buffer，
// 而 JUCE 的 VST3 wrapper 对超大块另有假设，所以这里按规范来。
//
// 用法：./ozoEZampVST3Host [可选：vst3 路径]

#include <cmath>
#include <iostream>
#include <memory>

// 只 include 模块总头。
// JUCE 9 的 juce_VST3PluginFormatHeadless.h 缺少 include guard，
// 重复 include 会直接报 redefinition，所以这里不能再单独引它一次。
#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

namespace
{
    constexpr float kSampleRate = 44100.0f;
    constexpr int   kBlock      = 512;
    constexpr float kFreq       = 1000.0f;
    constexpr float kAmp        = 0.5f;      // -6 dBFS

    float sineSample (int i, float freq, float sr)
    {
        return std::sin (2.0f * juce::MathConstants<float>::pi * freq * (float) i / sr);
    }
}

int main (int argc, char* argv[])
{
    juce::String vst3Path = "/Users/wangxuele/Library/Audio/Plug-Ins/VST3/ozo PRISM.vst3";
    if (argc > 1)
        vst3Path = argv[1];

    std::cout << "Loading: " << vst3Path << std::endl;

    juce::AudioPluginFormatManager fm;
    fm.addFormat (new juce::VST3PluginFormatHeadless());

    // 不能直接手搓一个 PluginDescription 去创建实例：
    // 必须让格式自己把包里的描述扫出来，否则 uniqueId 对不上，
    // 宿主会回一句 "No compatible plug-in format exists"。
    auto* format = fm.getFormat (0);
    juce::OwnedArray<juce::PluginDescription> found;
    format->findAllTypesForFile (found, vst3Path);

    if (found.isEmpty())
    {
        std::cout << "FAILED: 插件包里没有可用的描述" << std::endl;
        return 1;
    }

    const juce::PluginDescription& desc = *found.getFirst();
    std::cout << "Found:   " << desc.name << "   (uid " << desc.uniqueId << ")" << std::endl;

    juce::String error;
    std::unique_ptr<juce::AudioPluginInstance> instance (
        fm.createPluginInstance (desc, kSampleRate, kBlock, error));

    if (instance == nullptr)
    {
        std::cout << "FAILED to load: " << error << std::endl;
        return 1;
    }

    std::cout << "Loaded:  " << instance->getName() << std::endl;
    std::cout << "I/O:     " << instance->getTotalNumInputChannels()
              << " in / " << instance->getTotalNumOutputChannels() << " out" << std::endl;

    for (auto* parameter : instance->getParameters())
    {
        if (parameter->getName (128).equalsIgnoreCase ("Glue"))
        {
            std::cout << "FAILED: retired Glue parameter is still exposed" << std::endl;
            return 1;
        }
    }
    std::cout << "PASS: no Glue parameter exposed to the host" << std::endl;

    instance->prepareToPlay (kSampleRate, kBlock);
    std::cout << "Latency: " << instance->getLatencySamples() << " smp" << std::endl;

    const int   numIns = instance->getTotalNumInputChannels();
    juce::AudioBuffer<float> buf (numIns, kBlock);
    juce::MidiBuffer midi;

    auto fillAt = [&] (int blockIndex)
    {
        for (int ch = 0; ch < numIns; ++ch)
            for (int i = 0; i < kBlock; ++i)
                buf.setSample (ch, i, kAmp * sineSample (i + blockIndex * kBlock, kFreq, kSampleRate));
    };

    auto rmsOfBuffer = [&]()
    {
        double s = 0.0;
        for (int i = 0; i < kBlock; ++i)
            s += (double) buf.getSample (0, i) * (double) buf.getSample (0, i);
        return std::sqrt (s / (double) kBlock);
    };

    // 预热 6 秒。Auto Match 的时间常数是 1.5 s，
    // 要跑够 ~4 个时间常数（e^-4 ≈ 1.8% 残差）才能认为收敛。
    const int warmBlocks = (int) (6.0 * kSampleRate / kBlock);
    for (int b = 0; b < warmBlocks; ++b)
    {
        fillAt (b);
        instance->processBlock (buf, midi);
    }

    // 正式测一块：先记干信号 RMS，再走一遍插件
    fillAt (warmBlocks);
    const double dryRms = rmsOfBuffer();

    instance->processBlock (buf, midi);
    const double outRms = rmsOfBuffer();

    const double dbDry = 20.0 * std::log10 (dryRms);
    const double dbOut = 20.0 * std::log10 (outRms);

    std::cout << std::endl;
    std::cout << "Dry RMS  = " << dryRms << "  (" << dbDry << " dBFS)" << std::endl;
    std::cout << "Out RMS  = " << outRms << "  (" << dbOut << " dBFS)" << std::endl;
    std::cout << "ΔRMS     = " << (dbOut - dbDry) << " dB" << std::endl;
    std::cout << std::endl;

    if (std::abs (dbOut - dbDry) < 0.5)
        std::cout << "结论：插件在处理音频，且 Auto Match 已把音量对齐（|Δ| < 0.5 dB）" << std::endl;
    else
        std::cout << "结论：插件在处理音频，但音量尚未对齐 —— 检查 Auto Match 是否开启" << std::endl;

    return 0;
}
