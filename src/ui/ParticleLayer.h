#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <cstdint>
#include <cmath>

#include "TintPalette.h"

namespace ozo
{

//==============================================================================
// 粒子层 —— 飘在面板**上面**的最后一层装饰
//
// 每颗粒子带一个固定的 u（在颜色轴上的位置 0..1），颜色 = palette->at(u)。
// u 是均匀分布的，所以配色权重一变，粒子的颜色分布立刻跟着变 ——
// 不需要等老粒子飘走、新粒子生成，拖旋钮的当下就能看到颜色在流动。
//
// 频谱能量会驱动它们：声音一起来，粒子就浮得更快、更亮、更大。
class ParticleLayer : public juce::Component,
                      private juce::Timer
{
public:
    ParticleLayer()
    {
        setInterceptsMouseClicks (false, false);
        reset();
        startTimerHz (30);
    }

    void setPalette (const TintPalette* p) noexcept { palette = p; }
    void setEnergy (float e) { energy += (juce::jlimit (0.0f, 1.0f, e) - energy) * 0.25f; }

    void paint (juce::Graphics& g) override
    {
        if (palette == nullptr)
            return;

        const float w = (float) getWidth();
        const float h = (float) getHeight();

        if (w <= 0.0f || h <= 0.0f)
            return;

        const float bright = 0.45f + 0.85f * energy;
        const float grow   = 0.85f + 0.55f * energy;

        for (const auto& d : dots)
        {
            const float px = d.x * w;
            const float py = d.y * h;
            const float a  = (0.16f + 0.50f * (0.5f + 0.5f * std::sin (t * d.rate + d.phase)))
                           * bright;
            const float s  = d.size * grow;

            g.setColour (palette->at (d.u).withAlpha (juce::jlimit (0.0f, 0.95f, a)));

            if (d.sparkle)
            {
                // 四角星：两根交叉的细线，比圆点更像"闪"
                const float L = s * 3.2f;
                g.drawLine (px - L, py, px + L, py, s * 0.55f);
                g.drawLine (px, py - L, px, py + L, s * 0.55f);
            }
            else
            {
                g.fillEllipse (px - s, py - s, s * 2.0f, s * 2.0f);
            }
        }
    }

private:
    void timerCallback() override
    {
        if (! isShowing())
            return;

        step();
        repaint();
    }

    void step()
    {
        t += 1.0f / 30.0f;

        // 能量越高，浮得越快 —— 声音一起来，整个背景就跟着动
        const float lift = 0.55f + 1.55f * energy;

        for (auto& d : dots)
        {
            d.y += d.vy * lift;
            d.x += d.vx;

            if (d.y < -0.03f) { d.y = 1.03f; d.x = rand01(); d.u = rand01(); }
            if (d.x < -0.03f)       d.x = 1.03f;
            else if (d.x > 1.03f)   d.x = -0.03f;
        }
    }

    void reset()
    {
        rngState = 0x2545F491u;

        for (auto& d : dots)
        {
            d.x       = rand01();
            d.y       = rand01();
            d.u       = rand01();
            d.vx      = (rand01() - 0.5f) * 0.0014f;
            d.vy      = -0.0007f - rand01() * 0.0016f;
            d.size    = 0.8f + rand01() * 1.7f;
            d.phase   = rand01() * 6.28318f;
            d.rate    = 0.5f + rand01() * 1.7f;
            d.sparkle = rand01() < 0.24f;
        }

        t = 0.0f;
    }

    float rand01() noexcept
    {
        rngState = rngState * 1664525u + 1013904223u;
        return (float) ((rngState >> 8) & 0xFFFFFFu) / (float) 0x1000000u;
    }

    //--------------------------------------------------------------------------
    struct Dot
    {
        float x = 0.0f, y = 0.0f, vx = 0.0f, vy = 0.0f;
        float u = 0.0f;               // 颜色轴上的位置，权重变了颜色就跟着变
        float size = 1.0f, phase = 0.0f, rate = 1.0f;
        bool  sparkle = false;
    };

    static constexpr int kNumDots = 52;

    const TintPalette* palette = nullptr;
    std::array<Dot, kNumDots> dots {};

    float energy = 0.0f;
    float t = 0.0f;
    uint32_t rngState = 0x2545F491u;
};

} // namespace ozo
