#include "SignalChain.h"

namespace ozo
{

SignalChain::SignalChain()  { profile = getCharacterProfile (Character::Tape); }
SignalChain::~SignalChain() {}

//==============================================================================
void SignalChain::rebuildOversampler (double sr, int blockSize, int numCh)
{
    sampleRate  = sr;
    maxBlock    = blockSize;
    numChannels = numCh;
    osLog2      = params.hq ? 2 : 1;      // HQ = 4x，否则 2x。两档都抗混叠。

    // FIR 等波纹的阻带衰减比 IIR 多相更陡。混叠分量能不能压到听不见，
    // 全看降采样这一步把 45 kHz 那些假谐波拦得多干净 —— 这里是决定性的。
    // 代价是多几十个采样的延迟，对混音场景无所谓。
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) juce::jmax (1, numCh),
        (size_t) osLog2,
        juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
        true);

    oversampler->initProcessing ((size_t) juce::jmax (1, blockSize));
    latencySamples = (int) oversampler->getLatencyInSamples();
}

void SignalChain::prepare (double sr, int blockSize, int numCh)
{
    rebuildOversampler (sr, blockSize, numCh);

    // 各级跑在过采样后的采样率上 —— 这是抗混叠的前提
    const double osRate = sr * (double) (1 << osLog2);
    preamp.prepare (osRate);
    fold.prepare   (osRate);
    tape.prepare   (osRate);
    tone.prepare   (osRate);
    comp.prepare   (osRate);
    grit.prepare   (osRate);
    output.prepare (osRate);

    tracker.prepare (sr);

    for (int c = 0; c < kMaxChannels; ++c)
    {
        kInHP[c].setOnePoleHP  (sr, 40.0f);
        kInLP[c].setOnePoleLP  (sr, 2500.0f);
        kOutHP[c].setOnePoleHP (sr, 40.0f);
        kOutLP[c].setOnePoleLP (sr, 2500.0f);
    }

    dryBuffer.setSize (juce::jmax (1, numCh), juce::jmax (1, blockSize), false, false, true);

    applyParams();
    reset();
}

void SignalChain::reset()
{
    preamp.reset();
    fold.reset();
    tape.reset();
    tone.reset();
    comp.reset();
    grit.reset();
    output.reset();
    tracker.reset();
    for (int c = 0; c < kMaxChannels; ++c)
    {
        kInHP[c].reset();  kInLP[c].reset();
        kOutHP[c].reset(); kOutLP[c].reset();
    }
    dryBuffer.clear();
    matchGainDb = 0.0f;
    smoothedMatchDb = 0.0f;
    grDbForMeter = 0.0f;
    inputLevelDb = outputLevelDb = -100.0f;
    if (oversampler != nullptr)
        oversampler->reset();
}

//==============================================================================
namespace
{
    // 参数没动就别重算滤波器系数 —— 每个 processBlock 都会调 setParams，
    // 而 biquad 系数里有 pow / exp / sqrt，没必要每块算一遍。
    bool sameParams (const ChainParams& a, const ChainParams& b) noexcept
    {
        return a.inputDb   == b.inputDb
            && a.drive     == b.drive
            && a.character == b.character
            && a.weight    == b.weight
            && a.air       == b.air
            && a.glue      == b.glue
            && a.outputDb  == b.outputDb
            && a.mix       == b.mix
            && a.autoMatch == b.autoMatch
            && a.hq        == b.hq
            && a.wild      == b.wild;   // 漏了这条开关狂野模式时参数不会重算
    }
}

void SignalChain::setParams (const ChainParams& p)
{
    const bool needRebuild = (p.hq != params.hq);
    const bool unchanged   = sameParams (p, params);

    if (unchanged && ! needRebuild)
        return;

    params = p;

    if (needRebuild && oversampler != nullptr)
    {
        rebuildOversampler (sampleRate, maxBlock, numChannels);
        const double osRate = sampleRate * (double) (1 << osLog2);
        preamp.prepare (osRate);
        fold.prepare   (osRate);
        tape.prepare   (osRate);
        tone.prepare   (osRate);
        comp.prepare   (osRate);
        grit.prepare   (osRate);
        output.prepare (osRate);
    }

    applyParams();
}

