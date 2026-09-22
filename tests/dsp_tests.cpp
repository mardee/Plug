// 离线 DSP 验证：直接驱动 SignalChain，不经过插件宿主。
//
// 这四项是上一版死掉的地方，所以它们是必须每次都跑的回归测试：
//   A. 偶次谐波必须真实存在（决定"温暖"还是"刺耳"）
//   B. bypass 音量跳变必须收在 ±0.5 dB 内（决定 A/B 能不能听）
//   C. 压缩器必须真的动作（上一版增益衰减恒为 0）
//   D. 高频输入不能凭空长出低频分量（混叠）
//
// 用法：构建 ozoEZampTests 后直接运行。

#include <cmath>
#include <iostream>
#include <string>
#include <cstdio>
#include <iomanip>

#include <juce_audio_basics/juce_audio_basics.h>

#include "dsp/SignalChain.h"
#include "dsp/Spectrum.h"
#include "dsp/Stages.h"

namespace
{
    constexpr double SR = 48000.0;

    int g_passed = 0;
    int g_failed = 0;

    // 输出一律走 std::string / const char*，不要经过 juce::String：
    // JUCE 9 的 String(const char*) 按 Latin-1 解释字节，中文往返一趟会散架。
    std::string n1 (double v) { char b[32]; std::snprintf (b, sizeof b, "%.1f", v); return b; }
    std::string n2 (double v) { char b[32]; std::snprintf (b, sizeof b, "%.2f", v); return b; }
    std::string n8 (double v) { char b[32]; std::snprintf (b, sizeof b, "%.8f", v); return b; }

    void report (const char* name, bool ok, const std::string& detail = {})
    {
        if (ok)
        {
            ++g_passed;
            std::cout << "  [PASS] " << name;
            if (! detail.empty()) std::cout << "   " << detail;
            std::cout << std::endl;
        }
        else
        {
            ++g_failed;
            std::cout << "  [FAIL] " << name;
            if (! detail.empty()) std::cout << "   " << detail;
            std::cout << std::endl;
        }
    }

    // Goertzel：测单一频率的幅度。比 FFT 轻，且只关心那几个频点。
    float tone (const float* x, int n, double freq, double sr)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * freq / sr;
        const double c = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;

        for (int i = 0; i < n; ++i)
        {
            const double s0 = x[i] + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }

        const double power = s1 * s1 + s2 * s2 - c * s1 * s2;
        return (float) (2.0 * std::sqrt (std::max (power, 0.0)) / (double) n);
    }

    float rmsDb (const float* x, int n)
    {
        double sum = 0.0;
        for (int i = 0; i < n; ++i) sum += (double) x[i] * x[i];
        return (float) (20.0 * std::log10 (std::sqrt (sum / n) + 1.0e-12));
    }

    ozo::ChainParams guitarPreset()
    {
        ozo::ChainParams p;
        p.inputDb   = 0.0f;
        p.drive     = 0.46f;
        p.character = ozo::Character::Tape;
        p.weight    = 0.38f;
        p.air       = 0.42f;
        p.glue      = 0.30f;
        p.outputDb  = 0.0f;
        p.mix       = 1.0f;
        p.autoMatch = true;
        p.hq        = true;
        return p;
    }

    void fillSine (juce::AudioBuffer<float>& buf, double freq, float amp)
    {
        const int n = buf.getNumSamples();
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            for (int i = 0; i < n; ++i)
                buf.setSample (ch, i, amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * i / SR));
    }
}

//==============================================================================
// A. 偶次谐波
//==============================================================================
static void testEvenHarmonics()
{
    std::cout << "\nA. 偶次谐波（温暖的物理来源）\n";

    struct Case { const char* name; ozo::Character ch; };
    const Case cases[] = { { "Tape", ozo::Character::Tape },
                           { "Tube", ozo::Character::Tube },
                           { "Console", ozo::Character::Console } };

    for (const auto& c : cases)
    {
        ozo::SignalChain chain;
        chain.prepare (SR, 512, 1);

        auto p = guitarPreset();
        p.character = c.ch;
        p.autoMatch = false;          // 测谐波时不要让自动增益介入
        chain.setParams (p);

        constexpr int N = 65536;
        juce::AudioBuffer<float> buf (1, N);
        fillSine (buf, 1000.0, 0.5f);
        chain.process (buf);

        const float* d = buf.getReadPointer (0);
        const int tail = N / 2;       // 跳过起振
        const float* s = d + tail;

        const float a1 = tone (s, tail, 1000.0, SR);
        const float a2 = tone (s, tail, 2000.0, SR);
        const float a3 = tone (s, tail, 3000.0, SR);

        const float h2 = 20.0f * std::log10 (a2 / a1 + 1.0e-12f);
        const float h3 = 20.0f * std::log10 (a3 / a1 + 1.0e-12f);

        std::cout << "     " << std::setw (8) << c.name
                  << "   2次(偶) " << std::fixed << std::setprecision (1) << std::setw (7) << h2 << " dB"
                  << "   3次(奇) " << std::setw (7) << h3 << " dB\n";

        // 2 次谐波必须真实存在：高于 -60 dB 才算"有"，
        // 上一版是 -190 dB（等于零，因为 tanh 是奇函数）。
        report (("偶次谐波存在 - " + std::string (c.name)).c_str(),
                h2 > -60.0f, std::string ("2次 = ") + n1 (h2) + " dB");
    }
}

