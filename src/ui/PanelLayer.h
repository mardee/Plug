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
// 面板做成上实下透的垂直渐变：顶部的旋钮区保持干净好读，
// 底部透得多一些，让底下的频谱柱能透上来。
class PanelLayer : public juce::Component
{
public:
    explicit PanelLayer (EZampProcessor& p) : processor (p)
    {
        // 它铺满整屏，绝不能吃掉下面控件的鼠标事件
        setInterceptsMouseClicks (false, false);
    }

    void paint (juce::Graphics& g) override
    {
        auto panel1 = juce::Rectangle<int> (Layout::margin, Layout::panel1Y,
                                            getWidth() - 2 * Layout::margin, Layout::panel1H);
        auto panel2 = juce::Rectangle<int> (Layout::margin, Layout::panel2Y,
                                            getWidth() - 2 * Layout::margin, Layout::panel2H);

        // ---- 面板 ----
        // 做得很透：只留一层极淡的卡纸感，让底下的频谱整片透上来。
        // 旋钮、文字本来就在面板之上的独立控件里，不受这一层影响。
        for (const auto& panel : { panel1, panel2 })
        {
            g.setGradientFill (juce::ColourGradient::vertical (
                OzoCol::sheenTop, juce::Colour (0x06FFFFFF), panel.toFloat()));
            g.fillRoundedRectangle (panel.toFloat(), 12.0f);

            // 边线也压到很淡，只当个"卡片边界"的暗示，不糊住波形
            g.setColour (OzoCol::panelEdge.withAlpha (0.30f));
            g.drawRoundedRectangle (panel.toFloat(), 12.0f, 1.0f);
        }

        // 分隔线：把旋钮区和 Character 区隔开
        g.drawHorizontalLine (panel1.getY() + 132, panel1.getX() + 16.0f, panel1.getRight() - 16.0f);

        // ---- 品牌 ----
        // drawText 会跟着当前 fill 走，所以先 setGradientFill 再画，就能得到虹彩字
        g.setGradientFill (OzoCol::holo (juce::Rectangle<float> (24.0f, 12.0f, 70.0f, 32.0f), 1.0f));
        g.setFont (juce::Font (juce::FontOptions (30.0f, juce::Font::bold)));
        g.drawText ("ozo", 24, 12, 76, 32, juce::Justification::centredLeft, false);

        g.setColour (OzoCol::text);
        g.setFont (juce::Font (juce::FontOptions (18.0f)));
        g.drawText ("EZamp", 84, 18, 90, 26, juce::Justification::centredLeft, false);

        g.setColour (OzoCol::textFaint);
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        g.drawText ("S T U D I O   C O L O R", 26, 44, 160, 12,
                    juce::Justification::centredLeft, false);

        // ---- 面板内的小标题 ----
        g.setColour (OzoCol::textDim);
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));

        g.drawText ("CHARACTER", panel1.getX() + 16, panel1.getY() + 140, 120, 12,
                    juce::Justification::centredLeft, false);

        g.drawText ("GAIN REDUCTION", 290, panel2.getY() + 12, 160, 12,
                    juce::Justification::centredLeft, false);

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

        juce::String left = "Over-sampling " + juce::String (hqOn ? "4x" : "2x")
                          + "   ·   Latency " + juce::String (processor.getLatencySamples())
                          + " smp";

        if (matchOn)
            left += "   ·   Auto Match "
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

    EZampProcessor& processor;
};

} // namespace ozo
