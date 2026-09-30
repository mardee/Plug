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

    // getLatencyInSamples() 只报降采样那一半。升采样的群延迟它在内部用
    // 整数延迟补齐了，不计入这个数。干湿混合要对齐的是整条往返的延迟，
    // 只用这一半会让湿信号始终晚一截，mix 不满 100% 时梳状滤波，听着像爆音。
    // 所以这里用一个冲激实测往返峰值的位置，那才是真延迟。
    {
        // 冲激放在第一块的开头，连续跑两块。只跑一块会截断：升采样的群延迟
        // 有一百多个采样，冲激的尾巴要到下一块才从降采样里出来。
        const int probeLen = juce::jmax (blockSize, 256);
        juce::AudioBuffer<float> probe (1, probeLen);

        int peakPos = 0;
        float peak = 0.0f;

        for (int pass = 0; pass < 2; ++pass)
        {
            probe.clear();
            if (pass == 0)
                probe.setSample (0, 0, 1.0f);

            juce::dsp::AudioBlock<float> probeBlock (probe);
            oversampler->processSamplesUp (probeBlock);
            oversampler->processSamplesDown (probeBlock);

            for (int i = 0; i < probeLen; ++i)
            {
                const float a = std::abs (probe.getSample (0, i));
                if (a > peak) { peak = a; peakPos = pass * probeLen + i; }
            }
        }

        latencySamples = peakPos;
        oversampler->reset();
    }
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
    grit.reset();
    output.reset();
    tracker.reset();
    for (int c = 0; c < kMaxChannels; ++c)
    {
        kInHP[c].reset();  kInLP[c].reset();
        kOutHP[c].reset(); kOutLP[c].reset();
    }
    dryBuffer.clear();
    std::fill (&dryDelay[0][0], &dryDelay[0][0] + kMaxChannels * kMaxLatency, 0.0f);
    dryWrite = 0;
    matchGainDb = 0.0f;
    smoothedMatchDb = 0.0f;
    inputLevelDb = outputLevelDb = -100.0f;

    // 平滑状态清掉后立刻对齐到当前参数。不能在这里调 applyParams()：
    // 它读的是平滑值，而平滑值此刻还是上一次的（或默认的），
    // 会把各级用错误的参数重算一遍。
    smoothInit = false;
    smDrive = params.drive;  smWeight = params.weight;
    smAir   = params.air;
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
        smAir   = params.air;
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

    const float wildTarget = params.wild ? 1.0f : 0.0f;
    const float wildCoef   = 1.0f - std::exp (-blockSec / 0.040f);
    wildMix += (wildTarget - wildMix) * wildCoef;

    // 贴到目标就钉死，避免永远差一个舍入误差导致每块都重算各级系数
    auto snap = [] (float& v, float target)
    {
        if (std::abs (v - target) < 1.0e-4f) v = target;
    };
    snap (smDrive, params.drive);   snap (smWeight, params.weight);
    snap (smAir,   params.air);
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
    smAir   = params.air;
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

    // 0.5 处分界：曲线形态（预增益档位）在这里切换。
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

    // 输出变压器保留各模式的基础染色，不再由压缩参数驱动。
    const float outBase = 0.30f + 0.25f * wm;
    const float outAsym = 0.6f  + 0.3f  * wm;
    output.setParams (outBase, profile.asymmetry * outAsym);

    // 折叠、次八度、毛刺只在狂野模式有意义。用 wm 缩放而不是硬切，
    // 这样开关时它们是渐入渐出的，不会在一个采样点上突然出现。
    // 次八度不跟 weight 走。贝斯上 weight 拉满时，次八度方波会盖过基频
    // 12 dB 以上（实测 tone 级峰值从 0.10 冲到 0.91），听感就是爆音。
    // 次八度是狂野模式本身的味道，给一个固定的保守量就够。
    fold.setParams (drive * wm, 0.18f * wm);
    grit.setParams (drive * wm, smWeight * wm);

}

//==============================================================================
// 染色链在过采样域里按采样点推进，各通道独立处理。
static void runChainOnBlock (juce::dsp::AudioBlock<float>& block,
                             PreampStage& preamp,
                             FoldStage&   fold,
                             TapeStage&   tape,
                             ToneStage&   tone,
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

    // 干信号同时推进延迟线，供后面按过采样延迟取用。
    // 写指针是所有通道共用的，所以只在最后推进一次。
    {
        int w = dryWrite;
        const int nc = juce::jmin (chans, kMaxChannels);

        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < nc; ++c)
                dryDelay[c][w] = buffer.getSample (c, i);

            if (++w >= kMaxLatency) w = 0;
        }

        dryWrite = w;
    }

    inputLevelDb = gainToDb (inRms);

    // ---------------------------------------------------------------------
    // 2. 升采样 → 染色链 → 降采样
    // ---------------------------------------------------------------------
    juce::dsp::AudioBlock<float> block (buffer);
    auto upBlock = oversampler->processSamplesUp (block);

    runChainOnBlock (upBlock, preamp, fold, tape, tone, grit, output);

    oversampler->processSamplesDown (block);


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

    // 延迟线的写指针此刻已经停在"下一块的起点"。当前块第 i 个采样
    // 对应的延迟读位置要倒推回去：块尾是 dryWrite-1，块头再往前 n-1。
    const int nc = juce::jmin (chans, kMaxChannels);

    for (int c = 0; c < nc; ++c)
    {
        float* d = buffer.getWritePointer (c);

        for (int i = 0; i < n; ++i)
        {
            // 增益放在响度测量之后，否则补偿会把上一块加上的增益再算一遍。
            // 软限阈值内严格直通，只有超过 0 dBFS 才进 tanh，
            // 所以正常电平下不产生谐波、不改变响度。
            float wet = d[i] * totalGain;
            const float aw = std::abs (wet);
            if (aw > kCeilingLin)
                wet = std::copysign (kCeilingLin + kSoftRange * std::tanh ((aw - kCeilingLin) / kSoftRange), wet);

            // 干信号从跨块延迟线里取，落后湿信号 lat 个采样。
            // 之前是在当前块内回退，块头那 lat 个采样读到的是 0，
            // mix 不满 100% 时每块开头都混进一段静音。
            int r = dryWrite - n + i - lat;
            r %= kMaxLatency;
            if (r < 0) r += kMaxLatency;

            d[i] = dryDelay[c][r] * mixDry + wet * mixWet;
        }

        outSum = juce::jmax (outSum, buffer.getRMSLevel (c, 0, n));
    }

    outputLevelDb = gainToDb (outSum);
}

} // namespace ozo