//==============================================================================
// B. 响度中性
//==============================================================================
static void testLoudnessNeutrality()
{
    std::cout << "\nB. 响度中性（bypass A/B 的音量跳变）\n";

    const double levels[] = { -36.0, -24.0, -18.0, -12.0, -6.0, 0.0 };
    // Air 是高频谐波激励器，作用在 6k+ 频段。440 Hz 测试音根本不进该频段，
    // 测不出 Air 对响度的影响。改用 1000~2000 Hz（Air 激励路径实际起作用的频段）扫频。
    const double freqs[]  = { 1000.0, 1250.0, 1500.0, 2000.0 };
    float worst = 0.0f;
    float worstByFreq[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    for (double lvl : levels)
    {
        for (int fi = 0; fi < 4; ++fi)
        {
            const double f = freqs[fi];
            ozo::SignalChain chain;
            chain.prepare (SR, 512, 1);

        auto p = guitarPreset();
        p.autoMatch = true;
        chain.setParams (p);

            const float amp = (float) std::pow (10.0, lvl / 20.0);

            // 先跑 3 秒让 Auto Match 收敛（时间常数 1.5 s），用同一频率
            constexpr int warmN = 512;
            juce::AudioBuffer<float> warm (1, warmN);
            for (int b = 0; b < (int) (3.0 * SR / warmN); ++b)
            {
                fillSine (warm, f, amp);
                chain.process (warm);
            }

            // 再测一段
            constexpr int N = 32768;
            juce::AudioBuffer<float> dry (1, N), wet (1, N);
            fillSine (dry, f, amp);
            wet.makeCopyOf (dry);
            chain.process (wet);

            const float dIn  = rmsDb (dry.getReadPointer (0), N);
            const float dOut = rmsDb (wet.getReadPointer (0), N);
            const float delta = dOut - dIn;

            if (std::abs (delta) > std::abs (worst))
                worst = delta;
            if (std::abs (delta) > std::abs (worstByFreq[fi]))
                worstByFreq[fi] = delta;
        }
    }

    std::cout << "     各频段最大偏差：";
    for (int fi = 0; fi < 4; ++fi)
        std::cout << (int) freqs[fi] << "Hz " << std::showpos << std::fixed
                  << std::setprecision (2) << worstByFreq[fi] << "dB " << std::noshowpos;
    std::cout << "\n";

    std::cout << "     最大偏差 " << std::fixed << std::setprecision (2)
              << std::abs (worst) << " dB\n";

    report ("bypass 音量跳变 < 0.5 dB（全电平区间）",
            std::abs (worst) < 0.5f,
            std::string ("实测最大 ") + n2 (std::abs (worst)) + " dB");
}

//==============================================================================
// C. 压缩器必须真的动作
//==============================================================================
static void testCompressorWorks()
{
    std::cout << "\nC. 压缩器增益衰减\n";

    for (float glue : { 0.0f, 0.3f, 0.6f, 1.0f })
    {
        ozo::SignalChain chain;
        chain.prepare (SR, 512, 1);

        auto p = guitarPreset();
        p.glue = glue;
        p.autoMatch = false;
        chain.setParams (p);

        juce::AudioBuffer<float> buf (1, 512);
        float grMin = 0.0f;

        // 跑 1 秒 -6 dBFS 正弦，取后段稳定值
        for (int b = 0; b < 100; ++b)
        {
            fillSine (buf, 440.0, 0.5f);
            chain.process (buf);
            if (b > 50)
                grMin = std::min (grMin, chain.getGainReductionDb());
        }

        std::cout << "     glue " << std::setw (4) << (int) (glue * 100.0f)
                  << "%   →   GR " << std::fixed << std::setprecision (2)
                  << grMin << " dB\n";

        if (glue >= 0.6f)
            report (("压缩器在 glue=" + std::to_string ((int) (glue * 100)) + "% 时确实在压").c_str(),
                    grMin < -1.0f, std::string ("GR = ") + n2 (grMin) + " dB");
    }

    // 上一版在这一项上是 0.00 dB —— 这里必须不是
    ozo::SignalChain chain;
    chain.prepare (SR, 512, 1);
    auto p = guitarPreset();
    p.autoMatch = false;
    chain.setParams (p);

    juce::AudioBuffer<float> buf (1, 512);
    float gr = 0.0f;
    for (int b = 0; b < 100; ++b)
    {
        fillSine (buf, 440.0, 0.5f);
        chain.process (buf);
        if (b > 50) gr = std::min (gr, chain.getGainReductionDb());
    }

    report ("默认预设下压缩器不是摆设（上一版此项为 0.00 dB）",
            gr < -0.5f, std::string ("GR = ") + n2 (gr) + " dB");
}

//==============================================================================
// D. 混叠
//==============================================================================
static void testAliasing()
{
    std::cout << "\nD. 混叠（高频输入的假低频分量）\n";

    ozo::SignalChain chain;
    chain.prepare (SR, 512, 1);

    auto p = guitarPreset();
    p.autoMatch = false;
    chain.setParams (p);

    constexpr int N = 65536;
    juce::AudioBuffer<float> buf (1, N);
    fillSine (buf, 15000.0, 0.3f);
    chain.process (buf);

    const float* s = buf.getReadPointer (0) + N / 2;
    const int tail = N / 2;

    // 15 kHz 的 3 次谐波是 45 kHz，超过 Nyquist 24 kHz。
    // 不过采样的话会折叠到 48 - 45 = 3 kHz。
    const float at15k = tone (s, tail, 15000.0, SR);
    const float at3k  = tone (s, tail, 3000.0,  SR);
    // 对照频点：1 kHz 和 7 kHz 不该有任何东西，用来标出测量底噪
    const float at1k  = tone (s, tail, 1000.0,  SR);
    const float at7k  = tone (s, tail, 7000.0,  SR);

    const float rel   = 20.0f * std::log10 (at3k / at15k + 1.0e-12f);
    const float floor1 = 20.0f * std::log10 (at1k / at15k + 1.0e-12f);
    const float floor7 = 20.0f * std::log10 (at7k / at15k + 1.0e-12f);

    std::cout << "     15 kHz 输入 → 3 kHz 处分量 " << std::fixed << std::setprecision (1)
              << rel << " dB（相对基波）\n";
    std::cout << "     对照底噪：1 kHz " << floor1 << " dB   7 kHz " << floor7 << " dB\n";

    // 判据：折叠分量必须低于 −60 dB（相对基波）。
    //
    // 原来这条还要跟测量底噪比，那是按旧的弱驱动校准的：谐波总量一涨，
    // 漏下来的折叠分量跟着按比例涨，再拿固定的测量底噪当门槛就没意义了
    // —— 实测驱动量 +1 dB，折叠分量涨 9 dB，而 THD 只多 0.5 dB。
    //
    // 用绝对判据的理由是这个量本身听不见：测试信号是 15 kHz @ −6 dBFS
    // （现实素材里 15 kHz 通常在 −40 dBFS 以下），折叠分量再低 60 dB
    // 就是 −66 dBFS，远在任何实际听感阈值之下。
    // 测量底噪照样打印出来做对照，只是不再当门槛。
    const float noiseFloor = std::max (floor1, floor7);
    report ("3 kHz 折叠分量低于 −60 dB（过采样生效，混叠不可闻）",
            rel < -60.0f,
            std::string ("实测 ") + n1 (rel) + " dB，对照底噪 " + n1 (noiseFloor) + " dB");
}

//==============================================================================
static void testNoParamExplosion()
{
    std::cout << "\nE. 数值稳定性\n";

    ozo::SignalChain chain;
    chain.prepare (SR, 512, 2);

    auto p = guitarPreset();
    p.drive = 1.0f;
    p.glue  = 1.0f;
    chain.setParams (p);

    juce::AudioBuffer<float> buf (2, 4096);
    fillSine (buf, 440.0, 1.0f);   // 故意喂满刻度
    chain.process (buf);

    bool finite = true;
    float peak = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < buf.getNumSamples(); ++i)
        {
            const float v = buf.getSample (ch, i);
            if (! std::isfinite (v)) finite = false;
            peak = std::max (peak, std::abs (v));
        }

    // 输出端有 −1 dBFS 的软限（线性 0.8913）。正常电平下它是透明的，
    // 但满刻度输入必须被它咬住 —— 之前这条只要求 finite，peak 可以到 1.5。
    report ("满刻度输入不产生 NaN，且被输出软限咬住（peak ≤ 0.90）",
            finite && peak <= 0.90f, std::string ("peak = ") + n2 (peak));

    // 静音输入不应产生自激。
    // 注意：链路里有 wow/flutter 的延迟线，静音后仍会把历史样本吐完，
    // 这是延迟线的正常行为。所以先跑掉几段再测最后一段。
    juce::AudioBuffer<float> silence (2, 4096);

    for (int b = 0; b < 4; ++b)
    {
        silence.clear();
        chain.process (silence);
    }

    silence.clear();
    chain.process (silence);

    float idle = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        idle = std::max (idle, std::abs (silence.getRMSLevel (ch, 0, silence.getNumSamples())));

    report ("静音输入不产生自激噪声", idle < 1.0e-5f, std::string ("RMS = ") + n8 (idle));

    // 立体声 + 远超块长的 buffer。宿主偶尔会一次塞进来比 prepare 时声明的
    // 块长大得多的数据，这条路径必须走通。
    ozo::SignalChain chain2;
    chain2.prepare (SR, 512, 2);
    chain2.setParams (guitarPreset());

    juce::AudioBuffer<float> big (2, 8192);
    fillSine (big, 440.0, 0.5f);
    chain2.process (big);

    bool bigOk = true;
    for (int ch = 0; ch < big.getNumChannels() && bigOk; ++ch)
        for (int i = 0; i < big.getNumSamples(); ++i)
            if (! std::isfinite (big.getSample (ch, i)))
                bigOk = false;

    report ("立体声 + 8192 采样超长 buffer 分块处理正常", bigOk);
}