void SignalChain::applyParams()
{
    profile = getCharacterProfile (params.character);
    const bool wild = params.wild;

    // 狂野档：先把各级切到狂野曲线，再喂参数
    preamp.setWild (wild);
    tape.setWild   (wild);
    tone.setWild   (wild);
    output.setWild (wild);

    // 狂野模式 drive 再推 1.35 倍（封顶 1），让预增益曲线真正顶到头
    const float drive = wild ? juce::jmin (1.0f, params.drive * 1.35f) : params.drive;

    preamp.setParams (drive * profile.preampDriveScale,
                      profile.asymmetry,
                      params.weight,
                      profile.lowShelfHz,
                      profile.weightScale);

    // air 越大 → 磁带高频留得越多（相当于"新磁带 / 高速走带"）。
    // 狂野模式反过来压暗一点：波形折叠已经把中高频塞满了，再开 tone 会刺耳。
    const float tapeToneHz = profile.tapeToneHz
                           * (wild ? (0.55f + params.air * 0.35f)
                                   : (0.70f + params.air * 0.55f));

    // 磁带级不再只吃 60% 的 drive —— 用户要的是"猛"，两级饱和都得顶上去
    tape.setAsymmetry (profile.asymmetry * 0.5f);
    tape.setParams (profile.tapeAmount,
                    drive,
                    tapeToneHz,
                    profile.wowFlutter * 0.18f);   // 0.18 ms —— 有"人味"但不跑调

    tone.setParams (params.weight, params.air,
                    profile.lowShelfHz, profile.highShelfHz, profile.airScale,
                    profile.weightScale);

    // glue 同时压低阈值、加大压缩比：0 = 几乎不压，1 = 明显的总线 glue。
    // 狂野档变压扁机：阈值再降 6 dB、压缩比 ×3.5、attack 快一倍、拐点收窄。
    const float threshDb = -16.0f - params.glue * 14.0f - (wild ? 6.0f : 0.0f);
    const float ratio    = profile.compRatio * (0.6f + params.glue * 0.9f)
                         * (wild ? 3.5f : 1.0f);
    const float atkMs    = profile.compAttackMs  * (wild ? 0.45f : 1.0f);
    const float relMs    = profile.compReleaseMs * (wild ? 0.60f : 1.0f);
    comp.setParams (threshDb, ratio, wild ? 4.0f : 8.0f, atkMs, relMs, 1.0f);

    // 输出级常态保留一点饱和（0.3 起），glue 拉满时到 1.15 —— 第二道压实。
    // 狂野档起点抬到 0.55，非对称也加大。
    output.setParams ((wild ? 0.55f : 0.30f) + params.glue * (wild ? 1.0f : 0.85f),
                      profile.asymmetry * (wild ? 0.9f : 0.6f));

    // 折叠级只在狂野模式工作。常规模式传 0，processSample 第一行就直通返回。
    fold.setParams (wild ? drive : 0.0f, wild ? params.weight : 0.0f);

    // 毛刺级：毛刺量跟 drive 走，密度跟 weight 走（低音素材也要给足颗粒）。
    // 常规模式传 0 —— processSample 第一行就返回。
    grit.setParams (wild ? drive : 0.0f, wild ? params.weight : 0.0f);
}

//==============================================================================
// 五级串在过采样域里跑。压缩器需要跨通道取最大值做立体声联动，
// 所以按采样点推进而不是整块处理。
static void runChainOnBlock (juce::dsp::AudioBlock<float>& block,
                             PreampStage& preamp,
                             FoldStage&   fold,
                             TapeStage&   tape,
                             ToneStage&   tone,
                             CompStage&   comp,
                             GritStage&   grit,
                             OutputStage& output)
{
    const int chans = (int) block.getNumChannels();
    const int n     = (int) block.getNumSamples();
    const int ch    = juce::jmin (chans, kMaxChannels);

    for (int i = 0; i < n; ++i)
    {
        for (int c = 0; c < ch; ++c)
        {
            float s = block.getSample (c, i);
            // 折叠要放在话放之后。试过放在话放之前：那时信号大、折得更狠，
            // 但后面磁带级和输出级的饱和会把折叠出来的高次谐波重新抹平，
            // 实测谐波反而比常规少 2 dB。放在话放之后，折叠产物离输出端更近。
            s = preamp.processSample (c, s);
            s = fold.processSample   (c, s);   // 狂野模式的折叠 + 次八度；常规模式直通
            s = tape.processSample   (c, s);
            s = tone.processSample   (c, s);
            block.setSample (c, i, s);
        }

        // 立体声联动：取所有通道的最大绝对值
        float peak = 0.0f;
        for (int c = 0; c < ch; ++c)
        {
            const float a = std::abs (block.getSample (c, i));
            if (a > peak) peak = a;
        }

        const float g = comp.computeGain (peak);

        for (int c = 0; c < ch; ++c)
        {
            float s = block.getSample (c, i) * g;
            // 毛刺放在压缩之后、输出饱和之前：
            // 放在压缩之前会被压平（正是用户抱怨的"扁"），放这里毛刺原样保留。
            s = grit.processSample   (c, s);
            s = output.processSample (c, s);
            block.setSample (c, i, s);
        }
    }
}

