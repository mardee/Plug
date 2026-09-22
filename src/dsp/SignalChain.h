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
    void advanceSmoothing() noexcept;
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

    // 干信号备份（用于最终 mix 与电平参考）。
    // 大小在 prepare() 里按 maxBlock 一次性分好，process() 里绝不增长 ——
    // 音频线程里 setSize 是分配，宿主塞来超长缓冲时会在实时线程里 malloc。
    juce::AudioBuffer<float> dryBuffer;

    // Auto Match
    LoudnessTracker tracker;
    float matchGainDb     = 0.0f;
    float smoothedMatchDb = 0.0f;

    // 上一块算好的总增益，下一块在过采样域里拿去软限。
    // 软限不能放在基带：tanh 在降采样之后会自己造出折回可闻区的谐波。
    float limGain = 1.0f;

    // 响度参照带：HP@200Hz + LP@6kHz。
    //
    // 之前是 HP@40Hz + LP@2.5kHz（K 计权近似）。问题是 Drive 和 Weight 的能量
    // 几乎全落在这个带里，匹配增益会把它们造成的响度变化如数补回去 ——
    // 谐波结构变了，但听感上的"劲"被吃掉。
    //
    // 下沿抬到 200 Hz：Weight 加出来的低频厚度不再被补偿，那部分响度变化
    // 本身就是染色的"劲"。
    //
    // 上沿不能停在 2 kHz。Air 的激励产物就落在 6 kHz 一带，低通设在 2 kHz
    // 时匹配完全看不见它，补偿追不平 —— 实测 bypass 偏差从 0.3 dB 涨到
    // 1.5 dB，而且集中在 1~2 kHz（正好贴着截止）。放到 6 kHz 才能把 Air
    // 的产物算进参照，同时 8 kHz 以上的空气感仍然留在带外。
    Biquad kInHP[kMaxChannels],  kInLP[kMaxChannels];
    Biquad kOutHP[kMaxChannels], kOutLP[kMaxChannels];

    // 连续参数的块级平滑。
    //
    // 宿主自动化和旋钮拖动都是块边界跳变：drive 从 0.3 跳到 1.0，
    // 预增益从 2.8x 跳到 21x，交接处就是一个 click。这里把 drive / weight /
    // air / glue 四个连续量按约 25 ms 追到目标值，每个音频块推进一步，
    // 然后拿平滑后的值去 setParams。
    //
    // 只平滑这四个。character 是离散档位，平滑没有意义；wild 是模式开关，
    // 它改变的是算法结构而不是一个数，下面单独做交叉淡化。
    float smDrive = 0.35f, smWeight = 0.40f, smAir = 0.35f, smGlue = 0.30f;
    bool  smoothInit = false;

    // 狂野模式交叉淡化。0 = 完全常规，1 = 完全狂野。
    // 直接切换会在一个采样点上把折叠、次八度、毛刺、压缩比全部切过去，
    // 必然 click。两条链不能并行跑（状态会分叉），所以用一个 0..1 的
    // 渐变量去缩放那些"只有狂野才有"的级，约 40 ms 走完。
    float wildMix = 0.0f;

    // 输出软限。阈值 −1 dBFS：低于它严格线性（音色不变），
    // 高于它按 tanh 压住。满档实测 peak 到过 +3.3 dBFS，不能靠宿主削波顶着。
    static constexpr float kCeilingDb  = 0.0f;
    static constexpr float kCeilingLin = 1.0f;
    // 超过阈值后的软拐点宽度。阈值内严格线性，只有真的削波才进 tanh。
    static constexpr float kSoftRange  = 0.122018454f;   // 1 dB

    // 表头数据
    float grDbForMeter  = 0.0f;
    float inputLevelDb  = -100.0f;
    float outputLevelDb = -100.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SignalChain)
};

} // namespace ozo
