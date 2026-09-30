#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <cmath>

#include "OzoLookAndFeel.h"

namespace ozo
{

//==============================================================================
// 配色权重表 —— 频谱与粒子共用同一份，保证整屏色彩永远一致
//
// Drive / Weight / Air 各领一色，蜜桃色使用三者合成权重。
// 旋钮开得越大，该色在横轴上的"势力范围"就越宽。
//
// 用的是高斯软分配而不是硬切分：
//   每色在自己区间的中心权重最高，区间宽度由旋钮值决定。
// 硬切分会在边界上留一道生硬的接缝，软分配是渐变过渡。
class TintPalette
{
public:
    static constexpr int kLutSize = 256;
    static constexpr int kNumTints = 4;
    static constexpr float kFloor = 0.20f;   // 每色的保底占比，保证四色永远都在

    TintPalette()
    {
        rebuild();
    }

    // w4: 四色的归一化权重 0..1，顺序 Drive / Weight / Air / 合成染色
    void setWeights (const float* w4)
    {
        bool changed = false;

        for (int i = 0; i < kNumTints; ++i)
        {
            const float v = juce::jlimit (0.0f, 1.0f, w4[i]);

            if (std::abs (v - weights[(size_t) i]) > 0.0015f)
            {
                weights[(size_t) i] = v;
                changed = true;
            }
        }

        if (changed)
            rebuild();
    }

    // 换主题之后强制重算。权重没变、只有 OzoCol::tint 变了的话，
    // setWeights 判定为"没变化"不会 rebuild，LUT 会一直停在旧配色上。
    void refresh() { rebuild(); }

    // 按当前权重抽一个颜色。返回值是颜色轴上的位置 0..1，
    // 旋钮开得越大，抽中它那一色的概率越高。u 是 0..1 的均匀随机数。
    float samplePos (float u) const noexcept
    {
        float w[kNumTints], total = 0.0f;
        for (int i = 0; i < kNumTints; ++i)
        {
            w[i] = kFloor + weights[(size_t) i];
            total += w[i];
        }

        float acc = 0.0f;
        for (int i = 0; i < kNumTints; ++i)
        {
            acc += w[i] / total;
            if (u <= acc)
                return ((float) i + 0.5f) / (float) kNumTints;
        }
        return 1.0f;
    }

    // 某个颜色轴位置对应的旋钮开度 0..1。粒子用它决定自己飘多快。
    float weightAt (float pos) const noexcept
    {
        const int i = juce::jlimit (0, kNumTints - 1, (int) (pos * (float) kNumTints));
        return weights[(size_t) i];
    }

    // pos: 0..1，返回该位置的颜色
    juce::Colour at (float pos) const noexcept
    {
        const int i = juce::jlimit (0, kLutSize - 1,
                                    (int) std::lround (pos * (float) (kLutSize - 1)));
        return lut[(size_t) i];
    }

private:
    void rebuild()
    {
        float w[kNumTints];
        float total = 0.0f;

        for (int i = 0; i < kNumTints; ++i)
        {
            w[i] = kFloor + weights[(size_t) i];
            total += w[i];
        }

        float cum[kNumTints + 1];
        cum[0] = 0.0f;

        for (int i = 0; i < kNumTints; ++i)
            cum[i + 1] = cum[i] + w[i] / total;

        for (int k = 0; k < kLutSize; ++k)
        {
            const float p = (float) k / (float) (kLutSize - 1);

            float acc[kNumTints], sum = 0.0f;

            for (int i = 0; i < kNumTints; ++i)
            {
                const float centre = 0.5f * (cum[i] + cum[i + 1]);
                const float half   = 0.5f * (cum[i + 1] - cum[i]);
                const float sigma  = half * 0.95f + 0.055f;
                const float d      = (p - centre) / sigma;

                acc[i] = std::exp (-0.5f * d * d);
                sum   += acc[i];
            }

            float r = 0.0f, g = 0.0f, b = 0.0f;

            for (int i = 0; i < kNumTints; ++i)
            {
                const float m = acc[i] / (sum + 1.0e-6f);
                const auto& c = OzoCol::tint[(size_t) i];

                r += c.getFloatRed()   * m;
                g += c.getFloatGreen() * m;
                b += c.getFloatBlue()  * m;
            }

            lut[(size_t) k] = juce::Colour::fromFloatRGBA (r, g, b, 1.0f);
        }
    }

    std::array<juce::Colour, kLutSize> lut {};
    std::array<float, kNumTints> weights { 0.25f, 0.25f, 0.25f, 0.25f };
};

} // namespace ozo
