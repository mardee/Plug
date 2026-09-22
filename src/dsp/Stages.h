#pragma once

#include "Saturation.h"

namespace ozo
{

// 每个 stage 最多支持的通道数。插件按宿主实际通道数取用。
static constexpr int kMaxChannels = 8;

//==============================================================================
// Character：三种经典染色味道。
// 它不是简单换预设，而是同时改变饱和的对称性、滤波曲线和压缩的性格。
enum class Character
{
    Tape    = 0,  // 磁带：对称为主，高频柔化，低频 head bump，带轻微 wow/flutter
    Tube    = 1,  // 电子管：强非对称，偶次谐波最丰富，中频厚，高频略暗
    Console = 2   // 调音台：中等非对称，低频紧实，高频开，压缩更有 ATTACK 感
};

struct CharacterProfile
{
    float       asymmetry      = 0.35f;  // 饱和非对称度 → 偶次谐波量
    float       preampDriveScale = 1.0f;
    float       tapeAmount     = 1.0f;   // 磁带级参与程度
    float       tapeToneHz     = 12000.0f;
    float       highShelfHz    = 8000.0f;
    float       airScale       = 1.0f;
    float       lowShelfHz     = 110.0f;
    float       weightScale    = 1.0f;
    float       compAttackMs   = 12.0f;
    float       compReleaseMs  = 140.0f;
    float       compRatio      = 3.0f;
    float       wowFlutter     = 1.0f;
    const char* name           = "Tape";
};

inline CharacterProfile getCharacterProfile (Character c)
{
    CharacterProfile p;
    switch (c)
    {
        case Character::Tube:
            p.asymmetry        = 0.85f;   // 偶次谐波最丰富
            p.preampDriveScale = 1.25f;
            p.tapeAmount       = 0.55f;
            p.tapeToneHz       = 10000.0f;
            p.highShelfHz      = 6000.0f;
            p.airScale         = 0.8f;
            p.lowShelfHz       = 130.0f;
            p.weightScale      = 1.15f;
            p.compAttackMs     = 18.0f;
            p.compReleaseMs    = 180.0f;
            p.compRatio        = 2.5f;
            p.wowFlutter       = 0.0f;    // 电子管没有走带抖动
            p.name             = "Tube";
            break;

        case Character::Console:
            p.asymmetry        = 0.5f;
            p.preampDriveScale = 0.9f;
            p.tapeAmount       = 0.4f;
            p.tapeToneHz       = 16000.0f;
            p.highShelfHz      = 8500.0f;
            p.airScale         = 1.25f;
            p.lowShelfHz       = 90.0f;
            p.weightScale      = 0.9f;
            p.compAttackMs     = 8.0f;
            p.compReleaseMs    = 110.0f;
            p.compRatio        = 4.0f;
            p.wowFlutter       = 0.0f;
            p.name             = "Console";
            break;

        case Character::Tape:
        default:
            p.asymmetry        = 0.35f;
            p.preampDriveScale = 1.0f;
            p.tapeAmount       = 1.0f;
            p.tapeToneHz       = 12000.0f;
            p.highShelfHz      = 7000.0f;
            p.airScale         = 1.0f;
            p.lowShelfHz       = 110.0f;
            p.weightScale      = 1.0f;
            p.compAttackMs     = 12.0f;
            p.compReleaseMs    = 140.0f;
            p.compRatio        = 3.0f;
            p.wowFlutter       = 1.0f;
            p.name             = "Tape";
            break;
    }
    return p;
}

//==============================================================================
// Wow & flutter：磁带走带速度的极微小周期性抖动。
// 这是磁带听起来"活"的关键之一 —— 但它不是音高问题，而是时间轴的呼吸。
// 默认幅度极小（约 0.15 ms），足以给声音加"人味"，又不会让人觉得跑调。
class WowFlutter
{
public:
    void prepare (double sr) noexcept
    {
        sampleRate = sr;
        incWow     = 2.0 * juce::MathConstants<double>::pi * 0.6 / sr;   // 0.6 Hz
        incFlutter = 2.0 * juce::MathConstants<double>::pi * 6.3 / sr;   // 6.3 Hz
        reset();
    }

    void reset() noexcept
    {
        std::fill (std::begin (buf), std::end (buf), 0.0f);
        wpos = 0; phaseWow = 0.0; phaseFlutter = 0.0;
    }

    // amount: 调制深度（采样数）。0 时完全直通。
    inline float process (float x, float amount) noexcept
    {
        buf[wpos] = x;

        if (amount <= 0.0001f)
        {
            wpos = (wpos + 1) % kBufSize;
            return buf[(wpos + kBufSize - kBaseDelay) % kBufSize];
        }

        const float mod = amount * (0.65f * std::sin (phaseWow)
                                  + 0.35f * std::sin (phaseFlutter));
        float rp = (float) wpos - (kBaseDelay + mod);
        while (rp < 0.0f) rp += (float) kBufSize;

        const int   i0   = (int) rp;
        const float frac = rp - (float) i0;
        const int   i1   = (i0 + 1) % kBufSize;
        const float y    = buf[i0] * (1.0f - frac) + buf[i1] * frac;

        phaseWow     += incWow;
        phaseFlutter += incFlutter;
        if (phaseWow     > 2.0 * juce::MathConstants<double>::pi) phaseWow     -= 2.0 * juce::MathConstants<double>::pi;
        if (phaseFlutter > 2.0 * juce::MathConstants<double>::pi) phaseFlutter -= 2.0 * juce::MathConstants<double>::pi;

        wpos = (wpos + 1) % kBufSize;
        return y;
    }

private:
    static constexpr int kBufSize   = 512;
    static constexpr int kBaseDelay = 64;