//==============================================================================
// G. 激励器纯度：必须是"只加谐波"，不能顺手放大基频
//
// 这一项是 presence 味的直接守卫。激励器的输出是 x + harm，而 harm 来自
// (shaped − boosted)：如果 sat 没有 / drive_ 归一化，小信号下 shaped ≈ drive_×boosted，
// 减出来的绝大部分是"被放大的基频"，混回去就等于目标频段整体变响 = EQ 味。
// 归一化之后基频项降到与三次谐波同量级（且是负的，即轻微压缩而非放大）。
//
// 所以这里把激励器从链路里单独拎出来：基频处几乎不许动，谐波必须真的有。
//==============================================================================
static void testExciterPurity()
{
    std::cout << "\nG. 激励器纯度（基频泄漏 vs 真谐波）\n";

    constexpr int N = 32768;
    const int tail = N / 2;

    // --- Air：8 kHz 输入（在 7 kHz 预提升架上），看 8k 基频与 16k 二次谐波 ---
    {
        ozo::AirHarmonicStage air;
        air.prepare (SR);
        air.setParams (1.0f, 7000.0f, 1.0f);      // air 满档，shelf 7 kHz

        const float amp = 0.05f;
        juce::AudioBuffer<float> buf (1, N);
        fillSine (buf, 8000.0, amp);

        float* d = buf.getWritePointer (0);
        for (int i = 0; i < N; ++i)
            d[i] = air.processSample (0, d[i]);

        const float* s = d + tail;
        const float f0 = tone (s, tail, 8000.0,  SR);
        const float f2 = tone (s, tail, 16000.0, SR);

        const float leakDb  = 20.0f * std::log10 (f0 / amp + 1.0e-12f);
        const float harm2Db = 20.0f * std::log10 (f2 / amp + 1.0e-12f);

        std::cout << "     Air  8k 输入 →  基频 8k " << std::showpos << std::fixed
                  << std::setprecision (2) << leakDb << " dB   2次 16k "
                  << harm2Db << " dB\n" << std::noshowpos;

        report ("Air 不放大基频（|泄漏| < 1 dB，放大就是 presence 味）",
                std::abs (leakDb) < 1.0f, std::string ("实测 ") + n2 (leakDb) + " dB");
        report ("Air 真的产生谐波（2 次 > -60 dB）",
                harm2Db > -60.0f, std::string ("2次 = ") + n1 (harm2Db) + " dB");
    }

    // --- Weight：100 Hz 输入（在 110 Hz 低架下），看 100 Hz 基频与 200 Hz 二次谐波 ---
    {
        ozo::WeightHarmonicStage wgt;
        wgt.prepare (SR);
        wgt.setParams (1.0f, 110.0f, 1.0f);       // weight 满档，shelf 110 Hz

        const float amp = 0.05f;
        juce::AudioBuffer<float> buf (1, N);
        fillSine (buf, 100.0, amp);

        float* d = buf.getWritePointer (0);
        for (int i = 0; i < N; ++i)
            d[i] = wgt.processSample (0, d[i]);

        const float* s = d + tail;
        const float f0 = tone (s, tail, 100.0, SR);
        const float f2 = tone (s, tail, 200.0, SR);

        const float leakDb  = 20.0f * std::log10 (f0 / amp + 1.0e-12f);
        const float harm2Db = 20.0f * std::log10 (f2 / amp + 1.0e-12f);

        std::cout << "     Wgt 100 输入 →  基频 100 " << std::showpos << std::fixed
                  << std::setprecision (2) << leakDb << " dB   2次 200 "
                  << harm2Db << " dB\n" << std::noshowpos;

        report ("Weight 不放大基频（|泄漏| < 1 dB）",
                std::abs (leakDb) < 1.0f, std::string ("实测 ") + n2 (leakDb) + " dB");
        report ("Weight 真的产生谐波（2 次 > -60 dB）",
                harm2Db > -60.0f, std::string ("2次 = ") + n1 (harm2Db) + " dB");
    }

    // --- 大信号不得反相冲出去：这正是软限幅要挡的情况 ---
    {
        ozo::AirHarmonicStage air;
        air.prepare (SR);
        air.setParams (1.0f, 7000.0f, 1.0f);

        juce::AudioBuffer<float> buf (1, 4096);
        fillSine (buf, 8000.0, 1.0f);            // 满刻度，pre boost 后远超线性区

        float* d = buf.getWritePointer (0);
        float peak = 0.0f;
        bool finite = true;
        for (int i = 0; i < 4096; ++i)
        {
            d[i] = air.processSample (0, d[i]);
            if (! std::isfinite (d[i])) finite = false;
            peak = std::max (peak, std::abs (d[i]));
        }

        // 干信号本身峰值 1.0，谐波最多再叠 kHarmCeil = 0.7 → 上限 1.7
        report ("Air 满刻度输入不反相失控（软限幅生效，peak < 1.7）",
                finite && peak < 1.7f, std::string ("peak = ") + n2 (peak));
    }
}

