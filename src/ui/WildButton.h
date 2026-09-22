#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Graffiti.h"
#include "OzoLookAndFeel.h"

namespace ozo
{

//==============================================================================
// WILD —— 涂鸦喷漆风格的模式开关
//
// 不只是换个字体：喷漆罐的感觉来自四件事叠在一起，缺一件就像普通按钮贴了张字：
//   1. 手写涂鸦字形 + 描边（描边是 tag 的灵魂，光填色会很平）
//   2. 整体歪一点（-5°），手写的东西不会横平竖直
//   3. 字下面挂"流挂"（drip）—— 喷漆往下淌的那一滴
//   4. 周围一圈喷溅点（spray dots）—— 罐子喷出来必然有飞溅
//
// 颜色走 OzoCol，所以常规/狂野两套主题自动跟着换。
// 打开时外圈有光晕，并由 setFlicker() 驱动轻微闪烁（编辑器 30 Hz 定时器喂值）。
//==============================================================================
class WildButton : public juce::Button
{
public:
    WildButton() : juce::Button ("WILD")
    {
        setClickingTogglesState (true);
        setTooltip ("狂野模式：换算法 + 全部加倍 + 界面转深色");
    }

    // 0..1，由编辑器定时器喂进来。只有打开时才有视觉意义。
    void setFlicker (float f) noexcept
    {
        const float v = juce::jlimit (0.0f, 1.0f, f);
        if (std::abs (v - flicker) < 0.01f)
            return;
        flicker = v;
        if (getToggleState())
            repaint();
    }

protected:
    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const bool on   = getToggleState();
        const auto area = getLocalBounds().toFloat().reduced (1.0f);
        const float r   = 7.0f;

        const float push = down ? 1.0f : 0.0f;

        // --- 外光晕：只在打开时，强度被 flicker 轻微调制 ---
        if (on)
        {
            const float glowA = 0.42f + 0.22f * flicker;
            // 这个 JUCE 版本没有 isRadial / r 成员了 —— 径向只能走构造函数：
            // point1 = 圆心，point2 到圆心的距离就是半径。
            const float rad = area.getWidth() * 0.85f;
            juce::ColourGradient halo (OzoCol::tint[0].withAlpha (glowA),
                                       area.getCentreX(), area.getCentreY(),
                                       OzoCol::tint[0].withAlpha (0.0f),
                                       area.getCentreX() + rad, area.getCentreY(),
                                       true);
            g.setGradientFill (halo);
            g.fillRoundedRectangle (area.expanded (7.0f), r + 7.0f);
        }

        // --- 底：关 = 深紫黑；开 = 炽红→熔橙的喷漆渐变 ---
        juce::ColourGradient base (
            on ? OzoCol::tint[0] : OzoCol::text,
            area.getX(), area.getY(),
            on ? OzoCol::tint[3] : OzoCol::text.darker (0.35f),
            area.getX(), area.getBottom(), false);
        g.setGradientFill (base);
        g.fillRoundedRectangle (area.translated (0.0f, push), r);

        if (highlighted && ! on)
        {
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.fillRoundedRectangle (area.translated (0.0f, push), r);
        }

        // --- 边：开的时候用亮色描边 + 一点点抖 ---
        g.setColour (on ? juce::Colour (0xFFFDF3C8).withAlpha (0.92f)
                        : OzoCol::panelEdge.darker (0.25f));
        g.drawRoundedRectangle (area.translated (0.0f, push), r,
                                on ? 2.0f : 1.2f);

        // ---------------------------------------------------------------------
        // 字：涂鸦字形 + 描边 + 歪 5 度
        // ---------------------------------------------------------------------
        // 字号是按字体 em 算的，手写字形的实际墨迹高度往往远小于 em，
        // 直接按 height 取会有大片空白。所以先按参考字号取出字形路径，
        // 再按实际包围盒缩放去填满按钮 —— 不管换什么字体都不会留空。
        auto f = Graffiti::font (100.0f, 0.03f);

        juce::GlyphArrangement ga;
        ga.addLineOfText (f, "WILD", 0.0f, 0.0f);

        juce::Path textPath;
        ga.createPath (textPath);
        auto tb = textPath.getBounds();

        const float targetW = area.getWidth()  * (on ? 0.90f : 0.86f);
        const float targetH = area.getHeight() * (on ? 0.70f : 0.66f);
        const float fit = juce::jmin (targetW / juce::jmax (1.0e-3f, tb.getWidth()),
                                      targetH / juce::jmax (1.0e-3f, tb.getHeight()));

        const float cx = area.getCentreX();
        const float cy = area.getCentreY() + push - area.getHeight() * 0.04f;
        const float tilt = juce::degreesToRadians (-5.0f)
                         + (on ? (flicker - 0.5f) * 0.012f : 0.0f);