    double sampleRate = 48000.0;
    double incWow = 0.0, incFlutter = 0.0;
    double phaseWow = 0.0, phaseFlutter = 0.0;
    int    wpos = 0;
    float  buf[kBufSize] { 0.0f };
};

//==============================================================================
// 第一级：Preamp —— 话放 / 输入变压器
// 低频厚度（weight）+ 非对称饱和。偶次谐波主要在这里产生。
class PreampStage
{
public:
    void prepare (double sr) noexcept
    {
        sampleRate = sr;
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            shelf[ch].reset();
            dc[ch].prepare (sr);
        }
        update();
    }

    void reset() noexcept
    {
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            shelf[ch].reset();
            dc[ch].reset();
        }
    }

    // drive: 0..1   asym: 0..1（偶次谐波量）   weight: 0..1（低频厚度）
    void setParams (float drive, float asym, float weight, float shelfHz, float weightScale) noexcept
    {
        driveNorm   = juce::jlimit (0.0f, 1.0f, drive);
        asymmetry   = juce::jlimit (0.0f, 1.0f, asym);
        weightNorm  = juce::jlimit (0.0f, 1.0f, weight);
        shelfFreq   = shelfHz;
        weightScale_ = weightScale;
        update();
    }

    // 狂野档：预增益曲线 1..21x → 1..71x，非对称拉满（偶次谐波给到顶），
    // 低架厚度 9 → 13 dB。狂野模式要的是"推一点就炸"，不是线性变响。
    void setWild (bool w) noexcept { wild_ = w; update(); }

    inline float processSample (int ch, float x) noexcept
    {
        // 1. 低频架：给厚度。增益随 weight 从 0 到 +9 dB（狂野 +13 dB）。
        float s = shelf[ch].process (x);

        // 2. 驱动 + 非对称饱和。
        //    driveGain 同时除以回去，保证小信号严格 unity gain。
        const float g = driveGain;
        float y = asymTanh (s * g, asymEff_) / g;

        // 3. 掐掉非对称带来的 DC
        return dc[ch].process (y);
    }

private:
    void update() noexcept
    {
        // drive 0..1 → 1x .. 21x 预增益（平方曲线：小档依然温和，大档直接压扁）
        // 上一版是 1..6x，实测在大信号上只压了 1~2 dB，几乎听不出存在感。
        // 现在满档 21x 意味着 0.1 的信号进去已经进入 tanh 的硬拐点区——
        // 这才是"挂上去就知道加了东西"的量。
        // 狂野档 1..26x。不能再往上给：40x 会把 0.3 的信号完全压成方波，
        // 而波形折叠是逐采样映射 —— 方波只有两个电平，折完还是方波，
        // 一点新谐波都出不来（实测 40x 时狂野比常规只多 0.5 dB 谐波）。
        // 狂野的"加倍"主要交给折叠器去做，不是靠无限加大饱和。
        // 注意：不能靠无限加大预增益来"给劲儿"。tanh(x·g)/g 在 g 很大时趋近硬限幅，
        // 传输曲线变成方波，谐波比有数学上限（方波 THD ≈ −6 dB），再推 g 只是
        // 把音量压得更小，听感反而更瘪。实测 0.7→1.0 只能多 2 dB 就是撞了这个顶。
        // 所以顶部的"劲儿"交给非对称：偶次谐波是方波里没有的东西，
        // 加它才能真正越过那条天花板。
        driveGain = 1.0f + driveNorm * driveNorm * (wild_ ? 25.0f : 20.0f);

        // 非对称随 drive 往上爬：小档保持温和，大档给足偶次谐波（growl / 劲儿）。
        // asymTanh 内部会把 a 夹到 [0,1]，所以这里不用手动 clamp。
        asymEff_  = asymmetry * (wild_ ? 1.5f : (1.0f + driveNorm * 1.5f));

        const float gainDb = weightNorm * (wild_ ? 13.0f : 9.0f) * weightScale_;
        for (int ch = 0; ch < kMaxChannels; ++ch)
            shelf[ch].setLowShelf (sampleRate, shelfFreq, 0.707f, gainDb);
    }

    double sampleRate = 48000.0;
    float  driveNorm = 0.0f, asymmetry = 0.35f, weightNorm = 0.0f;
    float  asymEff_ = 0.35f;
    bool   wild_ = false;
    float  driveGain = 1.0f;
    float  shelfFreq = 110.0f, weightScale_ = 1.0f;

    Biquad    shelf[kMaxChannels];
    DCBlocker dc[kMaxChannels];
};

//==============================================================================
// 第二级：Tape —— 磁带机
// 高频柔化（磁头损耗）+ 低频 head bump + 磁带自身饱和 + wow/flutter
class TapeStage
{
public:
    void prepare (double sr) noexcept
    {
        sampleRate = sr;
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            hf[ch].reset();
            bump[ch].reset();
            dc[ch].prepare (sr);
            wow[ch].prepare (sr);
        }
        update();
    }

    void reset() noexcept
    {
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            hf[ch].reset();
            bump[ch].reset();
            dc[ch].reset();
            wow[ch].reset();
        }
    }

    // amount: 0..1 磁带参与度   drive: 0..1   toneHz: 高频滚降点
    // wowMs:  走带抖动幅度（毫秒）。0.15 ms 左右是"有人味但不跑调"的量。
    void setParams (float amount, float drive, float toneHz, float wowMs) noexcept
    {
        amountNorm = juce::jlimit (0.0f, 1.0f, amount);
        driveNorm  = juce::jlimit (0.0f, 1.0f, drive);
        toneFreq   = toneHz;
        wowMs_     = juce::jmax (0.0f, wowMs);
        update();
    }

    inline float processSample (int ch, float x) noexcept
    {
        if (amountNorm <= 0.0001f)
            return x;

        // 1. 低频 head bump（磁头在低频的隆起），约 +2.5 dB @ 60 Hz
        float s = bump[ch].process (x);

        // 2. 走带抖动
        s = wow[ch].process (s, wowAmount * amountNorm);

        // 3. 高频柔化
        s = hf[ch].process (s);

        // 4. 磁带饱和：比话放更对称一些（磁带本身是较对称的压缩介质）
        // 磁带级：常规档 1..10x → 1..15x。
        // 两级饱和叠加才是"猛"的关键——单级再怎么推只在波形上捏一下，
        // 第二级会在已经被压平的峰值上再压实一次，谐波密度才上得去。
        const float g = 1.0f + driveNorm * driveNorm * (wild_ ? 26.0f : 12.0f);
        float y = asymTanh (s * g, asymmetryForTape) / g;

        return dc[ch].process (y);
    }

    void setAsymmetry (float a) noexcept { asymmetryForTape = juce::jlimit (0.0f, 1.0f, a) * 0.6f; }

    // 狂野档：磁带级饱和 1..10x → 1..27x。跟 Preamp 叠起来是第二道压实。
    void setWild (bool w) noexcept { wild_ = w; }