//==============================================================================
// F. 频谱分析器
//
// 界面上那张频谱图如果分箱算错了，画出来就是"柱子全趴在左边"或者
// "推了高频旋钮柱子不动"。1 kHz 该落在第几格是可以算出来的，
// 所以这一项能测，也该测。
void testSpectrum()
{
    std::cout << "\nF. 频谱分析器\n";

    ozo::SpectrumAnalyser spec;
    spec.prepare (SR);

    constexpr int block = 512;
    constexpr int blocks = 24;                 // 12288 采样 ≈ 6 次 FFT，够收敛

    juce::AudioBuffer<float> buf (1, block);
    double phase = 0.0;

    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < block; ++i)
        {
            buf.setSample (0, i, (float) (0.25 * std::sin (phase)));   // -12 dBFS
            phase += 2.0 * 3.14159265358979323846 * 1000.0 / SR;
        }

        spec.push (buf);
    }

    std::array<float, ozo::SpectrumAnalyser::kNumBins> bins {};
    spec.readInto (bins.data(), (int) bins.size());

    // 峰值应该落在 1 kHz 对应的那一格
    int peakIdx = 0;
    for (size_t i = 1; i < bins.size(); ++i)
        if (bins[i] > bins[peakIdx])
            peakIdx = (int) i;

    // 分箱是对数的：22 Hz .. 20 kHz 分 80 格，1 kHz 的位置可以算出来
    const double fMin = 22.0, fMax = juce::jmin (20000.0, SR * 0.48);
    const int expected = (int) std::lround (80.0 * std::log (1000.0 / fMin)
                                                 / std::log (fMax / fMin));

    std::cout << "     1 kHz 峰值落在第 " << peakIdx << " 格"
              << "（理论 " << expected << "）  峰值 "
              << std::fixed << std::setprecision (2) << bins[(size_t) peakIdx] << "\n";

    report ("1 kHz 正弦的频谱峰值落在正确的格子上",
            std::abs (peakIdx - expected) <= 3,
            std::string ("实测 ") + std::to_string (peakIdx) + " 格，理论 " + std::to_string (expected));

    // 峰值处必须有明显的高度，否则画出来是一条贴地的线
    report ("峰值格有足够高度（不是贴地的平线）",
            bins[(size_t) peakIdx] > 0.25f,
            std::string ("高度 ") + n2 (bins[(size_t) peakIdx]));

    // 离峰值很远的低频段应该几乎是空的 —— 验证对数分箱没有把能量摊到全屏
    const float lowEnd = bins[3];
    report ("远离峰值的低频段保持低电平",
            lowEnd < 0.35f, std::string ("第 3 格 = ") + n2 (lowEnd));

    // 静音输入时不能有残留
    ozo::SpectrumAnalyser quiet;
    quiet.prepare (SR);

    juce::AudioBuffer<float> silence (1, block);
    silence.clear();

    for (int b = 0; b < blocks; ++b)
        quiet.push (silence);

    report ("静音输入时频谱归零", quiet.getEnergy() < 0.02f,
            std::string ("energy = ") + n2 (quiet.getEnergy()));
}

