#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "OzoLookAndFeel.h"
#include "Layout.h"
#include "../PluginProcessor.h"

namespace ozo
{

//==============================================================================
// 面板层 —— 夹在频谱层和粒子层之间
//
// 之所以要从 Editor::paint 里搬出来：JUCE 的子组件永远画在父组件之上。
// 频谱要垫在面板**底下**，粒子要飘在面板**上面**，
// 那面板本身就必须是一个独立的子组件，才能被夹在这两层中间。
//
// 支持两种视图形态：
// - ONE-KNOB 模式：极简深空画布，完全隐藏卡片框和侧栏，让中央光球独占沉浸视觉
// - EXPERT 模式：经典卡片底板、参数分区与电平表磨砂底座
//==============================================================================
class PanelLayer : public juce::Component
{
public:
    explicit PanelLayer (EZampProcessor& p) : processor (p)
    {
        setInterceptsMouseClicks (false, false);
    }

    void setDrawerProgress (float p)
    {
        p = juce::jlimit (0.0f, 1.0f, p);
        if (std::abs (drawerProgress - p) > 0.001f)
        {
            drawerProgress = p;
            repaint();
        }
    }

    void setWildMode (bool wild)
    {
        if (isWild != wild)
        {
            isWild = wild;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        // ---------------------------------------------------------------------
        // 1. 品牌 Header
        // ---------------------------------------------------------------------
        g.setGradientFill (OzoCol::holo (juce::Rectangle<float> (24.0f, 12.0f, 70.0f, 32.0f), 1.0f));
        g.setFont (juce::Font (juce::FontOptions (30.0f, juce::Font::bold)));
        g.drawText ("ozo", 24, 12, 76, 32, juce::Justification::centredLeft, false);

        g.setColour (OzoCol::text);
        g.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
        g.drawText ("PRISM", 84, 18, 90, 26, juce::Justification::centredLeft, false);

        if (expanded)
        {
            g.setColour (OzoCol::textFaint);
            g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
            g.drawText ("S T U D I O   C O L O R", 26, 44, 160, 12,
                        juce::Justification::centredLeft, false);
        }

        // ---------------------------------------------------------------------
        // 2. EXPERT 侧边抽屉浮窗背景底板（宽 320px，平滑进出过渡）
        // ---------------------------------------------------------------------
        if (drawerProgress > 0.005f)
        {
            const float dw = (float) Layout::drawerW;
            const float dh = (float) getHeight();
            // dx 随 drawerProgress 从 getWidth() (收起) 滑入到 Layout::drawerX (展开)
            const float dx = (float) getWidth() - dw * drawerProgress;
            auto drawerArea = juce::Rectangle<float> (dx, 0.0f, dw, dh);

            juce::Graphics::ScopedSaveState sss (g);
            g.setOpacity (drawerProgress);

            if (isWild)
            {
                // =============================================================
                // 深色狂野模式：黑曜石暗夜红晶抽屉底板 + 霓虹亮紫与炽红边框
                // =============================================================
                g.setColour (juce::Colour (0x60000000));
                g.fillRect (drawerArea.translated (-5.0f, 0.0f).withWidth (5.0f));

                juce::ColourGradient darkDrawerBg (
                    juce::Colour (0xF21C0A1A), dx, 0.0f,
                    juce::Colour (0xFA320E22), dx + dw, dh, false);
                darkDrawerBg.addColour (0.50, juce::Colour (0xF0240D1E));
                g.setGradientFill (darkDrawerBg);
                g.fillRect (drawerArea);

                // 左侧全息流光边界细线 (珍珠冰蓝 -> 耀眼亮紫 -> 鲜亮炽红)
                juce::ColourGradient holoBorder (
                    juce::Colour (0xFFDBE6F9), dx, 0.0f,
                    juce::Colour (0xFFFF2655), dx, dh, false);
                holoBorder.addColour (0.50, juce::Colour (0xFFD62BFF));
                g.setGradientFill (holoBorder);
                g.fillRect (dx, 0.0f, 1.5f, dh);

                // 抽屉内部三大暗夜高透卡片槽位
                auto drawCardDark = [&] (float y, float h, const juce::String& title)
                {
                    auto card = juce::Rectangle<float> (dx + 14.0f, y, dw - 28.0f, h);
                    g.setColour (juce::Colour (0x28000000));
                    g.fillRoundedRectangle (card, 8.0f);
                    g.setColour (juce::Colour (0x25FFFFFF));
                    g.drawRoundedRectangle (card, 8.0f, 1.0f);

                    if (title.isNotEmpty())
                    {
                        g.setColour (OzoCol::tintText[1]);
                        g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
                        g.drawText (title, card.getX() + 10.0f, card.getY() + 6.0f, 140.0f, 12.0f,
                                    juce::Justification::centredLeft, false);
                    }
                };

                drawCardDark (50.0f, 130.0f, "TONE SHAPING");
                drawCardDark (190.0f, 140.0f, "CHARACTER & ROUTING");
                drawCardDark (340.0f, 136.0f, "LEVELS & PRESETS");
            }
            else
            {
                // =============================================================
                // 浅色模式：通透柔和的磨砂冰晶底板 + 缎面卡片
                // =============================================================
                g.setColour (juce::Colour (0x1A000000));
                g.fillRect (drawerArea.translated (-4.0f, 0.0f).withWidth (4.0f));

                juce::ColourGradient drawerBg (
                    juce::Colour (0xF5FFFFFF), dx, 0.0f,
                    juce::Colour (0xEAF0F6FA), dx + dw, dh, false);
                drawerBg.addColour (0.50, juce::Colour (0xF0FAFCFF));
                g.setGradientFill (drawerBg);
                g.fillRect (drawerArea);

                // 抽屉左边缘全息色散细光线
                juce::ColourGradient holoBorder (
                    juce::Colour (0xFF9BB6F0), dx, 0.0f,
                    juce::Colour (0xFFE898AC), dx, dh, false);
                holoBorder.addColour (0.50, juce::Colour (0xFFCBA0DE));
                g.setGradientFill (holoBorder);
                g.fillRect (dx, 0.0f, 1.5f, dh);

                // 抽屉内部三大功能卡片槽位
                auto drawCard = [&] (float y, float h, const juce::String& title)
                {
                    auto card = juce::Rectangle<float> (dx + 14.0f, y, dw - 28.0f, h);
                    g.setColour (juce::Colour (0x40FFFFFF));
                    g.fillRoundedRectangle (card, 8.0f);
                    g.setColour (juce::Colour (0x12000000));
                    g.drawRoundedRectangle (card, 8.0f, 1.0f);

                    if (title.isNotEmpty())
                    {
                        g.setColour (OzoCol::textDim);
                        g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
                        g.drawText (title, card.getX() + 10.0f, card.getY() + 6.0f, 140.0f, 12.0f,
                                    juce::Justification::centredLeft, false);
                    }
                };

                drawCard (50.0f, 130.0f, "TONE SHAPING");
                drawCard (190.0f, 140.0f, "CHARACTER & ROUTING");
                drawCard (340.0f, 136.0f, "LEVELS & PRESETS");
            }
        }

        // ---------------------------------------------------------------------
        // 3. 页脚状态信息
        // ---------------------------------------------------------------------
        drawFooter (g);
    }

    // 页脚的匹配增益是活的，由 Editor 定时调一下（只重绘这一条）
    void refreshFooter()
    {
        repaint (0, Layout::footerY, getWidth(), getHeight() - Layout::footerY);
    }

private:
    void drawFooter (juce::Graphics& g)
    {
        g.setColour (OzoCol::textDim);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));

        const bool hqOn = getBoolParam (ParamID::hq);
        const bool matchOn = getBoolParam (ParamID::match);

        const auto dot = juce::String::fromUTF8 ("  \xc2\xb7  ");
        juce::String left = "Over-sampling " + juce::String (hqOn ? "4x" : "2x")
                          + dot + "Latency " + juce::String (processor.getLatencySamples())
                          + " smp";

        if (matchOn)
            left += dot + "Auto Match "
                 + juce::String (processor.getChain().getMatchGainDb(), 1) + " dB";

        g.drawText (left, 24, Layout::footerY + 8, 420, 16,
                    juce::Justification::centredLeft, false);

        g.drawText ("v1.1.0", getWidth() - 74, Layout::footerY + 8, 50, 16,
                    juce::Justification::centredRight, false);
    }

    bool getBoolParam (const juce::String& id) const
    {
        auto* p = processor.getAPVTS().getRawParameterValue (id);
        return p != nullptr && p->load() > 0.5f;
    }

    bool expanded = false;
    bool isWild   = false;
    float drawerProgress = 0.0f;
    EZampProcessor& processor;
};

} // namespace ozo