private:
    void update() noexcept
    {
        // 毫秒 → 采样数。注意这里用的是过采样后的采样率，
        // 抖动深度必须跟着采样率走，否则开关 HQ 时抖动量会变。
        wowAmount = wowMs_ * 0.001f * (float) sampleRate;

        // toneHz 越低越"暗"。用一阶低通，滚降比 biquad 更接近磁头的自然衰减。
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            hf[ch].setOnePoleLP (sampleRate, toneFreq);
            bump[ch].setLowShelf (sampleRate, 60.0f, 0.9f, 2.5f * amountNorm);
        }
    }

    double sampleRate = 48000.0;
    float  amountNorm = 0.0f, driveNorm = 0.0f;
    float  toneFreq = 12000.0f, wowAmount = 0.0f, wowMs_ = 0.0f;
    float  asymmetryForTape = 0.2f;
    bool   wild_ = false;

    Biquad     hf[kMaxChannels];
    Biquad     bump[kMaxChannels];
    DCBlocker  dc[kMaxChannels];
    WowFlutter wow[kMaxChannels];
};

//==============================================================================
// Air 谐波激励器 —— 用"原有的原料"（非对称软饱和 asymTanh + 高架 biquad）
// 在 air 频段（6k 以上）合成整数谐波，得到真正的"空气感"，而非 EQ 峰。
//
// 原理（经典 exciter / aural exciter）：
//   1. 预提亮：用高架把 air 频段抬进 asymTanh 的非线性区（只用于激励，不直接出声）
//   2. 非线性：复用 asymTanh —— 输入越大谐波越丰富，Air 旋钮即"谐波量"
//   3. 取出纯谐波 = 非线性后 − 预提亮的线性部分，再用高通只留 air 频段，
//      掐掉非对称引入的 DC，避免掉回 presence / 污染中频
//   4. 把谐波混合回原信号，深度由 Air 旋钮控制
class AirHarmonicStage
{
public:
    void prepare (double sr) noexcept
    {
        sampleRate = sr;
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            pre[ch].reset();
            hp[ch].reset();
            dc[ch].reset();
        }
        update();
    }

    void reset() noexcept
    {
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            pre[ch].reset();
            hp[ch].reset();
            dc[ch].reset();
        }
    }

    void setParams (float air, float airHz, float airScale) noexcept
    {
        airNorm    = juce::jlimit (0.0f, 1.0f, air);
        airShelfHz = airHz;
        airScale_  = airScale;
        update();
    }

    inline float processSample (int ch, float x) noexcept
    {
        const float boosted = pre[ch].process (x);                  // 1. 预提亮

        // 2. 非线性 → 整数谐波。
        //    必须 / drive_ 归一化。不除回去的话小信号下 shaped ≈ drive_ × boosted，
        //    (shaped − boosted) 里绝大部分是"被放大的基频"而不是谐波 ——
        //    drive_ = 3.5 时基频泄漏比三次谐波高约 25 dB（真谐波只占 5%）。
        //    混回干信号就变成目标频段整体变响 = presence 味，不是空气感。
        //    归一化后主项变成 −(drive_²/3)·b³：基频分量是"轻微压缩"(负)，
        //    不再是 +13.6 dB 的放大。实测基频泄漏 +13.57 dB → +0.14 dB。
        const float shaped  = asymTanh (boosted * drive_, 0.3f) / drive_;

        float       harm    = (shaped - boosted) * kHarmNorm;       // 3a. 纯谐波
        harm = hp[ch].process (harm);                               // 3b. 只留 air 频段
        harm = dc[ch].process (harm);                               // 3c. 掐掉非对称 DC

        // 4. 混合 + 软限幅。
        //    pre boost 最高 +16 dB，大信号冲出 tanh 线性区后 shaped − boosted
        //    可到 −2.9 量级（boosted=3.15、shaped 饱和在 1/3.5=0.286），
        //    直接乘 depth 会反相冲出去。tanh 小信号严格线性、大信号软限到 ±kHarmCeil。
        return x + kHarmCeil * std::tanh (harm * depth_ / kHarmCeil);
    }

private:
    void update() noexcept
    {
        // 预提亮：让本就稀薄的 air 频段内容进入 tanh 非线性区
        const float preDb  = airNorm * 16.0f * airScale_;
        // 高通截止：air 频段中线，滚掉中频，避免掉回 presence
        const float hpHz   = juce::jmax (4200.0f, airShelfHz * 0.62f);
        drive_ = 1.0f + airNorm * 2.5f * airScale_;                // Air 0→1 : drive 1.0→3.5
        depth_ = airNorm * 1.8f * airScale_;                       // 混合深度随 Air 线性

        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            pre[ch].setHighShelf (sampleRate, airShelfHz, 0.707f, preDb);
            hp[ch].setOnePoleHP  (sampleRate, hpHz);
        }
    }

    // 归一化后谐波量掉到原来的 1/3.5，kHarmNorm 必须同步放大才能保持同等谐波输出：
    // 原 0.12 × 14.29b³ = 1.71b³  ⇒  1.71 / 4.083 = 0.42
    static constexpr float kHarmNorm = 0.42f;
    // 软限幅天花板：谐波最多往干信号上叠加 ±0.7，不会反相也不会冲爆
    static constexpr float kHarmCeil = 0.70f;

    double   sampleRate = 48000.0;
    float    airNorm = 0.0f, airShelfHz = 8000.0f, airScale_ = 1.0f;
    float    drive_ = 1.0f, depth_ = 0.0f;
    Biquad    pre[kMaxChannels];
    Biquad    hp[kMaxChannels];
    DCBlocker dc[kMaxChannels];
};

