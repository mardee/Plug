#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "OzoLookAndFeel.h"

namespace ozo
{

//==============================================================================
// WILD —— 全息彩虹胶囊开关（精美轻奢磨砂冰晶底板 + ✦ WILD 霓虹流光边框 + 浮现动效）
//==============================================================================
class WildButton : public juce::Button
{
public:
    WildButton() : juce::Button ("WILD")
    {
        setClickingTogglesState (true);
        setTooltip ("狂野模式：波形折叠 + 次八度合成 + 各级参数加倍 + 界面全面转深色");
    }

    void setTargetVisible (bool shouldShow)
    {
        if (targetVisible != shouldShow || (shouldShow && ! isVisible()))
        {
            targetVisible = shouldShow;
            if (targetVisible)
            {
                setVisible (true);
                if (appearProgress < 0.05f)
                {
                    appearProgress = 0.05f;
                    gleamTimer = 0.0f;
                }
            }
        }
    }

    void setFlicker (float f) noexcept
    {
        flicker = juce::jlimit (0.0f, 1.0f, f);
    }

    void setAnimationTime (float t) noexcept
    {
        animTime = t;

        // 浮现平滑弹簧动效与呼吸
        if (targetVisible)
        {
            appearProgress += (1.0f - appearProgress) * 0.18f;
            gleamTimer += 0.035f;
        }
        else
        {
            appearProgress += (0.0f - appearProgress) * 0.22f;
            if (appearProgress < 0.015f && isVisible())
                setVisible (false);
        }

        if (isVisible())
            repaint();
    }

    float getAppearProgress() const noexcept { return appearProgress; }

protected:
    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        if (appearProgress < 0.01f)
            return;

        const bool on = getToggleState();
        const auto bounds = getLocalBounds().toFloat();
        const float push = down ? 1.0f : 0.0f;

        // 浮现入场动效：从 85% 平滑放大至 100% + 透明度淡入
        const float scale = 0.85f + 0.15f * appearProgress;
        juce::Graphics::ScopedSaveState sss (g);
        g.addTransform (juce::AffineTransform::scale (scale, scale, bounds.getCentreX(), bounds.getCentreY()));
        g.setOpacity (appearProgress);

        const auto area = bounds.reduced (3.0f);
        const float r = area.getHeight() * 0.5f; // 胶囊圆角半径

        // =====================================================================
        // 1. 底板：浅色模式缎面微透全息冰晶底板，深色模式黑曜石暗影底板 + 珍珠冰蓝/兰花紫/珊瑚绯红渐变
        // =====================================================================
        if (on)
        {
            // 狂野开启：黑曜石底色 + 玫瑰珊瑚暗晶流光
            juce::ColourGradient darkGlass (
                juce::Colour (0xEE1C0A18), area.getX(), area.getY(),
                juce::Colour (0xF83A0D22), area.getX(), area.getBottom(), false);
            darkGlass.addColour (0.50, juce::Colour (0xEE2A0E22));
            g.setGradientFill (darkGlass);
            g.fillRoundedRectangle (area.translated (0.0f, push), r);

            // 动态横向全息色散光脉（珍珠冰蓝 -> 耀眼亮紫 -> 鲜亮炽红）
            {
                juce::Graphics::ScopedSaveState state (g);
                juce::Path clipPath;
                clipPath.addRoundedRectangle (area.translated (0.0f, push), r);
                g.reduceClipRegion (clipPath);

                const float period = 2.0f;
                const float phase = std::fmod (animTime, period) / period;
                const float totalDist = area.getWidth() + 90.0f;
                const float curX = area.getX() - 45.0f + phase * totalDist;

                juce::ColourGradient beam (
                    juce::Colour (0x00DBE6F9), curX - 35.0f, area.getY(),
                    juce::Colour (0x00FF2655), curX + 35.0f, area.getBottom(), false);
                beam.addColour (0.35, juce::Colour (0x44DBE6F9)); // 珍珠冰蓝
                beam.addColour (0.65, juce::Colour (0x77D62BFF)); // 耀眼亮紫
                beam.addColour (0.90, juce::Colour (0x77FF2655)); // 鲜亮炽红

                g.setGradientFill (beam);
                g.fillRect (area);
            }
        }
        else
        {
            // 浅色模式：通透柔和的磨砂冰晶底板（微量柔和阴影 + 纯净微透渐变）
            g.setColour (juce::Colour (0x10000000));
            g.fillRoundedRectangle (area.translated (0.0f, push + 1.5f), r);

            juce::ColourGradient lightGlass (
                juce::Colour (0xFAFFFFFF), area.getX(), area.getY(),
                juce::Colour (0xEEF3F6FC), area.getX(), area.getBottom(), false);
            lightGlass.addColour (0.40, juce::Colour (0xF7FCFEFF));
            g.setGradientFill (lightGlass);
            g.fillRoundedRectangle (area.translated (0.0f, push), r);

            if (highlighted)
            {
                g.setColour (juce::Colours::white.withAlpha (0.60f));
                g.fillRoundedRectangle (area.translated (0.0f, push), r);
            }
        }