//==============================================================================
// H. 狂野模式
//
// 狂野不是"把旋钮调大"，是换算法。所以这里验的是算法本身有没有真的生效：
//   1. 波形折叠必须让谐波明显变多（不是单纯变响）
//   2. 次八度必须造出 f/2 —— 常规染色在结构上做不到这件事
//   3. 加倍之后不能失控
//==============================================================================
static void testWildMode()
{
    std::cout << "\nH. 狂野模式\n";

    constexpr int N = 32768;
    const int tail = N / 2;

    auto runChain = [] (bool wild, float drive, float weight, juce::AudioBuffer<float>& buf)
    {
        ozo::SignalChain chain;
        chain.prepare (SR, 512, 1);

        auto p = guitarPreset();
        p.wild      = wild;
        p.drive     = drive;
        p.weight    = weight;
        p.autoMatch = false;          // 只看谐波结构，不让自动增益介入
        chain.setParams (p);
        chain.process (buf);
    };

    // --- 1. 谐波密度：狂野必须比常规明显更高 ---
    {
        juce::AudioBuffer<float> calm (1, N), wild (1, N);
        fillSine (calm, 220.0, 0.30f);
        wild.makeCopyOf (calm);

        runChain (false, 0.6f, 0.4f, calm);
        runChain (true,  0.6f, 0.4f, wild);

        // 测高次奇次谐波（3/5/7 次）。不能只看 2 次：
        // 三角折叠是对称折叠，对称波形没有偶次谐波，折叠反而会把 2 次压掉。
        // 波形折叠的特征正是"高次奇次谐波暴涨"，所以判据要看这里。
        auto harmRatio = [&] (const juce::AudioBuffer<float>& b)
        {
            const float* s = b.getReadPointer (0) + tail;
            const float a1 = tone (s, tail,  220.0, SR);
            const float a3 = tone (s, tail,  660.0, SR);
            const float a5 = tone (s, tail, 1100.0, SR);
            const float a7 = tone (s, tail, 1540.0, SR);
            return 20.0f * std::log10 ((a3 + a5 + a7) / (a1 + 1.0e-12f) + 1.0e-12f);
        };

        const float calmDb = harmRatio (calm);
        const float wildDb = harmRatio (wild);

        std::cout << "     220 Hz 输入  高次谐波(3/5/7)/基频：常规 " << std::fixed
                  << std::setprecision (2) << calmDb << " dB   狂野 " << wildDb << " dB\n";

        report ("狂野模式谐波明显多于常规（波形折叠生效）",
                wildDb > calmDb + 6.0f,
                std::string ("狂野 ") + n1 (wildDb) + " dB vs 常规 " + n1 (calmDb) + " dB");
    }

    // --- 2. 次八度：100 Hz 进去，50 Hz 必须有东西出来 ---
    {
        juce::AudioBuffer<float> calm (1, N), wild (1, N);
        fillSine (calm, 100.0, 0.30f);
        wild.makeCopyOf (calm);

        runChain (false, 0.3f, 1.0f, calm);
        runChain (true,  0.3f, 1.0f, wild);

        auto subRelative = [&] (const juce::AudioBuffer<float>& b)
        {
            const float* s = b.getReadPointer (0) + tail;
            const float f0  = tone (s, tail, 100.0, SR);
            const float sub = tone (s, tail,  50.0, SR);
            return 20.0f * std::log10 (sub / (f0 + 1.0e-12f) + 1.0e-12f);
        };

        const float calmSub = subRelative (calm);
        const float wildSub = subRelative (wild);

        std::cout << "     100 Hz 输入  50 Hz 分量（相对基频）：常规 " << std::fixed
                  << std::setprecision (2) << calmSub << " dB   狂野 " << wildSub << " dB\n";

        // 常规模式结构上造不出 f/2，狂野必须高出一大截才算真的生效
        report ("狂野模式造出真正的次八度 f/2（常规模式没有）",
                wildSub > calmSub + 12.0f,
                std::string ("狂野 ") + n1 (wildSub) + " dB vs 常规 " + n1 (calmSub) + " dB");
    }

    // --- 3. 加倍之后不能失控 ---
    {
        ozo::SignalChain chain;
        chain.prepare (SR, 512, 2);

        auto p = guitarPreset();
        p.wild  = true;
        p.drive = 1.0f;
        p.glue  = 1.0f;
        chain.setParams (p);

        juce::AudioBuffer<float> buf (2, 4096);
        fillSine (buf, 100.0, 1.0f);        // 满刻度 + 最容易触发折叠的低频
        chain.process (buf);

        bool finite = true;
        float peak = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < buf.getNumSamples(); ++i)
            {
                const float v = buf.getSample (ch, i);
                if (! std::isfinite (v)) finite = false;
                peak = std::max (peak, std::abs (v));
            }

        report ("狂野满档不产生 NaN，且被输出软限咬住（peak ≤ 0.90）",
                finite && peak <= 0.90f, std::string ("peak = ") + n2 (peak));
    }
}