//==============================================================================
// Weight 谐波激励器 —— 在低频段合成整数谐波，得到真正的"厚度/重量感"，
// 而非单纯低架 EQ。与 Air 对称的低频版：
//   1. 预提低：用低架把低频（贝斯/吉他低把位/箱体共振）抬进 asymTanh 非线性区。
//      吉他的低频本来就弱，所以预增益比 Air 更狠才驱动得动非线性。
//   2. 非线性：asymTanh 产生 2f/3f… 谐波，落进 100-600 Hz 低中频 → 身体感
//   3. 取出纯谐波，低通只留低中频（掐掉 DC、掐掉过高的部分免得变 presence），混合回
class WeightHarmonicStage
{
public:
    void prepare (double sr) noexcept
    {
        sampleRate = sr;
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            pre[ch].reset();
            lp[ch].reset();
            hp[ch].reset();
            hp2[ch].reset();
            dc[ch].reset();
        }
        update();
    }

    void reset() noexcept
    {
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            pre[ch].reset();
            lp[ch].reset();
            hp[ch].reset();
            hp2[ch].reset();
            dc[ch].reset();
        }
    }

    void setParams (float weight, float lowHz, float weightScale) noexcept
    {
        weightNorm   = juce::jlimit (0.0f, 1.0f, weight);
        lowShelfHz_  = lowHz;
        weightScale_ = weightScale;
        update();
    }

    inline float processSample (int ch, float x) noexcept
    {
        const float boosted = pre[ch].process (x);                  // 1. 预提低

        // 2. 非线性 → 整数谐波。同样必须 / drive_ 归一化，理由同 Air：
        //    不除回去时 (shaped − boosted) 里 95% 是被放大的基频，
        //    混回去是"低频变响"而不是"长出身��"。归一化后才是纯三次谐波。
        const float shaped  = asymTanh (boosted * drive_, 0.3f) / drive_;

        float       harm    = (shaped - boosted) * kHarmNorm;       // 3a. 纯谐波
        harm = lp[ch].process (harm);                               // 3b. 只留低中频（身体）
        harm = hp[ch].process (hp2[ch].process (harm));              // 3c. 滤掉基频以下（两级）
        harm = dc[ch].process (harm);                               // 3d. 掐 DC

        // 4. 混合 + 软限幅（同 Air）。Weight 的 pre boost 最高 +22 dB，
        //    比 Air 更容易冲出线性区，这一层限幅更不能省。
        return x + kHarmCeil * std::tanh (harm * depth_ / kHarmCeil);
    }

private:
    void update() noexcept
    {
        // 预提低：吉他低频弱，需要比 Air 更狠的预增益才能驱动非线性
        const float preDb  = weightNorm * 22.0f * weightScale_;
        // 低通截止：保留 2f~6f 谐波（80Hz 基频 → 160~480Hz 身体区），滚掉更高的 presence
        const float lpHz   = juce::jlimit (450.0f, 950.0f, lowShelfHz_ * 6.0f);
        // 高通放在基频以下。不滤的话，前面折叠级造的次八度会混进谐波增量，
        // 再被 depth 放大：贝斯推 weight 时次八度涨 19 dB，盖过基频，就是爆音。
        // 切点 120 Hz：50 Hz 次八度两级约 -15 dB。代价是 100 Hz 基频被削 1.3 dB，方向是变小不是变大。
        const float hpHz   = 120.0f;
        drive_ = 1.0f + weightNorm * 2.5f * weightScale_;          // Weight 0→1 : drive 1.0→3.5
        depth_ = weightNorm * 2.2f * weightScale_;                 // 混合深度随 Weight 线性

        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            pre[ch].setLowShelf (sampleRate, lowShelfHz_, 0.707f, preDb);
            lp[ch].setOnePoleLP  (sampleRate, lpHz);
            hp[ch].setOnePoleHP  (sampleRate, hpHz);
            hp2[ch].setOnePoleHP (sampleRate, hpHz);
        }
    }

    // 同 Air：归一化后谐波量掉到 1/3.5，重新标定
    // 原 0.10 × 14.29b³ = 1.43b³  ⇒  1.43 / 4.083 = 0.35
    static constexpr float kHarmNorm = 0.35f;
    static constexpr float kHarmCeil = 0.70f;

    double   sampleRate = 48000.0;
    float    weightNorm = 0.0f, lowShelfHz_ = 110.0f, weightScale_ = 1.0f;
    float    drive_ = 1.0f, depth_ = 0.0f;
    Biquad    pre[kMaxChannels];
    Biquad    lp[kMaxChannels];
    Biquad    hp[kMaxChannels];
    Biquad    hp2[kMaxChannels];
    DCBlocker dc[kMaxChannels];
};

//==============================================================================
// 狂野模式专属：波形折叠 + 次八度
//
// 常规模式的上限就是 asymTanh —— 推到头也只是把波形压成方波，谐波量到顶了。
// 波形折叠不一样：超过阈值就把波形折回来，折一次多一批谐波，推得越狠折得越多，
// 谐波密度没有上限。这是 Buchla / Dreadbox 那类"撕裂感"的物理来源。
//
// 次八度是另一回事：谐波只能往上长（2f / 3f…），常规染色造不出 f/2。
// 这里用零交叉二分频合成真正的 f/2，低音能往下掉一个八度 —— 这是常规模式
// 结构上就做不到的东西，所以放在狂野模式里。
// 代价是复音素材上会锁到最强的那个周期（LoAir 的同款特性），量给得保守。
class FoldStage
{
public:
    void prepare (double sr) noexcept
    {
        sampleRate = sr;
        reset();
        update();
    }

