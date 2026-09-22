#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <cstdint>
#include <cmath>

#include "TintPalette.h"

namespace ozo
{

//==============================================================================
// 粒子层 —— 飘在面板上面的最后一层装饰
//
// 四类粒子，能量（声音）越大越热闹：
//   · 拖尾星尘：每颗身后拖 5 个残影，位置是过去几帧的历史，颜色沿拖尾渐隐
//   · 十字闪：两根交叉细线，亮度按自己的相位脉冲，亮的时候带一圈光晕
//   · 彗星：一颗头 + 一条沿速度方向的渐隐尾巴
//   · 轨道碎屑：绕着一个缓慢移动的中心转小圈，成团而不是散点
//
// 颜色仍然按每颗粒子自己的 u 从调色板取，拖旋钮时整片颜色立刻跟着变。
//==============================================================================
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

        const float bright = 0.55f + 1.05f * energy;
        const float grow   = 0.90f + 0.70f * energy;

        for (const auto& d : dots)
        {
            const float px = d.x * w;
            const float py = d.y * h;
            const float pulse = 0.5f + 0.5f * std::sin (t * d.rate + d.phase);
            const float a  = (0.22f + 0.62f * pulse) * bright;
            const float s  = d.size * grow;
            const auto  c  = palette->at (d.u);

            switch (d.kind)
            {
                case Kind::dust:   drawDust   (g, d, px, py, s, a, c); break;
                case Kind::spark:  drawSpark  (g, px, py, s, a, pulse, c); break;
                case Kind::comet:  drawComet  (g, d, px, py, s, a, w, h, c); break;
                case Kind::orbit:  drawOrbit  (g, d, px, py, s, a, w, h, c); break;
            }
        }
    }

