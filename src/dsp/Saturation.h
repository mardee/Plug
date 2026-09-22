#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <iterator>
#include <cmath>

namespace ozo
{

//==============================================================================
// 归一化非对称软饱和 —— 本插件"温暖感"的物理来源
//
// 为什么必须非对称：std::tanh 是奇函数，奇函数只能产生奇次谐波（3、5 次…），
// 听感偏"硬、刺、fizz"。而模拟设备（变压器 / 电子管 / 磁带）那种"厚、暖"
// 的质感主要来自偶次谐波（2、4 次…），偶次谐波只能由非对称传递函数产生。
//
// 做法：正负半周使用不同的驱动增益 g 再过 tanh，最后除以 g 归一化。
//   - 小信号：tanh(x·g)/g ≈ x，斜率保持 1（unity gain，音量不漂移）
//   - 大信号：两个半周被压缩的程度不同 → 波形不对称 → 偶次谐波
// 物理上对应 Class A / 单端变压器那种"一个方向先软"的特性。
//
// asym: 0 = 对称（纯奇次，干净但硬）  1 = 强非对称（偶次丰富，厚而暖）
inline float asymTanh (float x, float asym) noexcept
{
    const float d = juce::jlimit (0.0f, 1.0f, asym) * 0.6f;
    const float g = (x >= 0.0f) ? (1.0f + d) : (1.0f - d);
    return std::tanh (x * g) / g;
}

// 对称软饱和，用于不希望改变谐波奇偶性的场合（例如磁带的对称压缩段）
inline float symTanh (float x) noexcept
{
    return std::tanh (x);
}

//==============================================================================
// 一阶 DC 阻断器
// 非对称饱和必然引入直流偏移，必须在每一级饱和之后立刻掐掉，
// 否则 DC 会累积并吃掉后级的动态余量。
class DCBlocker
{
public:
    void prepare (double sampleRate) noexcept
    {
        // 截止约 15 Hz：足够低到不影响任何可听低频，又足以泄放 DC
        R = 1.0f - (2.0f * juce::MathConstants<float>::pi * 15.0f / static_cast<float> (sampleRate));
        if (R > 0.9999f) R = 0.9999f;
    }

    void reset() noexcept { x1 = 0.0f; y1 = 0.0f; }

    inline float process (float x) noexcept
    {
        const float y = x - x1 + R * y1;
        x1 = x;
        y1 = y;
        return y;
    }

private:
    float R = 0.999f;
    float x1 = 0.0f, y1 = 0.0f;
};

//==============================================================================
// 手写双二阶（biquad），直接形式 I。
// 不用 juce::dsp::IIR 是因为这里需要逐采样、可内联、零分配，
// 并且要在过采样后的高采样率下跑，状态量必须自己管。
class Biquad
{
public:
    void reset() noexcept { x1 = x2 = y1 = y2 = 0.0f; }

    inline float process (float x) noexcept
    {
        const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;  x1 = x;
        y2 = y1;  y1 = y;
        return y;
    }

    // RBJ cookbook 低架
    void setLowShelf (double sampleRate, float f0, float Q, float gainDb) noexcept
    {
        const double A  = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * juce::MathConstants<double>::pi * (double) f0 / sampleRate;
        const double cw = std::cos (w0), sw = std::sin (w0);
        const double al = sw / (2.0 * (double) Q);
        const double sq = 2.0 * std::sqrt (A) * al;

        const double a0 = (A + 1.0) + (A - 1.0) * cw + sq;
        b0 = (float) ( A * ((A + 1.0) - (A - 1.0) * cw + sq) / a0);
        b1 = (float) (2.0 * A * ((A - 1.0) - (A + 1.0) * cw) / a0);
        b2 = (float) ( A * ((A + 1.0) - (A - 1.0) * cw - sq) / a0);
        a1 = (float) (-2.0 * ((A - 1.0) + (A + 1.0) * cw) / a0);
        a2 = (float) (      ((A + 1.0) + (A - 1.0) * cw - sq) / a0);
    }

    // RBJ cookbook 高架
    void setHighShelf (double sampleRate, float f0, float Q, float gainDb) noexcept
    {
        const double A  = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * juce::MathConstants<double>::pi * (double) f0 / sampleRate;
        const double cw = std::cos (w0), sw = std::sin (w0);
        const double al = sw / (2.0 * (double) Q);
        const double sq = 2.0 * std::sqrt (A) * al;

        const double a0 = (A + 1.0) - (A - 1.0) * cw + sq;
        b0 = (float) ( A * ((A + 1.0) + (A - 1.0) * cw + sq) / a0);
        b1 = (float) (-2.0 * A * ((A - 1.0) + (A + 1.0) * cw) / a0);
        b2 = (float) ( A * ((A + 1.0) + (A - 1.0) * cw - sq) / a0);
        a1 = (float) ( 2.0 * ((A - 1.0) - (A + 1.0) * cw) / a0);
        a2 = (float) (      ((A + 1.0) - (A - 1.0) * cw - sq) / a0);
    }

    // RBJ cookbook 峰值（钟形）滤波，用于中频塑形
    void setPeak (double sampleRate, float f0, float Q, float gainDb) noexcept
    {
        const double A  = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * juce::MathConstants<double>::pi * (double) f0 / sampleRate;
        const double cw = std::cos (w0), sw = std::sin (w0);
        const double al = sw / (2.0 * (double) Q);

        const double a0 = 1.0 + al / A;
        b0 = (float) ((1.0 + al * A) / a0);
        b1 = (float) (-2.0 * cw / a0);
        b2 = (float) ((1.0 - al * A) / a0);
        a1 = (float) (-2.0 * cw / a0);
        a2 = (float) ((1.0 - al / A) / a0);
    }

    // 一阶低通（令 a 系数为零，退化为单极点）
    void setOnePoleLP (double sampleRate, float fc) noexcept
    {
        const double a = std::exp (-2.0 * juce::MathConstants<double>::pi
                                   * (double) fc / sampleRate);
        b0 = (float) (1.0 - a);
        b1 = 0.0f;  b2 = 0.0f;
        a1 = (float) (-a);
        a2 = 0.0f;
    }

    // 一阶高通（磁带/变压器的低频 head bump 之后用来收住超低频）
    void setOnePoleHP (double sampleRate, float fc) noexcept
    {
        const double a = std::exp (-2.0 * juce::MathConstants<double>::pi
                                   * (double) fc / sampleRate);
        b0 = (float) ((1.0 + a) * 0.5);
        b1 = (float) (-(1.0 + a) * 0.5);
        b2 = 0.0f;
        a1 = (float) (-a);
        a2 = 0.0f;
    }

    void makeIdentity() noexcept
    {
        b0 = 1.0f; b1 = 0.0f; b2 = 0.0f; a1 = 0.0f; a2 = 0.0f;
    }

private:
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
};

//==============================================================================
// 分贝 / 线性互转的小工具
inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) noexcept  { return 20.0f * std::log10 (std::max (g, 1.0e-7f)); }

// 把 [0,1] 的旋钮位置映射到以 dB 为单位的量，带一点曲线让手感更自然
inline float mapToDb (float norm, float minDb, float maxDb) noexcept
{
    return minDb + (maxDb - minDb) * juce::jlimit (0.0f, 1.0f, norm);
}

} // namespace ozo