        // =====================================================================
        // 2. 全息色散轮廓边框 (珍珠冰蓝 -> 耀眼亮紫 -> 鲜亮炽红)
        // =====================================================================
        {
            const float strokeW = on ? 2.0f : 1.4f;

            if (on)
            {
                // 深色开启态：微弱外发光日冕
                const float glowA = 0.28f + 0.20f * flicker;
                juce::ColourGradient halo (
                    juce::Colour (0xFFDBE6F9).withAlpha (glowA * 0.7f), area.getX(), area.getY(),
                    juce::Colour (0xFFFF2655).withAlpha (glowA), area.getRight(), area.getBottom(), false);
                halo.addColour (0.50, juce::Colour (0xFFD62BFF).withAlpha (glowA * 0.85f));

                g.setGradientFill (halo);
                g.drawRoundedRectangle (area.translated (0.0f, push), r, strokeW + 2.5f);
            }

            // 新风格配色：珍珠冰蓝 -> 耀眼亮紫 -> 鲜亮炽红 (浅色模式精致通透马卡龙渐变)
            juce::ColourGradient borderHolo (
                on ? juce::Colour (0xFFDBE6F9) : juce::Colour (0xFFB6A6E8),
                area.getX(), area.getY(),
                on ? juce::Colour (0xFFFF2655) : juce::Colour (0xFFE898AC),
                area.getRight(), area.getBottom(), false);

            if (on)
            {
                borderHolo.addColour (0.45, juce::Colour (0xFFD62BFF));
                borderHolo.addColour (0.80, juce::Colour (0xFFFF2655));
            }
            else
            {
                borderHolo.addColour (0.35, juce::Colour (0xFF9BB6F0));
                borderHolo.addColour (0.70, juce::Colour (0xFFCBA0DE));
            }

            g.setGradientFill (borderHolo);
            g.drawRoundedRectangle (area.translated (0.0f, push), r, strokeW);
        }

        // =====================================================================
        // 3. 入场高光扫光动效 (Appearance Gleam Wave)
        // =====================================================================
        if (gleamTimer < 1.0f)
        {
            juce::Graphics::ScopedSaveState state (g);
            juce::Path clipPath;
            clipPath.addRoundedRectangle (area.translated (0.0f, push), r);
            g.reduceClipRegion (clipPath);

            const float gleamX = area.getX() - 40.0f + gleamTimer * (area.getWidth() + 80.0f);
            juce::ColourGradient gleam (
                juce::Colours::white.withAlpha (0.0f), gleamX - 30.0f, area.getY(),
                juce::Colours::white.withAlpha (0.0f), gleamX + 30.0f, area.getBottom(), false);
            gleam.addColour (0.5, juce::Colours::white.withAlpha (0.70f * (1.0f - gleamTimer)));
            g.setGradientFill (gleam);
            g.fillRect (area);
        }

        // =====================================================================
        // 4. [✦ 星芒 + WILD 文字] 居中排版与高对比度文字
        // =====================================================================
        const auto font = juce::Font (juce::FontOptions (16.0f, juce::Font::bold | juce::Font::italic));
        g.setFont (font);