private:
    enum class Kind { dust, spark, comet, orbit };

    //--------------------------------------------------------------------------
    struct Pt { float x = 0.0f, y = 0.0f; };

    static constexpr int kTrail = 6;

    struct Dot
    {
        float x = 0.0f, y = 0.0f, vx = 0.0f, vy = 0.0f;
        float u = 0.0f;
        float size = 1.0f, phase = 0.0f, rate = 1.0f;
        Kind  kind = Kind::dust;
        std::array<Pt, kTrail> hist {};
        int   head = 0;
    };


    // 软光点：一圈淡晕 + 一颗实心核。比单画一个 fillEllipse 有体积。
    static void softDot (juce::Graphics& g, float x, float y, float r,
                         juce::Colour c, float a)
    {
        g.setColour (c.withAlpha (juce::jlimit (0.0f, 0.35f, a * 0.30f)));
        g.fillEllipse (x - r, y - r, r * 2.0f, r * 2.0f);
        const float core = r * 0.42f;
        g.setColour (c.withAlpha (juce::jlimit (0.0f, 0.95f, a)));
        g.fillEllipse (x - core, y - core, core * 2.0f, core * 2.0f);
    }

    //--------------------------------------------------------------------------
    // 拖尾星尘：残影从历史位置里取，越老越淡越小
    void drawDust (juce::Graphics& g, const Dot& d, float px, float py,
                   float s, float a, juce::Colour c)
    {
        // 残影：历史位置是像素，间距只有 1~2 px，所以隔帧取、半径给足，才看得见拖尾
        for (int k = kTrail - 1; k >= 2; k -= 2)
        {
            const float f = 1.0f - (float) k / (float) kTrail;
            const auto& hp = d.hist[(d.head + kTrail - k) % kTrail];
            const float r = s * (1.2f + 1.4f * f);
            g.setColour (c.withAlpha (juce::jlimit (0.0f, 0.8f, a * 0.30f * f)));
            g.fillEllipse (hp.x - r, hp.y - r, r * 2.0f, r * 2.0f);
        }

        softDot (g, px, py, s * 3.2f, c, a);
    }

    // 十字闪 + 峰值时光晕
    void drawSpark (juce::Graphics& g, float px, float py, float s, float a,
                    float pulse, juce::Colour c)
    {
        if (pulse > 0.82f)
        {
            const float rad = s * 9.0f;
            juce::ColourGradient halo (c.withAlpha (a * 0.45f), px, py,
                                       c.withAlpha (0.0f),      px + rad, py, true);
            g.setGradientFill (halo);
            g.fillEllipse (px - rad, py - rad, rad * 2.0f, rad * 2.0f);
        }

        // 闪光是一颗更亮的点，外加四道很短的芒。芒长按像素封顶，绝不拉成线。
        softDot (g, px, py, s * 4.0f, c, a);
        g.setColour (c.withAlpha (juce::jlimit (0.0f, 0.9f, a * pulse)));
        const float L = juce::jmin (14.0f, s * 3.0f);
        g.drawLine (px - L, py, px + L, py, 1.2f);
        g.drawLine (px, py - L, px, py + L, 1.2f);
    }

    // 彗星：头是实心点，尾巴沿速度反方向拉一条渐隐线
    void drawComet (juce::Graphics& g, const Dot& d, float px, float py,
                    float s, float a, float w, float h, juce::Colour c)
    {
        // 尾巴长度按像素封顶。上一版乘了窗口宽度，1564px 的窗口上每条尾巴两万像素，
        // 横贯整个面板 —— 截图里那些细线就是这个。
        const float len = juce::jmin (26.0f, s * 8.0f);
        // vx/vy 是归一化坐标（每帧约 0.003），先还原成像素再取方向，尾巴才是像素级的
        const float dx  = d.vx * w, dy = d.vy * h;
        const float n   = std::sqrt (dx * dx + dy * dy) + 1.0e-3f;
        const float tx  = px - dx / n * len;
        const float ty  = py - dy / n * len;

        g.setColour (c.withAlpha (juce::jlimit (0.0f, 0.55f, a * 0.45f)));
        g.drawLine (px, py, tx, ty, juce::jmax (1.4f, s * 0.9f));

        softDot (g, px, py, s * 3.6f, c, a);
        juce::ignoreUnused (w, h);
    }

    // 轨道碎屑：绕中心转的三颗小点，中心自己也在飘
    void drawOrbit (juce::Graphics& g, const Dot& d, float px, float py,
                    float s, float a, float w, float h, juce::Colour c)
    {
        const float R = s * 5.5f;
        for (int k = 0; k < 3; ++k)
        {
            const float ang = t * d.rate * 1.6f + d.phase + (float) k * 2.0944f;
            const float ox  = px + std::cos (ang) * R;
            const float oy  = py + std::sin (ang) * R * 0.62f;   // 压扁，看起来有透视
            const float r   = s * 0.55f;
            g.setColour (c.withAlpha (juce::jlimit (0.0f, 0.9f, a * (0.55f + 0.45f * (float) k / 2.0f))));
            g.fillEllipse (ox - r, oy - r, r * 2.0f, r * 2.0f);
        }

        juce::ignoreUnused (w, h);
    }

    //--------------------------------------------------------------------------
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

        const float w = (float) getWidth();
        const float h = (float) getHeight();
        const float lift = 0.65f + 1.9f * energy;

        for (auto& d : dots)
        {
            // 速度跟这颗粒子所属的那个旋钮走：拧得越大，同色粒子飘得越快。
            // 保底 0.45，旋钮归零时还在慢慢动，不会僵死。
            const float drive = palette != nullptr
                              ? 0.45f + 1.7f * palette->weightAt (d.u)
                              : 1.0f;
            d.y += d.vy * lift * drive;
            d.x += d.vx * (0.7f + 0.9f * energy) * drive;

            // 彗星偶尔变向，轨迹不是直线
            if (d.kind == Kind::comet)
            {
                d.vx += (rand01() - 0.5f) * 0.00012f;
                d.vx = juce::jlimit (-0.0040f, 0.0040f, d.vx);
            }

            if (d.y < -0.05f) { respawn (d, true); }

            // 每帧有约 2% 的粒子按当前权重重新抽色。只靠飘出屏幕再重生的话，
            // 拧旋钮后要等十几秒颜色分布才变过来。
            else if (palette != nullptr && rand01() < 0.02f)
                d.u = palette->samplePos (rand01());
            if (d.x < -0.05f)       d.x = 1.05f;
            else if (d.x > 1.05f)   d.x = -0.05f;

            // 记录历史位置，拖尾用。坐标换成像素，绘制时不用再乘宽高
            d.head = (d.head + 1) % kTrail;
            d.hist[d.head] = { d.x * w, d.y * h };
        }
    }

    void respawn (Dot& d, bool fromBottom)
    {
        d.x = rand01();
        d.y = fromBottom ? 1.04f : rand01();
        // 按四个旋钮的权重抽颜色：开得大的那一色，重生出来的粒子就多
        d.u = palette != nullptr ? palette->samplePos (rand01()) : rand01();
    }

    void reset()
    {
        rngState = 0x2545F491u;

        for (int i = 0; i < kNumDots; ++i)
        {
            auto& d = dots[(size_t) i];
            d.x     = rand01();
            d.y     = rand01();
            d.u     = rand01();   // 调色板此时还没接上，第一帧 step() 会按权重重抽
            d.phase = rand01() * 6.28318f;
            d.rate  = 0.6f + rand01() * 2.2f;
            d.head  = 0;

            // 比例：星尘最多，闪和彗星点缀，轨道碎屑最少（它占面积大）
            const float r = rand01();
            if      (r < 0.46f) { d.kind = Kind::dust;   d.size = 1.1f + rand01() * 1.8f; }
            else if (r < 0.70f) { d.kind = Kind::spark;  d.size = 0.9f + rand01() * 1.2f; }
            else if (r < 0.88f) { d.kind = Kind::comet;  d.size = 1.3f + rand01() * 1.4f; }
            else                { d.kind = Kind::orbit;  d.size = 1.0f + rand01() * 1.1f; }

            // 彗星走得快、方向斜；其余慢慢往上飘
            if (d.kind == Kind::comet)
            {
                d.vx = (rand01() - 0.5f) * 0.0060f;
                d.vy = -0.0025f - rand01() * 0.0040f;
            }
            else
            {
                d.vx = (rand01() - 0.5f) * 0.0012f;
                d.vy = -0.0006f - rand01() * 0.0015f;
            }

            for (auto& p : d.hist) p = { d.x, d.y };
        }

        t = 0.0f;
    }

    float rand01() noexcept
    {
        rngState = rngState * 1664525u + 1013904223u;
        return (float) ((rngState >> 8) & 0xFFFFFFu) / (float) 0x1000000u;
    }

    static constexpr int kNumDots = 84;

    const TintPalette* palette = nullptr;
    std::array<Dot, kNumDots> dots {};

    float energy = 0.0f;
    float t = 0.0f;
    uint32_t rngState = 0x2545F491u;
};

} // namespace ozo
