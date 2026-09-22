#include "SignalChain.h"

namespace ozo
{

SignalChain::SignalChain()
{
    profile = getCharacterProfile (Character::Tape);
}
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
        // 参照带 200 Hz–6 kHz，见 SignalChain.h 里的说明。
        // 上沿到 6 kHz 是因为 Air 的激励就落在 6k 一带，
        // 截止设在 2 kHz 时它的产物被排除在参照之外，补偿追不平。
        kInHP[c].setOnePoleHP  (sr, 200.0f);
        kInLP[c].setOnePoleLP  (sr, 6000.0f);
        kOutHP[c].setOnePoleHP (sr, 200.0f);
        kOutLP[c].setOnePoleLP (sr, 6000.0f);
    }

    dryBuffer.setSize (juce::jmax (1, numCh), juce::jmax (1, blockSize), false, false, true);

    reset();
    // reset 会清掉平滑状态，所以对齐必须放在它之后。
    // 否则第一次 process 会把平滑值重新钉回默认参数，setParams 设过的值全丢。
    applyParams();
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

    // 平滑状态清掉后立刻对齐到当前参数。不能在这里调 applyParams()：
    // 它读的是平滑值，而平滑值此刻还是上一次的（或默认的），
    // 会把各级用错误的参数重算一遍。
    smoothInit = false;
    smDrive = params.drive;  smWeight = params.weight;
    smAir   = params.air;    smGlue   = params.glue;
    wildMix = params.wild ? 1.0f : 0.0f;
    smoothInit = true;

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

void SignalChain::advanceSmoothing() noexcept
{
    // 第一次直接对齐，否则插件加载时要等 25 ms 才从默认值爬到实际参数，
    // 开头几个块的音色是错的。
    if (! smoothInit)
    {
        smDrive = params.drive;  smWeight = params.weight;
        smAir   = params.air;    smGlue   = params.glue;
        wildMix = params.wild ? 1.0f : 0.0f;
        smoothInit = true;
        return;
    }

    // 每块追一段。系数按"一个音频块"算：25 ms 走完约 63%。
    // 块长 512、48 kHz 时一块约 10.7 ms，所以三块左右到位。

    const float blockSec = (float) juce::jmax (1, maxBlock) / (float) sampleRate;
    const float coef = 1.0f - std::exp (-blockSec / 0.025f);


    smDrive  += (params.drive  - smDrive)  * coef;
    smWeight += (params.weight - smWeight) * coef;
    smAir    += (params.air    - smAir)    * coef;
    smGlue   += (params.glue   - smGlue)   * coef;

    const float wildTarget = params.wild ? 1.0f : 0.0f;
    const float wildCoef   = 1.0f - std::exp (-blockSec / 0.040f);
    wildMix += (wildTarget - wildMix) * wildCoef;

    // 贴到目标就钉死，避免永远差一个舍入误差导致每块都重算各级系数
    auto snap = [] (float& v, float target)
    {
        if (std::abs (v - target) < 1.0e-4f) v = target;
    };
    snap (smDrive, params.drive);   snap (smWeight, params.weight);
    snap (smAir,   params.air);     snap (smGlue,   params.glue);
    if (std::abs (wildMix - wildTarget) < 1.0e-3f) wildMix = wildTarget;

    // 不在这里调 applyParams()。各级系数只在 setParams() 里重算一次。
    // 每块都重算会清掉 biquad 的内部状态，参照滤波器永远停在起振阶段，
    // 实测 bypass 偏差从 0.27 dB 涨到 0.56 dB。
}

