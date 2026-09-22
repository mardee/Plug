#pragma once

#include <juce_graphics/juce_graphics.h>
#include <BinaryData.h>

namespace ozo
{

//==============================================================================
// 涂鸦字体
//
// 从二进制资源加载，不依赖用户系统装没装这个字体。
// Sedgwick Ave Display（SIL OFL 1.1，授权见 assets/fonts/OFL.txt）——
// 手写涂鸦/街头喷漆那一挂，正好配狂野模式。
//==============================================================================
struct Graffiti
{
    static juce::Typeface::Ptr typeface() noexcept
    {
        // 只加载一次，之后所有控件共用同一个 Typeface::Ptr
        static juce::Typeface::Ptr tf =
            juce::Typeface::createSystemTypefaceFor (
                BinaryData::SedgwickAveDisplayRegular_ttf,
                (size_t) BinaryData::SedgwickAveDisplayRegular_ttfSize);

        return tf;
    }

    static bool available() noexcept { return typeface() != nullptr; }

    static juce::Font font (float height, float tracking = 0.0f) noexcept
    {
        // 用 FontOptions 构造（Font(Typeface::Ptr) 在新 JUCE 里已废弃）
        if (auto tf = typeface())
            return juce::Font (juce::FontOptions (tf)
                                   .withHeight (height)
                                   .withKerningFactor (tracking));

        // 兜底：资源没打进来时至少还是个粗体，不会退化成默认字体看不出是涂鸦
        return juce::Font (juce::FontOptions (height * 1.15f, juce::Font::bold)
                               .withKerningFactor (tracking));
    }
};

}