    void reset() noexcept
    {
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            subLp[ch].reset();
            prev[ch]  = 0.0f;
            flip[ch]  = false;
            env_[ch]  = 0.0f;
        }
    }

    // drive: 0..1 折叠量   weight: 0..1 次八度量
    void setParams (float drive, float weight) noexcept
    {
        foldNorm = juce::jlimit (0.0f, 1.0f, drive);
        subNorm  = juce::jlimit (0.0f, 1.0f, weight);
        update();
    }

    inline float processSample (int ch, float x) noexcept
    {
        if (foldNorm <= 0.001f && subNorm <= 0.001f)
            return x;

        float y = x;

        // 1. 波形折叠
        if (foldNorm > 0.001f)
        {
            const float folded = triFold (x * preGain_, thresh_);
            // 折叠产物过一道软限再混，避免直接冲顶
            y = x + (std::tanh (folded * 0.8f) * 1.25f - x) * foldMix_;
        }

        // 2. 次八度：正向上穿零 → 二分频 → f/2 方波 → 低通取基频
        // 零交叉和包络都看输入 x，不看已经叠了次八度的 y。看 y 的话，
        // Weight 抬起来的 2 次谐波会让检测器锁到错误的周期上，贝斯一推
        // weight 次八度就盖过基频（实测 weight 0 → 满档：−11.7 dB → +0.7 dB）。
        if (subNorm > 0.001f)
        {
            if (x > 0.0f && prev[ch] <= 0.0f)
                flip[ch] = ! flip[ch];
            prev[ch] = x;

            // 方波幅度必须跟着输入走。写死 ±1 的话，输入 0.3 时次八度会比基频
            // 还大 26 dB —— 那不是加厚度，是把整段信号换成低八度的嗡嗡声。
            // 实测过：固定 ±1 时 50 Hz 分量相对基频 +26.6 dB，完全盖掉原声。
            env_[ch] = envCoef_ * env_[ch] + (1.0f - envCoef_) * std::abs (x);

            const float sq = flip[ch] ? env_[ch] : -env_[ch];
            y += subLp[ch].process (sq) * subAmt_;
        }

        return y;
    }

private:
    // 三角折叠：把 x 折进 [-T, T]。小信号原样返回，大信号来回折返。
    // 校验：x=0,T=1 → 0；x=1.5 → 0.5；x=4 → 0（完全折回）。
    static float triFold (float x, float T) noexcept
    {
        const float p = 4.0f * T;
        float m = x + T;
        m -= p * std::floor (m / p);        // 取模到 [0, 4T)
        if (m > 2.0f * T) m = p - m;        // 折返
        return m - T;
    }

    void update() noexcept
    {
        // 话放会把信号压到 tanh 饱和区（实测 0.3 → 0.05），折叠器拿到的电平很小，
        // 所以预增益要够大才折得动。7x 时只折 1 次（谐波 +3 dB），18x 能折 3 次以上。
        preGain_ = 1.0f + foldNorm * 18.0f;
        thresh_  = 0.45f / (1.0f + foldNorm * 2.5f);
        foldMix_ = foldNorm * 0.65f;        // 留 35% 原信号，折叠过头就只剩噪声了
        subAmt_  = subNorm  * 0.30f;

        // 次八度方波要滤成近似正弦。200 Hz 一阶：留住 40~200 Hz 的 f/2，
        // 同时压掉方波自身的 3f/2、5f/2，否则听感是嗡嗡的方波而不是低音。
        for (int ch = 0; ch < kMaxChannels; ++ch)
            subLp[ch].setOnePoleLP (sampleRate, 200.0f);

        // 包络跟随 20 ms：够快到跟着音符走，够慢到不会逐周期抖
        envCoef_ = std::exp (-1.0f / juce::jmax (1.0f, 0.020f * (float) sampleRate));
    }

    double sampleRate = 48000.0;
    float  foldNorm = 0.0f, subNorm = 0.0f;
    float  preGain_ = 1.0f, thresh_ = 0.45f, foldMix_ = 0.0f, subAmt_ = 0.0f;

    Biquad subLp[kMaxChannels];
    float  prev[kMaxChannels] { 0.0f };
    float  env_[kMaxChannels] { 0.0f };
    float  envCoef_ = 0.999f;
    bool   flip[kMaxChannels] { false };
};

//==============================================================================
// 狂野模式专属：GritStage —— 毛刺 / 不规则噪声
//
// 起因：狂野模式一推 drive，中频（吉他）和低频（贝斯）会迅速"变扁"。
// 那是饱和 + 压缩把动态拍平了。**再加大饱和只会更扁**，所以这里走另一条路：
// 不加重压，而是往信号里掺"不稳定"的东西——
//
//   1. 随机游走 LFO（速率自己也在漂）→ 整体呼吸不稳定，永远不会形成固定颤音
//   2. S&H 振幅抖动（sputter）→ 噼啪/毛刺的主力，偶尔来一发掉电级深抖 → 结巴
//   3. 泊松随机尘噪颗粒 → 不规则的噼啪点，带通塑形（不是隆隆声）
//   4. 随机量化砂砾 → 跟着信号走的沙沙底噪
//
// 三条原则：
//  a. 全部由输入包络门控 —— 没信号就不出声，绝不留恒定嘶声（实测必须验证）
//  b. 随机数用固定种子 PRNG，reset() 后行为可复现（便于测试与离线渲染一致）
//  c. amount = 0 时第一行就返回，常规模式零开销
//==============================================================================
class GritStage
{
public:
    void prepare (double sr) noexcept
    {
        sampleRate = sr;
        reset();
        update();
    }