void SignalChain::setParams (const ChainParams& p)
{

    const bool needRebuild = (p.hq != params.hq);
    const bool unchanged   = sameParams (p, params);

    if (unchanged && ! needRebuild)
        return;

    params = p;

    // 先把平滑值对齐到新参数，再重算各级。否则 applyParams() 读到的
    // 还是上一次的平滑值，新参数被旧值覆盖，要等下一块才纠正过来。
    smDrive = params.drive;  smWeight = params.weight;
    smAir   = params.air;    smGlue   = params.glue;
    wildMix = params.wild ? 1.0f : 0.0f;
    smoothInit = true;

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

    // 平滑推进不在这里。applyParams 只在参数变化时被调用，而参数长时间
    // 不动才是常态 —— 推进写在这里的话，旋钮一松平滑就停在半路，永远走不到
    // 目标值（实测 bypass 偏差因此从 0.3 dB 涨到 1.5 dB）。
    // 推进在 process() 里每块做一次，见 advanceSmoothing()。

    // 0.5 处分界：曲线形态（压缩比、attack、预增益档位）在这里切换。
    // 连续量已经在平滑，所以切换点前后的增益差很小，听不出接缝。
    // 用平滑后的值而不是原始参数。advanceSmoothing() 每块把它们往目标推一步，
    // 推完才调用这里，所以各级看到的是连续变化而不是块边界上的跳变。
    const float wm = wildMix;
    const bool wild = wm >= 0.5f;

    preamp.setWild (wild);
    tape.setWild   (wild);
    tone.setWild   (wild);
    output.setWild (wild);

    const float drive = juce::jmin (1.0f, smDrive * (1.0f + 0.35f * wm));

    preamp.setParams (drive * profile.preampDriveScale,
                      profile.asymmetry,
                      smWeight,
                      profile.lowShelfHz,
                      profile.weightScale);

    // air 越大 → 磁带高频留得越多（相当于"新磁带 / 高速走带"）。
    // 狂野模式反过来压暗一点：波形折叠已经把中高频塞满了，再开 tone 会刺耳。
    // 两个系数都按 wm 插值，切换时不跳。
    const float toneLo = 0.70f + smAir * 0.55f;
    const float toneHi = 0.55f + smAir * 0.35f;
    const float tapeToneHz = profile.tapeToneHz * (toneLo + (toneHi - toneLo) * wm);

    tape.setAsymmetry (profile.asymmetry * 0.5f);
    tape.setParams (profile.tapeAmount,
                    drive,
                    tapeToneHz,
                    profile.wowFlutter * 0.18f);   // 0.18 ms —— 有"人味"但不跑调

    tone.setParams (smWeight, smAir,
                    profile.lowShelfHz, profile.highShelfHz, profile.airScale,
                    profile.weightScale);

    // glue 同时压低阈值、加大压缩比：0 = 几乎不压，1 = 明显的总线 glue。
    // 狂野档的加成（阈值 −6 dB、压缩比 ×3.5、attack ×0.45）按 wm 渐变。
    const float threshDb = -16.0f - smGlue * 14.0f - 6.0f * wm;
    const float wildBoost = 1.0f + 2.5f * wm;          // 1 → 3.5
    const float ratio    = profile.compRatio * (0.6f + smGlue * 0.9f) * wildBoost;
    const float atkScale = 1.0f - 0.55f * wm;          // 1 → 0.45
    const float relScale = 1.0f - 0.40f * wm;          // 1 → 0.60
    const float kneeDb   = 8.0f - 4.0f * wm;           // 8 → 4
    comp.setParams (threshDb, ratio, kneeDb,
                    profile.compAttackMs * atkScale,
                    profile.compReleaseMs * relScale, 1.0f);

    // 输出级：常态 0.3 起，狂野起点抬到 0.55，都跟着 glue 走
    const float outBase = 0.30f + 0.25f * wm;
    const float outGlue = 0.85f + 0.15f * wm;
    const float outAsym = 0.6f  + 0.3f  * wm;
    output.setParams (outBase + smGlue * outGlue, profile.asymmetry * outAsym);

    // 折叠、次八度、毛刺只在狂野模式有意义。用 wm 缩放而不是硬切，
    // 这样开关时它们是渐入渐出的，不会在一个采样点上突然出现。
    // 次八度不跟 weight 走。贝斯上 weight 拉满时，次八度方波会盖过基频
    // 12 dB 以上（实测 tone 级峰值从 0.10 冲到 0.91），听感就是爆音。
    // 次八度是狂野模式本身的味道，给一个固定的保守量就够。
    fold.setParams (drive * wm, 0.18f * wm);
    grit.setParams (drive * wm, smWeight * wm);

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

    // 平滑每块推进一步。参数不动时也要推 —— 旋钮松开之后的那几块
    // 正是它还在往目标值走的时候。
    advanceSmoothing();

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

    // K 加权（中频带）RMS，仅用于 autoMatch 响度比较，不用于表头
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

    // 干信号备份。缓冲已按 maxBlock 分好，而走到这里的 n 必然 ≤ maxBlock
    // （更长的在上面已切块递归），所以这里不再 setSize —— 音频线程零分配。
    jassert (dryBuffer.getNumChannels() >= chans && dryBuffer.getNumSamples() >= n);

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
    //    固定 makeup gain 补不了随电平变化的量，只能实时测、实时补。
    //
    //    测量必须在软限之前。软限会削掉超过 −1 dBFS 的峰值，如果拿削完的
    //    信号当"输出响度"，追踪器会以为输出永远不够响，补偿无限往上加，
    //    永远追不平（实测偏差从 0.3 dB 涨到 1.9 dB）。
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

    // 湿信号比干信号晚了整个过采样滤波器的延迟。不对齐就混，
    // mix 不是 0 也不是 1 时会梳状滤波。延迟量在 prepare() 里就算好了。
    const int lat = latencySamples;

    float outSum = 0.0f;

    for (int c = 0; c < chans; ++c)
    {
        float* d   = buffer.getWritePointer (c);
        const float* dry = dryBuffer.getReadPointer (c);

        for (int i = 0; i < n; ++i)
        {
            // 增益放在响度测量之后，否则补偿会把上一块加上的增益再算一遍。
            // 软限阈值内严格直通，只有超过 0 dBFS 才进 tanh，
            // 所以正常电平下不产生谐波、不改变响度。
            // 干信号按过采样延迟对齐，否则 mix 不全干不全湿时会梳状滤波。
            float wet = d[i] * totalGain;
            const float aw = std::abs (wet);
            if (aw > kCeilingLin)
                wet = std::copysign (kCeilingLin + kSoftRange * std::tanh ((aw - kCeilingLin) / kSoftRange), wet);
            const float dryS = (i >= lat) ? dry[i - lat] : 0.0f;
            d[i] = dryS * mixDry + wet * mixWet;
        }

        outSum = juce::jmax (outSum, buffer.getRMSLevel (c, 0, n));
    }

    outputLevelDb = gainToDb (outSum);
}

} // namespace ozo
