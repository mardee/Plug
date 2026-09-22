#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "OzoLookAndFeel.h"

namespace ozo
{

//==============================================================================
// 增益衰减表。0 dB 在右端，向左最深 -24 dB。
// 颜色随深度从绿转琥珀再转红 —— 一眼看出压得轻还是压得狠。
class GrMeter : public juce::Component
{
public:
    void setGainReductionDb (float db) noexcept { targetDb = juce::jlimit (-24.0f, 0.0f, db); }
    void tick() noexcept { shownDb += (targetDb - shownDb) * 0.25f; }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();

        // 槽
        g.setColour (OzoCol::panelInner);
        g.fillRoundedRectangle (b, 4.0f);

        // 填充（从右端 0 dB 向左生长）
        const float norm = juce::jlimit (0.0f, 1.0f, -shownDb / 24.0f);
        if (norm > 0.0015f)
        {
            const float w = b.getWidth() * norm;
            auto fill = juce::Rectangle<float> (b.getRight() - w, b.getY(), w, b.getHeight());

            juce::Colour c = OzoCol::meterGreen;
            if (norm > 0.70f)      c = OzoCol::meterRed;
            else if (norm > 0.35f) c = OzoCol::meterAmber;

            g.setColour (c);
            g.fillRoundedRectangle (fill, 4.0f);
        }

        // 刻度：-6 / -12 / -18
        g.setColour (juce::Colour (0x33000000));
        for (int i = 1; i <= 3; ++i)
        {
            const float xv = b.getRight() - b.getWidth() * (i * 6.0f / 24.0f);
            g.drawVerticalLine (juce::roundToInt (xv), b.getY() + 4.0f, b.getBottom() - 4.0f);
        }

        // 边框
        g.setColour (OzoCol::panelEdge);
        g.drawRoundedRectangle (b, 4.0f, 1.0f);

        // 读数
        g.setColour (shownDb < -0.05f ? OzoCol::text : OzoCol::textDim);
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        g.drawText (juce::String (shownDb, 1) + " dB",
                    b.reduced (8, 0), juce::Justification::centredLeft, false);
    }

private:
    float targetDb = 0.0f;
    float shownDb  = 0.0f;
};

//==============================================================================
// 电平条。-60 dBFS 到 0 dBFS，超过 -6 dB 转琥珀，接近 0 转红。
class LevelBar : public juce::Component
{
public:
    explicit LevelBar (juce::String labelText) : label (std::move (labelText)) {}

    void setLevelDb (float db) noexcept { targetDb = juce::jlimit (-60.0f, 6.0f, db); }
    void tick() noexcept
    {
        const float c = targetDb > shownDb ? 0.45f : 0.12f;   // 起快落慢，像真表
        shownDb += (targetDb - shownDb) * c;
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();

        // 标签
        g.setColour (OzoCol::textFaint);
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        g.drawText (label, juce::Rectangle<int> (0, 0, 26, (int) b.getHeight()),
                    juce::Justification::centredLeft, false);

        auto bar = b.withTrimmedLeft (30.0f);

        g.setColour (OzoCol::panelInner);
        g.fillRoundedRectangle (bar, 2.0f);

        const float norm = juce::jlimit (0.0f, 1.0f, (shownDb + 60.0f) / 60.0f);
        if (norm > 0.001f)
        {
            auto fill = bar.withWidth (bar.getWidth() * norm);

            juce::Colour c = OzoCol::mint;
            if (shownDb > -1.0f)      c = OzoCol::meterRed;
            else if (shownDb > -6.0f) c = OzoCol::meterAmber;

            g.setColour (c);
            g.fillRoundedRectangle (fill, 2.0f);
        }

        // -6 dB 参考线
        const float markX = bar.getX() + bar.getWidth() * (54.0f / 60.0f);
        g.setColour (juce::Colour (0x3A000000));
        g.drawVerticalLine (juce::roundToInt (markX), bar.getY(), bar.getBottom());
    }

private:
    juce::String label;
    float targetDb = -60.0f;
    float shownDb  = -60.0f;
};

} // namespace ozo