    void reset() noexcept
    {
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            dustHp[ch].reset();
            dustLp[ch].reset();
            env_[ch]      = 0.0f;
            dust_[ch]     = 0.0f;
            shVal_[ch]    = 1.0f;
            shLeft_[ch]   = 1;
            // 每通道不同种子：立体声的毛刺互不相关 → 声场更宽
            rng[ch]       = juce::Random (0x5EED + ch * 7919);
            lfoPhase_[ch] = rng[ch].nextFloat() * juce::MathConstants<float>::twoPi;
            lfoHz_[ch]    = 1.5f + rng[ch].nextFloat() * 3.0f;
        }
        update();
    }

    // amount : 0..1 毛刺总量（狂野模式下跟 drive 走）
    // density: 0..1 颗粒密度倾向（跟 weight 走 → 低音素材也给足颗粒）
    void setParams (float amount, float density) noexcept
    {
        amount_ = juce::jlimit (0.0f, 1.0f, amount);
        dens_   = juce::jlimit (0.0f, 1.0f, density);
        update();
    }

    inline float processSample (int ch, float x) noexcept
    {
        if (amount_ <= 0.001f)
            return x;

        auto& r = rng[ch];

        // 包络跟随 15 ms：所有毛刺都乘它，安静段落自动闭嘴
        env_[ch] = envCoef_ * env_[ch] + (1.0f - envCoef_) * std::abs (x);
        const float gate = env_[ch];

        // --- 1. 随机游走 LFO ---
        // 频率本身每步都在随机漂移，所以不会形成一个能被人耳捕捉的周期。
        lfoHz_[ch] += (r.nextFloat() - 0.5f) * lfoDrift_;
        lfoHz_[ch]  = juce::jlimit (0.25f, 9.0f, lfoHz_[ch]);
        lfoPhase_[ch] += lfoHz_[ch] * twoPiOverSr_;
        if (lfoPhase_[ch] > juce::MathConstants<float>::twoPi)
            lfoPhase_[ch] -= juce::MathConstants<float>::twoPi;

        const float wobble = 1.0f + wobbleAmt_ * std::sin (lfoPhase_[ch]);

        // --- 2. S&H 振幅抖动 ---
        // 换值的时刻随机会数个采样点，所以是噼啪而不是规则的 AM。
        if (--shLeft_[ch] <= 0)
        {
            // 小概率来一发接近掉电的深度抖动 → 结巴/爆音感
            if (r.nextFloat() < dropProb_)
                shVal_[ch] = 1.0f - 0.85f * r.nextFloat();
            else
                shVal_[ch] = 1.0f + sputterAmt_ * (r.nextFloat() * 2.0f - 1.0f);

            shLeft_[ch] = 1 + (int) (r.nextFloat() * shSpan_);
        }

        float y = x * wobble * shVal_[ch];

        // --- 3. 尘噪颗粒：泊松随机触发的短脉冲 ---
        if (r.nextFloat() < grainProb_)
            dust_[ch] += (r.nextFloat() * 2.0f - 1.0f) * grainAmt_ * gate;
        dust_[ch] *= grainDecay_;
        // 带通塑形：掐掉隆隆声（<220 Hz）和过刺的高频（>6.5 kHz）
        y += dustHp[ch].process (dustLp[ch].process (dust_[ch]));

        // --- 4. 砂砾：跟信号包络走的随机噪声 ---
        y += (r.nextFloat() - 0.5f) * sandAmt_ * gate;

        return y;
    }

private:
    void update() noexcept
    {
        const float a = amount_;

        // 低频素材（贝斯）要的毛刺跟吉他不一样：高频嘶声在小素材上不明显，
        // 听得出来的是"结巴/掉电"那种大颗粒的动态跳动。所以 weight（dens_）越高：
        //   · S&H 越慢 → 不是沙沙而是噼啪，边带更靠近基频，低音上也听得见
        //   · 抖得更深、掉电更频繁
        //   · 尘噪通带往下延到 120 Hz
        // 实测：不这么做的话，100 Hz 输入的毛刺只比常规多 10 dB，220 Hz 有 22 dB。
        sputterAmt_ = 0.10f + a * 0.42f + dens_ * 0.20f;
        dropProb_   = 0.002f + a * 0.008f + dens_ * 0.012f;
        wobbleAmt_  = 0.03f + a * 0.14f;
        lfoDrift_   = 0.0004f + a * 0.0018f;  // 每采样点的频率漂移量
        sandAmt_    = (a * a * 0.10f) + (a * dens_ * 0.06f);

        // 颗粒：每秒 12 → ~100 发，密度随 weight 再加
        grainAmt_   = 0.06f + a * 0.30f + dens_ * 0.14f;
        grainProb_  = (12.0f + a * 55.0f + dens_ * 60.0f) / (float) sampleRate;

        // S&H 频率：快 = 沙沙，慢 = 噼啪。低音素材调到慢档。
        const float shHz = (1500.0f + a * 4500.0f) * (1.0f - 0.7f * dens_);
        shSpan_     = juce::jmax (2.0f, (float) sampleRate / juce::jmax (200.0f, shHz));

        // 颗粒衰减 ~0.6 ms
        grainDecay_ = std::exp (-1.0f / juce::jmax (1.0f, 0.0006f * (float) sampleRate));
        envCoef_    = std::exp (-1.0f / juce::jmax (1.0f, 0.015f  * (float) sampleRate));
        twoPiOverSr_ = juce::MathConstants<float>::twoPi / (float) juce::jmax (1.0, sampleRate);

        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            // 220 Hz → 120 Hz：weight 拉高时尘噪往下延，低音素材也有颗粒感
            dustHp[ch].setOnePoleHP (sampleRate, 220.0f - 100.0f * dens_);
            dustLp[ch].setOnePoleLP (sampleRate, 6500.0f);
        }
    }

    double sampleRate = 48000.0;
    float  amount_ = 0.0f, dens_ = 0.0f;
    float  sputterAmt_ = 0.0f, dropProb_ = 0.0f, wobbleAmt_ = 0.0f;
    float  lfoDrift_ = 0.0f, sandAmt_ = 0.0f, grainAmt_ = 0.0f, grainProb_ = 0.0f;
    float  shSpan_ = 24.0f, grainDecay_ = 0.99f, envCoef_ = 0.999f, twoPiOverSr_ = 0.0f;

    juce::Random rng[kMaxChannels];
    Biquad dustHp[kMaxChannels], dustLp[kMaxChannels];
    float  env_[kMaxChannels]   { 0.0f };
    float  dust_[kMaxChannels]  { 0.0f };
    float  shVal_[kMaxChannels] { 1.0f };
    int    shLeft_[kMaxChannels]{ 1 };
    float  lfoPhase_[kMaxChannels] { 0.0f };
    float  lfoHz_[kMaxChannels]    { 1.5f };
};

