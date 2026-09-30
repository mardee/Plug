#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "OzoLookAndFeel.h"

namespace ozo
{

//==============================================================================
// 电平条，竖向细条。-60 dBFS 在底端，0 dBFS 在顶端，超过 -6 dB 转琥珀，接近 0 转红。
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

        // 标签在顶上，读数在底下
        g.setColour (OzoCol::textFaint);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText (label, juce::Rectangle<float> (b.getX(), b.getY() + 2.0f, b.getWidth(), 14.0f),
                    juce::Justification::centred, false);

        g.setColour (shownDb > -6.0f ? OzoCol::text : OzoCol::textDim);
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        g.drawText (shownDb > -59.5f ? juce::String (shownDb, 1) : juce::String ("-inf"),
                    juce::Rectangle<float> (b.getX(), b.getBottom() - 17.0f, b.getWidth(), 16.0f),
                    juce::Justification::centred, false);

        const float slotW = 12.0f;
        auto bar = juce::Rectangle<float> (b.getCentreX() - slotW * 0.5f,
                                           b.getY() + 19.0f,
                                           slotW,
                                           b.getHeight() - 40.0f);

        // 底槽
        g.setColour (OzoCol::panelInner.withAlpha (0.85f));
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (juce::Colour (0x18000000));
        g.drawRoundedRectangle (bar, 3.0f, 1.0f);

        // 填充从底端向上生长
        const float norm = juce::jlimit (0.0f, 1.0f, (shownDb + 60.0f) / 60.0f);
        if (norm > 0.001f)
        {
            const float h = bar.getHeight() * norm;
            auto fill = juce::Rectangle<float> (bar.getX(), bar.getBottom() - h, bar.getWidth(), h);

            juce::Colour c = OzoCol::mint;
            if (shownDb > -1.0f)      c = OzoCol::meterRed;
            else if (shownDb > -6.0f) c = OzoCol::meterAmber;

            g.setColour (c);
            g.fillRoundedRectangle (fill, 3.0f);
        }

        // -6 dB / -18 dB / -36 dB 参考刻度线
        g.setColour (OzoCol::textFaint.withAlpha (0.40f));
        const float dbs[] = { -6.0f, -18.0f, -36.0f };
        for (float d : dbs)
        {
            const float markY = bar.getBottom() - bar.getHeight() * ((d + 60.0f) / 60.0f);
            g.drawHorizontalLine (juce::roundToInt (markY), bar.getX() - 4.0f, bar.getX() - 1.0f);
            g.drawHorizontalLine (juce::roundToInt (markY), bar.getRight() + 1.0f, bar.getRight() + 4.0f);
        }
    }

private:
    juce::String label;
    float targetDb = -60.0f;
    float shownDb  = -60.0f;
};

} // namespace ozo
