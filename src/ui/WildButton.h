#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "OzoLookAndFeel.h"

namespace ozo
{

//==============================================================================
// WILD —— 两张手绘涂鸦，不用字体
//
// 关：一只闭着的眼睛，眼角挂一滴。安静，但在看着你。
// 开：一只睁开的眼睛，虹膜是主题色渐变，瞳孔里有高光；
//     眼睛上方三道闪电，下面喷漆流挂，周围一圈飞溅。
//
// 两张都是路径画出来的，颜色走 OzoCol，常规/狂野主题自动跟着换。
// 打开时整体跟着 flicker 轻微缩放，外圈有呼吸光晕。
//==============================================================================
class WildButton : public juce::Button
{
public:
    WildButton() : juce::Button ("WILD")
    {
        setClickingTogglesState (true);
        setTooltip ("狂野模式：换算法 + 全部加倍 + 界面转深色");
    }

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
        const float r   = 8.0f;
        const float push = down ? 1.0f : 0.0f;

        if (on)
        {
            const float glowA = 0.40f + 0.30f * flicker;
            const float rad = area.getWidth() * 0.95f;
            juce::ColourGradient halo (OzoCol::tint[0].withAlpha (glowA),
                                       area.getCentreX(), area.getCentreY(),
                                       OzoCol::tint[0].withAlpha (0.0f),
                                       area.getCentreX() + rad, area.getCentreY(), true);
            g.setGradientFill (halo);
            g.fillRoundedRectangle (area.expanded (8.0f), r + 8.0f);
        }

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

        g.setColour (on ? juce::Colour (0xFFFDF3C8).withAlpha (0.92f)
                        : OzoCol::panelEdge.darker (0.25f));
        g.drawRoundedRectangle (area.translated (0.0f, push), r, on ? 2.0f : 1.2f);

        // 涂鸦几乎铺满按钮。上一版在 30px 高的按钮里再缩一圈，图案只剩十几像素，
        // 根本看不出画的是什么。
        const float breathe = on ? 1.0f + (flicker - 0.5f) * 0.04f : 1.0f;
        auto box = area.reduced (area.getWidth() * 0.06f, area.getHeight() * 0.10f)
                       .translated (0.0f, push);
        box = box.withSizeKeepingCentre (box.getWidth() * breathe, box.getHeight() * breathe);

        if (on) drawEyeOpen (g, box);
        else    drawStar    (g, box);