//==============================================================================
// 第三级：Tone —— Pultec 风格的染色均衡
//
// 这里是"串联"均衡，不是干湿混合。上一版把 EQ 做成并联（in*(1-w)+filt*w），
// 结果滤波曲线变成并联叠加，形状完全不对，还会和干信号打架。
// 均衡只能串在链路上。
class ToneStage
{
public:
    void prepare (double sr) noexcept
    {
        sampleRate = sr;
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            low[ch].reset();
            tight[ch].reset();
            presence[ch].reset();
            air[ch].reset();
            airExciter.prepare (sampleRate);
            weightExciter.prepare (sampleRate);
        }
        update();
    }

    void reset() noexcept
    {
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            low[ch].reset();
            tight[ch].reset();
            presence[ch].reset();
            air[ch].reset();
            airExciter.reset();
            weightExciter.reset();
        }
    }

    // weight: 0..1（低频厚度）  air: 0..1（高频空气量）
    void setParams (float weight, float air, float shelfHz, float airHz,
                    float airScale, float weightScale) noexcept
    {
        weightNorm = juce::jlimit (0.0f, 1.0f, weight);
        airNorm    = juce::jlimit (0.0f, 1.0f, air);
        lowShelfHz = shelfHz;
        airShelfHz = airHz;
        airScale_  = airScale;
        weightScale_ = weightScale;
        update();
    }

    // 狂野档：低架 7.5 → 13 dB、高架 13 → 20 dB、两个激励器深度 ×1.7。
    // 常规模式里 13 dB 的空气已经偏 presence 了，狂野模式就是要这个冲劲。
    void setWild (bool w) noexcept { wild_ = w; update(); }

    inline float processSample (int ch, float x) noexcept
    {
        float s = low[ch].process (x);             // 低架：厚度
        s = tight[ch].process (s);                 // 低频收紧：把 40 Hz 以下收掉，低频不糊
        s = weightExciter.processSample (ch, s);   // Weight 谐波激励：低频泛音填出身体感
        s = presence[ch].process (s);              // 中频：让人声/乐器顶出来
        s = airExciter.processSample (ch, s);      // Air 谐波激励：在 air 频段合成整数谐波
        return air[ch].process (s);                 // 高架：最顶上那层空气
    }

private:
    void update() noexcept
    {
        // 狂野低架 13 → 9 dB。13 dB 叠在次八度上会把贝斯峰值从 0.10 推到 0.91，
        // 后面的毛刺再放大到 1.44，输出级只能把已经削过的波形压回去 —— 爆音。
        // 厚度交给 Weight 激励器（×1.7 不变），低架只负责托底。
        const float lowGainDb  = weightNorm * (wild_ ? 9.0f : 7.5f);
        const float tightDb    = -weightNorm * 3.5f;   // 提升的同时收紧，Pultec 的精髓
        const float presenceDb = 2.0f;                 // 固定 +2dB：不再被 Weight 偷偷抬高中频
        const float airGainDb  = airNorm * (wild_ ? 20.0f : 13.0f) * airScale_;

        const float exciteScale = wild_ ? 1.7f : 1.0f;
        airExciter.setParams    (airNorm,    airShelfHz, airScale_    * exciteScale);
        weightExciter.setParams (weightNorm, lowShelfHz, weightScale_ * exciteScale);

        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            low[ch].setLowShelf      (sampleRate, lowShelfHz,   0.707f, lowGainDb);
            tight[ch].setLowShelf    (sampleRate, 42.0f,        0.707f, tightDb);
            presence[ch].setPeak     (sampleRate, 2600.0f,      0.9f,   presenceDb);
            air[ch].setHighShelf     (sampleRate, airShelfHz,   0.707f, airGainDb);
        }
    }

    double sampleRate = 48000.0;
    float  weightNorm = 0.0f, airNorm = 0.0f;
    float  lowShelfHz = 110.0f, airShelfHz = 8000.0f, airScale_ = 1.0f;
    float  weightScale_ = 1.0f;
    bool   wild_ = false;

    Biquad low[kMaxChannels];
    Biquad tight[kMaxChannels];
    Biquad presence[kMaxChannels];
    Biquad air[kMaxChannels];
    AirHarmonicStage airExciter;
    WeightHarmonicStage weightExciter;
};

//==============================================================================
// 第四级：Comp —— 总线压缩
//
// 关键修正：包络检测必须在**线性域**做。
// 上一版在 dB 域做一阶平滑——信号每次过零点 log10(|x|) 会砸到 -100 dB 底，
// 把包络反复拽回谷底，导致包络永远爬不到阈值以上，压缩器实测增益衰减恒为 0。
// 正确顺序：线性域取峰值 → 一阶平滑 → 再转 dB → 与阈值比较。
class CompStage
{
public:
    void prepare (double sr) noexcept
    {
        sampleRate = sr;
        updateCoefs();
        reset();
    }

    void reset() noexcept { env = 0.0f; gainReductionDb = 0.0f; }

    void setParams (float threshDb, float ratio, float kneeDb,
                    float attackMs, float releaseMs, float amount) noexcept
    {
        thresholdDb = threshDb;
        ratio_      = juce::jmax (1.0f, ratio);
        kneeDb_     = juce::jmax (0.0f, kneeDb);
        attackMs_   = juce::jmax (0.05f, attackMs);
        releaseMs_  = juce::jmax (5.0f, releaseMs);
        amountNorm  = juce::jlimit (0.0f, 1.0f, amount);
        updateCoefs();
    }