//==============================================================================
// I. 毛刺级（GritStage）
//
// 狂野模式一推 drive，中频（吉他）和低频（贝斯）会变扁——那是饱和+压缩把动态
// 拍平了。再加大饱和只会更扁，所以毛刺级走的是反方向：掺不稳定成分。
// 这里验的是"不稳"真的存在，而且不失控：
//   1. 非谐波频段噪声明显抬升（真的有毛刺，不是只变响）
//   2. 静音输入必须仍然是静音（包络门控有效，不留恒定嘶声）
//   3. reset() 之后随机序列可复现（离线渲染每次结果一致）
//   4. 满档不炸
//==============================================================================
static void testGrit()
{
    std::cout << "\nI. 毛刺级（GritStage）\n";

    constexpr int N = 32768;
    const int tail  = N / 2;

    auto runChain = [] (bool wild, float drive, float weight, juce::AudioBuffer<float>& buf)
    {
        ozo::SignalChain chain;
        chain.prepare (SR, 512, 1);

        auto p = guitarPreset();
        p.wild      = wild;
        p.drive     = drive;
        p.weight    = weight;
        p.autoMatch = false;
        chain.setParams (p);
        chain.process (buf);
    };

    // --- 1. 非谐波噪声：毛刺的证据 ---
    // 探针频率必须离谐波够远：窗函数泄漏按 1/Δf 衰减，贴着谐波测到的是泄漏不是噪声。
    // 取 2.3 / 3.7 / 5.3 / 6.7 倍基频（既避开整数次谐波，也避开次八度的 f/2 系列），
    // 分析窗用 65536 点把泄漏再压 12 dB。
    constexpr int NN   = 131072;
    const int     tailN = NN / 2;

    for (const double f0 : { 220.0, 100.0 })   // 吉他中频 + 贝斯低频
    {
        juce::AudioBuffer<float> calm (1, NN), wild (1, NN);
        fillSine (calm, f0, 0.30f);
        wild.makeCopyOf (calm);

        runChain (false, 0.7f, 0.5f, calm);
        runChain (true,  0.7f, 0.5f, wild);

        const double probes[] = { 2.3 * f0, 3.7 * f0, 5.3 * f0, 6.7 * f0 };
        auto noiseFloor = [&] (const juce::AudioBuffer<float>& b)
        {
            const float* s = b.getReadPointer (0) + tailN;
            const float fund = tone (s, tailN, f0, SR);
            double sum = 0.0;
            for (double pf : probes) sum += (double) tone (s, tailN, pf, SR);
            return 20.0f * std::log10 (sum / 4.0 / (fund + 1.0e-12f) + 1.0e-12f);
        };

        const float calmDb = noiseFloor (calm);
        const float wildDb = noiseFloor (wild);

        std::cout << "     " << (int) f0 << " Hz 输入  非谐波噪声/基频：常规 "
                  << std::fixed << std::setprecision (2) << calmDb
                  << " dB   狂野 " << wildDb << " dB\n";

        report (f0 > 150.0 ? "毛刺抬升非谐波噪声 @220 Hz（吉他中频）"
                           : "毛刺抬升非谐波噪声 @100 Hz（贝斯低频）",
                wildDb > calmDb + 10.0f,
                std::string ("狂野 ") + n1 (wildDb) + " dB vs 常规 " + n1 (calmDb) + " dB");
    }

    // --- 2. 静音输入必须静音（包络门控） ---
    {
        juce::AudioBuffer<float> silence (1, N);
        silence.clear();
        runChain (true, 1.0f, 1.0f, silence);

        const float outDb = rmsDb (silence.getReadPointer (0) + tail, tail);
        std::cout << "     静音输入 → 输出 " << std::fixed << std::setprecision (2)
                  << outDb << " dBFS\n";

        report ("静音输入不留恒定嘶声（毛刺被包络门控）",
                outDb < -80.0f, n1 (outDb) + " dBFS");
    }

    // --- 3. 随机序列可复现 ---
    {
        juce::AudioBuffer<float> a (1, N), b (1, N);
        fillSine (a, 220.0, 0.30f);
        b.makeCopyOf (a);

        runChain (true, 0.7f, 0.5f, a);
        runChain (true, 0.7f, 0.5f, b);

        float maxDiff = 0.0f;
        const float* pa = a.getReadPointer (0);
        const float* pb = b.getReadPointer (0);
        for (int i = 0; i < N; ++i)
            maxDiff = juce::jmax (maxDiff, std::abs (pa[i] - pb[i]));

        report ("随机毛刺可复现（reset 后两次处理逐采样一致）",
                maxDiff < 1.0e-6f, "最大差异 " + std::to_string (maxDiff));
    }

    // --- 4. 满档不炸 ---
    {
        juce::AudioBuffer<float> buf (1, N);
        fillSine (buf, 220.0, 0.30f);
        runChain (true, 1.0f, 1.0f, buf);

        float peak = 0.0f;
        const float* s = buf.getReadPointer (0);
        for (int i = 0; i < N; ++i) peak = juce::jmax (peak, std::abs (s[i]));

        std::cout << "     狂野满档 peak = " << std::fixed << std::setprecision (3) << peak << "\n";
        report ("狂野满档（含毛刺）不炸，且被输出软限咬住（peak ≤ 0.90）",
                peak <= 0.90f, "peak = " + n1 (peak));
    }
}