        juce::GlyphArrangement ga;
        ga.addLineOfText (font, "WILD", 0.0f, 0.0f);
        const float textW  = ga.getBoundingBox (0, -1, false).getWidth();
        const float starR  = area.getHeight() * 0.22f;
        const float starW  = starR * 2.0f;
        const float gap    = 8.0f;
        const float totalW = starW + gap + textW;

        const float startX = area.getCentreX() - (totalW * 0.5f);
        const float starCx = startX + starR;
        const float starCy = area.getCentreY() + push;
        const float textX  = startX + starW + gap;

        // 绘制四角星芒 ✦
        drawDiamondSparkle (g, starCx, starCy, starR, on);

        // 绘制斜体全息文字 "WILD"
        auto textBox = juce::Rectangle<float> (textX, area.getY() + push, textW + 6.0f, area.getHeight());

        if (on)
        {
            // 狂野开启态：白金-珊瑚绯红光辉文字
            g.setColour (juce::Colour (0xFFEA6355).withAlpha (0.45f));
            g.drawText ("WILD", textBox.translated (-1.0f, 0.0f), juce::Justification::centredLeft, false);
            g.drawText ("WILD", textBox.translated ( 1.0f, 0.0f), juce::Justification::centredLeft, false);

            juce::ColourGradient onTextGrad (
                juce::Colour (0xFFFFFFFF), textBox.getX(), textBox.getY(),
                juce::Colour (0xFFFFE4DE), textBox.getRight(), textBox.getY(), false);
            g.setGradientFill (onTextGrad);
            g.drawText ("WILD", textBox, juce::Justification::centredLeft, false);
        }
        else
        {
            // 浅色模式：与 PRISM 视觉体系统一的深紫罗兰-兰花紫渐变文字
            juce::ColourGradient holoTextGrad (
                juce::Colour (0xFF4A2882), textBox.getX(), textBox.getY(),
                juce::Colour (0xFF9E4882), textBox.getRight(), textBox.getY(), false);
            holoTextGrad.addColour (0.50, juce::Colour (0xFF6A3B9A));

            g.setGradientFill (holoTextGrad);
            g.drawText ("WILD", textBox, juce::Justification::centredLeft, false);
        }
    }

private:
    float flicker = 0.0f;
    float animTime = 0.0f;
    float appearProgress = 0.0f;
    float gleamTimer = 0.0f;
    bool  targetVisible = false;

    //--------------------------------------------------------------------------
    // 绘制四角星芒 (Diamond Sparkle ✦)
    //--------------------------------------------------------------------------
    void drawDiamondSparkle (juce::Graphics& g, float cx, float cy, float R, bool on)
    {
        juce::Path p;
        p.startNewSubPath (cx, cy - R);
        p.quadraticTo (cx, cy, cx + R, cy);
        p.quadraticTo (cx, cy, cx, cy + R);
        p.quadraticTo (cx, cy, cx - R, cy);
        p.quadraticTo (cx, cy, cx, cy - R);
        p.closeSubPath();

        if (on)
        {
            // 珍珠冰蓝 -> 珊瑚绯红
            juce::ColourGradient starGrad (
                juce::Colour (0xFFDBE6F9), cx, cy - R,
                juce::Colour (0xFFEA6355), cx, cy + R, false);
            starGrad.addColour (0.5, juce::Colour (0xFFC385D0));
            g.setGradientFill (starGrad);
            g.fillPath (p);

            // 中心高光
            g.setColour (juce::Colours::white);
            g.fillEllipse (cx - 1.2f, cy - 1.2f, 2.4f, 2.4f);
        }
        else
        {
            // 兰花紫 -> 珊瑚绯红
            juce::ColourGradient starGrad (
                juce::Colour (0xFF7C5CE0), cx - R, cy - R,
                juce::Colour (0xFFE56A88), cx + R, cy + R, false);
            starGrad.addColour (0.5, juce::Colour (0xFFB66FD8));
            g.setGradientFill (starGrad);
            g.fillPath (p);

            g.setColour (juce::Colours::white.withAlpha (0.90f));
            g.fillEllipse (cx - 1.0f, cy - 1.0f, 2.0f, 2.0f);
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WildButton)
};

} // namespace ozo