//==============================================================================
void SignalChain::process (juce::AudioBuffer<float>& buffer)
{
    const int n     = buffer.getNumSamples();
    const int chans = buffer.getNumChannels();

    if (n <= 0 || chans <= 0 || oversampler == nullptr)
        return;

    // 过采样器按 maxBlock 分配了内部缓冲。万一宿主（或离线测试）塞进来
    // 更长的 buffer，必须切开处理，否则会写越界。
    if (n > maxBlock)
    {
        const int nc = juce::jmin (chans, kMaxChannels);
        float* ptrs[kMaxChannels] {};

        int start = 0;
        while (start < n)
        {
            const int len = juce::jmin (maxBlock, n - start);

            for (int c = 0; c < nc; ++c)
                ptrs[c] = buffer.getWritePointer (c) + start;

            juce::AudioBuffer<float> sub (ptrs, nc, len);
            process (sub);

            start += len;
        }
        return;
    }

    juce::ScopedNoDenormals noDenormals;

    // ---------------------------------------------------------------------
    // 1. 输入增益 + 保存干信号（作为 bypass 参考与平行混合的干端）
    // ---------------------------------------------------------------------
    const float inGain = dbToGain (params.inputDb);
    float inRms = 0.0f;

    for (int c = 0; c < chans; ++c)
    {
        float* d = buffer.getWritePointer (c);
        for (int i = 0; i < n; ++i)
            d[i] *= inGain;

        inRms = juce::jmax (inRms, buffer.getRMSLevel (c, 0, n));
    }

    // K 加权 RMS（仅用于 autoMatch 响度比较，不用于表头）
    float kInRms = 0.0f;
    for (int c = 0; c < chans; ++c)
    {
        const float* isrc = buffer.getReadPointer (c);
        double iSum = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const float x = kInHP[c].process (kInLP[c].process (isrc[i]));
            iSum += (double) x * x;
        }
        const float rms = (float) std::sqrt (iSum / (double) n);
        if (rms > kInRms) kInRms = rms;
    }

    if (dryBuffer.getNumChannels() < chans || dryBuffer.getNumSamples() < n)
        dryBuffer.setSize (chans, n, false, false, true);

    for (int c = 0; c < chans; ++c)
        dryBuffer.copyFrom (c, 0, buffer, c, 0, n);

    inputLevelDb = gainToDb (inRms);

    // ---------------------------------------------------------------------
    // 2. 升采样 → 五级染色 → 降采样
    // ---------------------------------------------------------------------
    juce::dsp::AudioBlock<float> block (buffer);
    auto upBlock = oversampler->processSamplesUp (block);

    runChainOnBlock (upBlock, preamp, fold, tape, tone, comp, grit, output);

    oversampler->processSamplesDown (block);

    grDbForMeter = comp.getGainReductionDb();

    // ---------------------------------------------------------------------
    // 3. Auto Match —— 把输出响度实时追平输入
    //    这是"bypass A/B 音量不跳"的唯一可靠做法：
    //    固定 makeup gain 补不了随电平变化的量，只能实时测、实时补。
    // ---------------------------------------------------------------------
    float outKRms = 0.0f;
    for (int c = 0; c < chans; ++c)
    {
        const float* src = buffer.getReadPointer (c);
        double sum = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const float x = kOutHP[c].process (kOutLP[c].process (src[i]));
            sum += (double) x * x;
        }
        const float rms = (float) std::sqrt (sum / (double) n);
        if (rms > outKRms) outKRms = rms;
    }

    if (kInRms > 1.0e-5f && outKRms > 1.0e-6f)
        tracker.push (kInRms, outKRms, n);

    const float targetMatchDb = params.autoMatch ? tracker.getCompensationDb() : 0.0f;

    // tracker 本身已经很慢了，这里再夹一层，确保不会出现逐块跳变
    const float smoothCoef = 1.0f - std::exp (-(double) n / (sampleRate * 0.25));
    smoothedMatchDb += (targetMatchDb - smoothedMatchDb) * smoothCoef;
    matchGainDb = smoothedMatchDb;

    // ---------------------------------------------------------------------
    // 4. 匹配增益 + 输出增益，最后与干端混合
    // ---------------------------------------------------------------------
    const float totalGain = dbToGain (matchGainDb + params.outputDb);
    const float mixWet = juce::jlimit (0.0f, 1.0f, params.mix);
    const float mixDry = 1.0f - mixWet;

    float outSum = 0.0f;

    for (int c = 0; c < chans; ++c)
    {
        float* d   = buffer.getWritePointer (c);
        const float* dry = dryBuffer.getReadPointer (c);

        for (int i = 0; i < n; ++i)
            d[i] = dry[i] * mixDry + (d[i] * totalGain) * mixWet;

        outSum = juce::jmax (outSum, buffer.getRMSLevel (c, 0, n));
    }

    outputLevelDb = gainToDb (outSum);
}

} // namespace ozo