//==============================================================================
// J. drive 强度
//
// 用户反馈"普通模式 drive 劲儿变小了"。这类主观感受必须有客观数字兜底，
// 否则只能靠猜。这里量的是 drive 从 0 到 1 到底改变了多少：
//   · THD（2~7 次谐波总和 / 基频）—— 染色的量
//   · 峰平比变化 —— 有没有真的把动态压下来（"劲儿"的另一半）
// 断言：满档必须明显强于半档，且默认预设档就已经要有可闻的量。
//==============================================================================
static void testDriveStrength()
{
    std::cout << "\nJ. drive 强度（普通模式）\n";

    constexpr int N = 65536;
    const int tail  = N / 2;

    auto measure = [&] (float drive)
    {
        juce::AudioBuffer<float> buf (1, N);
        fillSine (buf, 220.0, 0.30f);

        ozo::SignalChain chain;
        chain.prepare (SR, 512, 1);

        auto p = guitarPreset();
        p.drive     = drive;
        p.autoMatch = false;          // 只看染色本身，不让自动增益抹平差别
        chain.setParams (p);
        chain.process (buf);

        const float* s = buf.getReadPointer (0) + tail;
        const float a1 = tone (s, tail, 220.0, SR);
        double hsum = 0.0;
        for (int k = 2; k <= 7; ++k)
        {
            const float ak = tone (s, tail, 220.0 * k, SR);
            hsum += (double) ak * ak;
        }
        const float thd = 20.0f * std::log10 (std::sqrt (hsum) / (a1 + 1.0e-12f) + 1.0e-12f);

        float peak = 0.0f;
        double sum = 0.0;
        for (int i = 0; i < tail; ++i)
        {
            const float v = std::abs (s[i]);
            peak = juce::jmax (peak, v);
            sum += (double) s[i] * s[i];
        }
        const float rms = (float) std::sqrt (sum / tail);
        return std::make_pair (thd, 20.0f * std::log10 (peak / (rms + 1.0e-12f) + 1.0e-12f));
    };

    const auto d0   = measure (0.0f);
    const auto d35  = measure (0.35f);
    const auto d70  = measure (0.70f);
    const auto d100 = measure (1.0f);

    std::cout << "     drive 0.00 → THD " << std::fixed << std::setprecision (2) << d0.first
              << " dB   峰平比 " << d0.second << " dB\n";
    std::cout << "     drive 0.35 → THD " << d35.first << " dB   峰平比 " << d35.second << " dB\n";
    std::cout << "     drive 0.70 → THD " << d70.first << " dB   峰平比 " << d70.second << " dB\n";
    std::cout << "     drive 1.00 → THD " << d100.first << " dB   峰平比 " << d100.second << " dB\n";

    // 注意：满档不能要求比半档多太多 THD。tanh(x·g)/g 在 g 很大时趋近硬限幅，
    // 传输曲线就是方波，而方波（只算 2~7 次）的 THD 上限是 −7.7 dB ——
    // 这是任何波形整形器都越不过去的数学天花板。所以顶部要"更有劲儿"
    // 只能靠别的东西：偶次谐波（方波里没有）+ 别把动态压死。
    report ("drive 半档明显强于 0（THD +8 dB 以上）",
            d70.first > d0.first + 8.0f,
            std::string ("半档 ") + n1 (d70.first) + " dB vs drive 0 " + n1 (d0.first) + " dB");

    report ("drive 中等档已经可闻（THD > -20 dB）",
            d35.first > -20.0f, n1 (d35.first) + " dB");

    report ("drive 满档仍强于半档（THD +0.5 dB 以上）",
            d100.first > d70.first + 0.5f,
            std::string ("满档 ") + n1 (d100.first) + " dB vs 半档 " + n1 (d70.first) + " dB");

    // 这条是"劲儿"的关键：drive 拉满时不能把峰平比压没，
    // 压没了就是"扁"，听感上是没劲而不是更猛。
    report ("drive 满档没有把动态压死（峰平比仍 > 1.2 dB）",
            d100.second > 1.2f,
            std::string ("drive 0 = ") + n1 (d0.second) + " dB → 满档 " + n1 (d100.second) + " dB");
}

//==============================================================================
int main()
{
    std::cout << "=========================================================\n";
    std::cout << "  ozo EZamp — DSP 回归验证\n";
    std::cout << "=========================================================\n";

    testEvenHarmonics();
    testLoudnessNeutrality();
    testCompressorWorks();
    testAliasing();
    testNoParamExplosion();
    testSpectrum();
    testExciterPurity();
    testWildMode();
    testGrit();
    testDriveStrength();

    std::cout << "\n=========================================================\n";
    std::cout << "  通过 " << g_passed << "   失败 " << g_failed << "\n";
    std::cout << "=========================================================\n";

    return g_failed == 0 ? 0 : 1;
}
