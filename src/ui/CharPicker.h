#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "OzoLookAndFeel.h"

namespace ozo
{

//==============================================================================
// Character 三选一拨钮。
//
// 一条横槽，滑块停在当前档上，不是三个各自独立的按钮。
// 点哪一档就滑过去；滑块的位置按帧插值，所以切换是滑过去的，不是跳过去的。
//
// 它不自己持有参数：外部每帧用 setIndex 喂当前档，点击时通过 onSelect 回调出去。
class CharPicker : public juce::Component
{
public:
    std::function<void (int)> onSelect;

    void setIndex (int i) noexcept
    {
        const int clamped = juce::jlimit (0, kNum - 1, i);
        if (clamped != targetIndex)
        {
            targetIndex = clamped;
            repaint();
        }
    }

    void tick() noexcept
    {
        const float before = shownPos;
        shownPos += ((float) targetIndex - shownPos) * 0.28f;
        if (std::abs (shownPos - before) > 0.0005f)
            repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (0.5f);

        // 槽
        g.setColour (OzoCol::panelInner);
        g.fillRoundedRectangle (b, b.getHeight() * 0.5f);

        // 滑块：宽度占 1/3，跟着 shownPos 在槽里移动
        const float segW = b.getWidth() / (float) kNum;
        const float pad  = 3.0f;
        auto pill = juce::Rectangle<float> (b.getX() + shownPos * segW + pad,
                                            b.getY() + pad,
                                            segW - 2.0f * pad,
                                            b.getHeight() - 2.0f * pad);

        g.setGradientFill (OzoCol::holo (pill, 1.0f));
        g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);

        // 三档文字。滑块盖住的那档用浅字，其余用暗字。
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));

        for (int i = 0; i < kNum; ++i)
        {
            const float cx = b.getX() + ((float) i + 0.5f) * segW;
            const bool on = std::abs ((float) i - shownPos) < 0.5f;

            g.setColour (on ? OzoCol::btnOnText : OzoCol::textDim);
            g.drawText (names[i],
                        juce::Rectangle<float> (cx - segW * 0.5f, b.getY(), segW, b.getHeight()),
                        juce::Justification::centred, false);
        }

        g.setColour (OzoCol::panelEdge);
        g.drawRoundedRectangle (b, b.getHeight() * 0.5f, 1.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = juce::jlimit (0, kNum - 1,
            (int) ((float) e.x / (float) juce::jmax (1, getWidth()) * (float) kNum));

        if (onSelect != nullptr)
            onSelect (i);
    }

private:
    static constexpr int kNum = 3;
    const char* names[kNum] = { "Tape", "Tube", "Console" };

    int   targetIndex = 0;
    float shownPos    = 0.0f;   // 滑块当前停在哪，0..2 的小数
};

} // namespace ozo