        // 屏幕坐标系里的实际字形范围 —— 流挂和喷溅要用它
        const juce::Rectangle<float> textBox (cx - tb.getWidth()  * fit * 0.5f,
                                              cy - tb.getHeight() * fit * 0.5f,
                                              tb.getWidth()  * fit,
                                              tb.getHeight() * fit);

        {
            juce::Graphics::ScopedSaveState ss (g);
            // 缩放后再旋转，最后把字形的中心挪到按钮中心。
            // 这个作用域结束就退出变换 —— 流挂和喷溅必须画在屏幕坐标系里，
            // 否则会被字体的缩放系数一起缩掉（实测会跑到按钮外面去）。
            g.addTransform (juce::AffineTransform::scale (fit)
                                .followedBy (juce::AffineTransform::rotation (tilt, 0.0f, 0.0f))
                                .translated (cx - fit * tb.getCentreX(),
                                             cy - fit * tb.getCentreY()));

        // 描边：深色外圈，让 tag 从底色里"抠"出来。
        // 线宽要除以缩放系数 —— 现在整个坐标系被 scale 过，直接写 1.7 会被一起缩掉。
        g.setColour (juce::Colour (0xD8000000));
        g.strokePath (textPath, juce::PathStrokeType (1.8f / juce::jmax (1.0e-3f, fit),
                        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // 填色：开 = 奶白（发光感），关 = 主题强调色
        if (on)
        {
            g.setGradientFill (juce::ColourGradient (
                juce::Colour (0xFFFFFFF6), tb.getX(), tb.getY(),
                juce::Colour (0xFFFFE9A8), tb.getX(), tb.getBottom(), false));
        }
        else
        {
            g.setGradientFill (juce::ColourGradient (
                OzoCol::accent,  tb.getX(), tb.getY(),
                OzoCol::accent2, tb.getX(), tb.getBottom(), false));
        }
            g.fillPath (textPath);
        }

        // --- 流挂：字底挂下来的三滴（屏幕坐标系） ---
        drawDrips (g, textBox, on);

        // --- 喷溅 ---
        drawSpray (g, area, textBox, on);
    }

private:
    // 喷漆往下淌的那一滴。x 位置固定（手写感需要确定性，不能每次重绘都变），
    // 长度用固定种子的小随机。
    void drawDrips (juce::Graphics& g, const juce::Rectangle<float>& tb, bool on)
    {
        juce::Random rnd (0xD8179);
        const float w = 2.3f;

        for (int i = 0; i < 3; ++i)
        {
            const float x = tb.getX() + tb.getWidth() * (0.16f + 0.30f * (float) i)
                          + rnd.nextFloat() * 3.0f - 1.5f;
            const float len = 3.0f + rnd.nextFloat() * (on ? 7.0f : 3.5f);
            const float y0  = tb.getBottom() - 1.0f;

            g.setColour ((on ? juce::Colour (0xFFFDF3C8) : OzoCol::accent)
                            .withAlpha (on ? 0.85f : 0.45f));
            g.fillRoundedRectangle (x, y0, w, len, w * 0.5f);          // 流下的细条
            g.fillEllipse (x - 0.7f, y0 + len - 1.6f, w + 1.4f, w + 1.4f); // 末端那滴
        }
    }

    // 罐子喷出来的飞溅。固定种子 —— 每帧重画必须长得一样，否则会像噪点在闪。
    void drawSpray (juce::Graphics& g, const juce::Rectangle<float>& area,
                    const juce::Rectangle<float>& tb, bool on)
    {
        juce::Random rnd (0x5FFA11);
        const int n = on ? 26 : 14;

        for (int i = 0; i < n; ++i)
        {
            float x, y;
            // 一半撒在字周围，一半撒在整个按钮里
            if (i % 2 == 0)
            {
                x = tb.expanded (5.0f).getX() + rnd.nextFloat() * tb.expanded (5.0f).getWidth();
                y = tb.expanded (5.0f).getY() + rnd.nextFloat() * tb.expanded (5.0f).getHeight();
            }
            else
            {
                x = area.getX() + rnd.nextFloat() * area.getWidth();
                y = area.getY() + rnd.nextFloat() * area.getHeight();
            }

            const float rad = 0.45f + rnd.nextFloat() * 0.85f;
            g.setColour ((on ? juce::Colour (0xFFFFF3D0) : OzoCol::textDim)
                            .withAlpha ((on ? 0.30f : 0.22f) * (0.4f + rnd.nextFloat() * 0.6f)));
            g.fillEllipse (x - rad, y - rad, rad * 2.0f, rad * 2.0f);
        }
    }

    float flicker = 0.0f;
};

}
