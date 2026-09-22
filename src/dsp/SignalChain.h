#pragma once

#include "Saturation.h"
#include "Stages.h"

namespace ozo
{

//==============================================================================
// 高层参数。UI 只跟这 9 个数打交道，内部怎么分配给各级由 SignalChain 决定。
struct ChainParams
{
    float     inputDb   = 0.0f;
    float     drive     = 0.35f;   // 0..1  染色强度
    Character character = Character::Tape;
    float     weight    = 0.40f;   // 0..1  低频厚度
    float     air       = 0.35f;   // 0..1  高频空气
    float     glue      = 0.30f;   // 0..1  总线压缩量
    float     outputDb  = 0.0f;
    float     mix       = 1.0f;    // 0..1  平行混合（只在最终输出做一次）
    bool      autoMatch = true;    // bypass 电平匹配
    bool      hq        = true;    // 4x 过采样
    bool      wild      = false;   // 狂野模式：换算法 + 全部加倍 + 界面转深色
};

//==============================================================================
// SignalChain
//
// 相比上一版的四个关键改变：
//  1. 所有非线性（饱和）都在 4x 过采样下计算 → 谐波不会折叠回可听频段
//  2. 饱和改为非对称 → 真的有偶次谐波 → 真的会"暖"
//  3. 压缩器在线性域做包络检测 → 它终于真的会压缩
//  4. Auto Match：实时把输出响度追平输入 → bypass A/B 不再有音量跳变
class SignalChain
{
public:
    SignalChain();
    ~SignalChain();

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    void process (juce::AudioBuffer<float>& buffer);

    void setParams (const ChainParams& p);
    const ChainParams& getParams() const noexcept { return params; }

    // UI 数据
    float getGainReductionDb() const noexcept { return grDbForMeter; }
    float getInputLevelDb()    const noexcept { return inputLevelDb; }
    float getOutputLevelDb()   const noexcept { return outputLevelDb; }
    float getMatchGainDb()     const noexcept { return matchGainDb; }
    int   getLatencySamples()  const noexcept { return latencySamples; }

private:
    void applyParams();
    void rebuildOversampler (double sampleRate, int maxBlockSize, int numChannels);

    ChainParams params;
    CharacterProfile profile;

    PreampStage preamp;
    FoldStage   fold;      // 狂野模式专属（常规模式下 setParams(0,0) 直接直通）
    TapeStage   tape;
    ToneStage   tone;
    CompStage   comp;
    GritStage   grit;      // 狂野模式专属：毛刺/不规则噪声（常规模式直通）
    OutputStage output;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    int    osLog2         = 2;    // 4x
    int    numChannels    = 2;
    double sampleRate     = 48000.0;
    int    maxBlock       = 512;
    int    latencySamples = 0;

    // 干信号备份（用于最终 mix 与电平参考）
    juce::AudioBuffer<float> dryBuffer;

    // Auto Match
    LoudnessTracker tracker;
    float matchGainDb     = 0.0f;
    float smoothedMatchDb = 0.0f;

    // K 权重（近似 ITU-R BS.1770）：HP@40Hz + LP@2.5kHz。
    // autoMatch 必须用 K 加权 RMS 而非纯 RMS——纯 RMS 会把高频谐波（饱和/磁带/air
    // 激励产生的互调）误判成响度，导致在 1-2kHz 单频测试音下过度补偿、bypass 跳 ~0.6dB。
    Biquad kInHP[kMaxChannels],  kInLP[kMaxChannels];
    Biquad kOutHP[kMaxChannels], kOutLP[kMaxChannels];

    // 表头数据
    float grDbForMeter  = 0.0f;
    float inputLevelDb  = -100.0f;
    float outputLevelDb = -100.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SignalChain)
};

} // namespace ozo