        drawSpray (g, area, on);
    }

private:
    //--------------------------------------------------------------------------
    // 关：一颗五角星，中心一颗十字高光。轮廓粗、填色满，小尺寸上也认得出。
    void drawStar (juce::Graphics& g, juce::Rectangle<float> box)
    {
        const auto c = box.getCentre();
        const float R = juce::jmin (box.getWidth(), box.getHeight()) * 0.50f;
        const float r = R * 0.40f;

        juce::Path star;
        for (int i = 0; i < 5; ++i)
        {
            const float a = -juce::MathConstants<float>::halfPi + (float) i * 1.256637f;
            const float x = c.x + std::cos (a) * R;
            const float y = c.y + std::sin (a) * R;
            if (i == 0) star.startNewSubPath (x, y); else star.lineTo (x, y);

            const float b = a + 0.628319f;
            star.lineTo (c.x + std::cos (b) * r, c.y + std::sin (b) * r);
        }
        star.closeSubPath();

        g.setColour (juce::Colour (0xD8000000));
        g.strokePath (star, juce::PathStrokeType (4.5f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
        g.setGradientFill (juce::ColourGradient (OzoCol::accent,  c.x, c.y - R,
                                                 OzoCol::accent2, c.x, c.y + R, false));
        g.fillPath (star);

        // 中心高光
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.drawLine (c.x - R * 0.16f, c.y, c.x + R * 0.16f, c.y, 1.6f);
        g.drawLine (c.x, c.y - R * 0.16f, c.x, c.y + R * 0.16f, 1.6f);
    }

    //--------------------------------------------------------------------------
    // 开：一只几乎占满按钮的眼睛。杏仁眼白、渐变虹膜、瞳孔、两颗高光，
    // 眼睛上方三道闪电。
    void drawEyeOpen (juce::Graphics& g, juce::Rectangle<float> box)
    {
        const auto c = box.getCentre();
        const float rx = box.getWidth()  * 0.46f;
        const float ry = box.getHeight() * 0.34f;

        juce::Path eye;
        eye.addCentredArc (c.x, c.y, rx, ry, 0.0f,
                           0.10f, juce::MathConstants<float>::pi - 0.10f, true);
        eye.addCentredArc (c.x, c.y, rx, ry, 0.0f,
                           juce::MathConstants<float>::pi + 0.10f,
                           juce::MathConstants<float>::twoPi - 0.10f, false);
        eye.closeSubPath();

        g.setColour (juce::Colour (0xFFFDFCFA));
        g.fillPath (eye);
        g.setColour (juce::Colour (0xD8000000));
        g.strokePath (eye, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

        const float ir = ry * 0.72f;
        juce::ColourGradient iris (OzoCol::tint[2], c.x - ir * 0.3f, c.y - ir * 0.3f,
                                   OzoCol::tint[0], c.x + ir, c.y + ir, true);
        g.setGradientFill (iris);
        g.fillEllipse (c.x - ir, c.y - ir, ir * 2.0f, ir * 2.0f);

        const float pr = ir * 0.48f;
        g.setColour (juce::Colour (0xFF14080E));
        g.fillEllipse (c.x - pr, c.y - pr * 0.9f, pr * 2.0f, pr * 2.0f);

        g.setColour (juce::Colours::white.withAlpha (0.95f));
        g.fillEllipse (c.x - pr * 0.72f, c.y - pr * 1.05f, pr * 0.62f, pr * 0.62f);
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.fillEllipse (c.x + pr * 0.30f, c.y + pr * 0.05f, pr * 0.28f, pr * 0.28f);

        drawBolts (g, box);
        drawDrips (g, box);
    }

        // 眼睛上方的三道闪电。锯齿路径，固定形状，不随机。
    void drawBolts (juce::Graphics& g, juce::Rectangle<float> box)
    {
        const float top = box.getY() - box.getHeight() * 0.06f;
        const float xs[3] = { box.getX() + box.getWidth() * 0.22f,
                              box.getCentreX(),
                              box.getRight() - box.getWidth() * 0.22f };
        const float h = box.getHeight() * 0.30f;

        g.setColour (juce::Colour (0xFFFDF3C8).withAlpha (0.95f));
        for (float x : xs)
        {
            juce::Path bolt;
            bolt.startNewSubPath (x,            top - h);
            bolt.lineTo          (x - h * 0.34f, top - h * 0.46f);
            bolt.lineTo          (x + h * 0.10f, top - h * 0.46f);
            bolt.lineTo          (x - h * 0.16f, top);
            g.strokePath (bolt, juce::PathStrokeType (1.7f, juce::PathStrokeType::mitered,
                                                      juce::PathStrokeType::butt));
        }
    }

    // 眼角流下来的两滴喷漆
    void drawDrips (juce::Graphics& g, juce::Rectangle<float> box)
    {
        g.setColour (juce::Colour (0xFFFDF3C8).withAlpha (0.85f));
        const float xs[2] = { box.getX() + box.getWidth() * 0.30f,
                              box.getX() + box.getWidth() * 0.66f };
        for (int i = 0; i < 2; ++i)
        {
            const float len = box.getHeight() * (i == 0 ? 0.34f : 0.22f);
            const float y0  = box.getBottom() - box.getHeight() * 0.18f;
            g.fillRoundedRectangle (xs[i], y0, 2.2f, len, 1.1f);
            g.fillEllipse (xs[i] - 1.1f, y0 + len - 1.5f, 4.4f, 4.8f);
        }
    }

    // 喷溅：固定种子，每帧一样，否则会像噪点闪
    void drawSpray (juce::Graphics& g, juce::Rectangle<float> area, bool on)
    {
        juce::Random rnd (0x5FFA11);
        const int n = on ? 22 : 10;

        for (int i = 0; i < n; ++i)
        {
            const float x = area.getX() + rnd.nextFloat() * area.getWidth();
            const float y = area.getY() + rnd.nextFloat() * area.getHeight();
            const float rad = 0.4f + rnd.nextFloat() * 0.8f;
            g.setColour ((on ? juce::Colour (0xFFFFF3D0) : OzoCol::textDim)
                            .withAlpha ((on ? 0.34f : 0.20f) * (0.4f + rnd.nextFloat() * 0.6f)));
            g.fillEllipse (x - rad, y - rad, rad * 2.0f, rad * 2.0f);
        }
    }

    float flicker = 0.0f;
};

} // namespace ozo