    // 输入：所有通道在该采样点的最大绝对值（线性）。返回：该点应施加的线性增益。
    inline float computeGain (float peak) noexcept
    {
        // 1. 线性域峰值包络 + 一阶平滑（attack 快、release 慢）
        const float c = (peak > env) ? attackCoef : releaseCoef;
        env = c * env + (1.0f - c) * peak;

        // 2. 转 dB 后与阈值比较（此时才是"电平"，不是"瞬时采样值"）
        const float envDb = 20.0f * std::log10 (std::max (env, 1.0e-6f));

        // 3. 软拐点静态曲线（Giannoulis 等，Digital Dynamic Range Compressor）
        const float x = envDb - thresholdDb;
        const float W = kneeDb_;
        const float R = ratio_;

        float grDb = 0.0f;
        if (2.0f * x > W)
        {
            grDb = x * (1.0f / R - 1.0f);
        }
        else if (2.0f * x > -W && W > 0.0f)
        {
            const float t = x + W * 0.5f;
            grDb = (1.0f / R - 1.0f) * t * t / (2.0f * W);
        }
        else if (2.0f * x > -W)
        {
            grDb = 0.0f;
        }

        grDb *= amountNorm;
        gainReductionDb = juce::jlimit (-60.0f, 0.0f, grDb);
        return std::pow (10.0f, gainReductionDb * 0.05f);
    }

    float getGainReductionDb() const noexcept { return gainReductionDb; }

private:
    void updateCoefs() noexcept
    {
        // 时间常数 → 一阶系数。注意这里是"达到 63% 所需时间"的常规定义。
        attackCoef  = std::exp (-1.0f / std::max (1.0f, 0.001f * attackMs_  * (float) sampleRate));
        releaseCoef = std::exp (-1.0f / std::max (1.0f, 0.001f * releaseMs_ * (float) sampleRate));
    }

    double sampleRate = 48000.0;
    float  thresholdDb = -18.0f, ratio_ = 3.0f, kneeDb_ = 8.0f;
    float  attackMs_ = 12.0f, releaseMs_ = 140.0f;
    float  amountNorm = 1.0f;
    float  attackCoef = 0.0f, releaseCoef = 0.0f;
    float  env = 0.0f;
    float  gainReductionDb = 0.0f;
};

//==============================================================================
// 第五级：Output —— 输出变压器 + 总线胶水
// 轻饱和 + 极轻的高频抛光 + 输出增益
class OutputStage
{
public:
    void prepare (double sr) noexcept
    {
        sampleRate = sr;
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            sheen[ch].reset();
            dc[ch].prepare (sr);
        }
        update();
    }

    void reset() noexcept
    {
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            sheen[ch].reset();
            dc[ch].reset();
        }
    }

    void setParams (float glue, float asym) noexcept
    {
        glueNorm   = juce::jlimit (0.0f, 1.0f, glue);
        asymmetry  = juce::jlimit (0.0f, 1.0f, asym);
        update();
    }

    // 狂野档：输出变压器 1..7x → 1..17x。这是链条上最后一道压实。
    void setWild (bool w) noexcept { wild_ = w; }

    inline float processSample (int ch, float x) noexcept
    {
        float s = sheen[ch].process (x);

        // 输出变压器：不只是"抛光"，满 glue 时这一级本身就是第二道饱和。
        // 两级饱和叠加是"猛"的关键——单级再怎么推也只在波形上捏一下，
        // 两级会在已经被压平的峰值上再压实一次，谐波密度才上得去。
        const float g = 1.0f + glueNorm * glueNorm * (wild_ ? 16.0f : 6.0f);
        float y = asymTanh (s * g, asymmetry) / g;
        return dc[ch].process (y);
    }

private:
    void update() noexcept
    {
        // 输出变压器：极轻的高频抛光，不是真的"削"高频，只是把最尖的那一点点磨圆
        for (int ch = 0; ch < kMaxChannels; ++ch)
            sheen[ch].setOnePoleLP (sampleRate, 17000.0f);
    }

    double sampleRate = 48000.0;
    float  glueNorm = 0.0f, asymmetry = 0.3f;
    bool   wild_ = false;

    Biquad    sheen[kMaxChannels];
    DCBlocker dc[kMaxChannels];
};

//==============================================================================
// 慢速响度追踪器：用于 Auto Match（bypass 电平匹配）
// 时间常数约 1.5 秒 —— 慢到不会引起任何抽吸，快到切换 bypass 时已经对齐。
class LoudnessTracker
{
public:
    void prepare (double sr) noexcept
    {
        sampleRate = sr;
        reset();
    }

    void reset() noexcept { msIn = 0.0f; msOut = 0.0f; valid = false; }

    void push (float inRms, float outRms, int numSamples) noexcept
    {
        const float c = std::exp (-(double) numSamples / (sampleRate * 1.5));
        const float inSq  = inRms  * inRms;
        const float outSq = outRms * outRms;

        if (! valid)
        {
            msIn = inSq; msOut = outSq; valid = true;
        }
        else
        {
            msIn  = c * msIn  + (1.0f - (float) c) * inSq;
            msOut = c * msOut + (1.0f - (float) c) * outSq;
        }
    }

    // 返回为了让输出追平输入所需的 dB 补偿
    float getCompensationDb() const noexcept
    {
        if (! valid || msIn < 1.0e-10f || msOut < 1.0e-10f) return 0.0f;
        const float inDb  = 10.0f * std::log10 (msIn);
        const float outDb = 10.0f * std::log10 (msOut);
        // 满档染色会把电平压掉 20 dB 以上，±18 dB 的补偿上限不够，
        // 会导致 Auto Match 追不平、bypass 切换时音量跳。放宽到 ±30。
        return juce::jlimit (-30.0f, 30.0f, inDb - outDb);
    }

private:
    double sampleRate = 48000.0;
    float  msIn = 0.0f, msOut = 0.0f;
    bool   valid = false;
};

} // namespace ozo
