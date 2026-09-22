#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ozo
{

//==============================================================================
// ozo 的视觉语言：镭射粉彩
//
// 浅底 + 四色粉彩（亮粉 / 薰衣草 / 薄荷 / 蜜桃）+ 全息渐变。
// 高亮色永远走渐变而不是单色——单色做不到"镭射"，只有彩虹渐层才有那层虹彩。
// 文字统一用深紫黑：粉彩底上用纯黑会显得脏，用深紫能和粉彩呼应。
namespace OzoCol
{
    inline juce::Colour bg         { 0xFFF7F3FD };   // 极浅薰衣草白
    inline juce::Colour bgTop      { 0xFFFFFFFF };
    inline juce::Colour panel      { 0xEEFFFFFF };  // 半透明白，让粒子透上来
    inline juce::Colour panelEdge  { 0xFFE3D9F5 };
    inline juce::Colour panelInner { 0xFFF0EAFB };

    inline juce::Colour text       { 0xFF2E2440 };   // 深紫黑
    inline juce::Colour textDim    { 0xFF6E6486 };
    inline juce::Colour textFaint  { 0xFFA096B8 };

    // 粉彩四色
    inline juce::Colour pink       { 0xFFFF8FD0 };
    inline juce::Colour lavender   { 0xFFB49CFF };
    inline juce::Colour mint       { 0xFF7FE3D4 };
    inline juce::Colour peach      { 0xFFFFC98B };

    // 四色主题 —— 四个染色旋钮各占一个，顺序固定：
    //   0 亮粉 = Drive   1 薰衣草 = Weight   2 薄荷 = Air   3 蜜桃 = Glue
    // 背景的频谱与粒子也用同一套，旋钮开多大，对应色就在背景上占多大。
    inline juce::Colour tint[4] = { pink, lavender, mint, peach };

    // 粉彩太亮，浅底上做文字看不清，所以每色配一个加深版
    inline juce::Colour tintText[4] =
    {
        juce::Colour { 0xFFC53D9C },   // 亮粉 → 品红
        juce::Colour { 0xFF6B4BC8 },   // 薰衣草 → 紫
        juce::Colour { 0xFF1E9B87 },   // 薄荷 → 青
        juce::Colour { 0xFFB96A22 }    // 蜜桃 → 赭
    };

    inline juce::Colour accent     { 0xFFFF8FD0 };   // 主高亮 = 亮粉
    inline juce::Colour accentDim  { 0xFFF0B8E0 };
    inline juce::Colour accent2    { 0xFF7FE3D4 };   // 薄荷，次要信息
    inline juce::Colour accentText { 0xFFC53D9C };   // 加深的粉，浅底上做文字用

    inline juce::Colour track      { 0xFFE9E1F7 };
    inline juce::Colour knobFace   { 0xFFFFFFFF };
    inline juce::Colour knobEdge   { 0xFFDCCFF0 };

    // 预设按钮占着窗口最底下那条，实底会把频谱挡死，所以留一点透
    inline juce::Colour btnBg      { 0xE8F4EEFC };
    inline juce::Colour btnHi      { 0xE0EBE2FA };
    inline juce::Colour btnOnText  { 0xFFFFFFFF };

    inline juce::Colour meterGreen { 0xFF6FD8B4 };
    inline juce::Colour meterAmber { 0xFFFFC46B };
    inline juce::Colour meterRed   { 0xFFFF7A9C };

    // 下面这几个原本硬编码在各绘制函数里（白旋钮、白高光、黑投影）。
    // 深底上白色会刺眼、黑色看不见，所以提成变量跟着主题走。
    inline juce::Colour knobFace2  { 0xFFF6F1FD };   // 旋钮本体渐变的另一端
    inline juce::Colour knobShadow { 0x1A000000 };
    inline juce::Colour sheenTop   { 0x1CFFFFFF };   // 面板顶部那道高光
    inline juce::Colour btnHi2     { 0x33FFFFFF };   // 按钮选中态的玻璃高光
    inline juce::Colour popupBg    { 0xFFFDFBFF };

    //--------------------------------------------------------------------------
    // 整套换色。wild = false 镭射粉彩 / true 炽热暗色。
    //
    // 狂野那套不是"把粉彩调暗"——粉彩压暗会发脏。它是换成高饱和的
    // 炽红 / 电紫 / 电光青 / 熔橙，让频谱在黑底上真的烧起来。
    inline void apply (bool wild) noexcept
    {
        if (! wild)
        {
            bg = juce::Colour (0xFFF7F3FD); bgTop = juce::Colour (0xFFFFFFFF); panel = juce::Colour (0xEEFFFFFF);
            panelEdge = juce::Colour (0xFFE3D9F5); panelInner = juce::Colour (0xFFF0EAFB); popupBg = juce::Colour (0xFFFDFBFF);

            text = juce::Colour (0xFF2E2440); textDim = juce::Colour (0xFF6E6486); textFaint = juce::Colour (0xFFA096B8);

            pink = juce::Colour (0xFFFF8FD0); lavender = juce::Colour (0xFFB49CFF);
            mint = juce::Colour (0xFF7FE3D4); peach    = juce::Colour (0xFFFFC98B);

            // 浅底上文字要加深才看得清
            tintText[0] = juce::Colour (0xFFC53D9C); tintText[1] = juce::Colour (0xFF6B4BC8);
            tintText[2] = juce::Colour (0xFF1E9B87); tintText[3] = juce::Colour (0xFFB96A22);

            accent = juce::Colour (0xFFFF8FD0); accentDim = juce::Colour (0xFFF0B8E0);
            accent2 = juce::Colour (0xFF7FE3D4); accentText = juce::Colour (0xFFC53D9C);

            track = juce::Colour (0xFFE9E1F7); knobFace = juce::Colour (0xFFFFFFFF); knobFace2 = juce::Colour (0xFFF6F1FD);
            knobEdge = juce::Colour (0xFFDCCFF0); knobShadow = juce::Colour (0x1A000000);
            sheenTop = juce::Colour (0x1CFFFFFF); btnHi2 = juce::Colour (0x33FFFFFF);

            btnBg = juce::Colour (0xE8F4EEFC); btnHi = juce::Colour (0xE0EBE2FA); btnOnText = juce::Colour (0xFFFFFFFF);

            meterGreen = juce::Colour (0xFF6FD8B4); meterAmber = juce::Colour (0xFFFFC46B); meterRed = juce::Colour (0xFFFF7A9C);
        }
        else
        {
            bg = juce::Colour (0xFF0A0710); bgTop = juce::Colour (0xFF1C1026); panel = juce::Colour (0xCC16101E);
            panelEdge = juce::Colour (0xFF3A2438); panelInner = juce::Colour (0xFF1E1426); popupBg = juce::Colour (0xFF1A1224);

            text = juce::Colour (0xFFFFF2F8); textDim = juce::Colour (0xFFBCA9CE); textFaint = juce::Colour (0xFF7C6A8E);

            pink = juce::Colour (0xFFFF2A3F); lavender = juce::Colour (0xFFB429FF);
            mint = juce::Colour (0xFF00E9FF); peach    = juce::Colour (0xFFFF9500);

            // 深底上反过来，文字要提亮
            tintText[0] = juce::Colour (0xFFFF6B7A); tintText[1] = juce::Colour (0xFFCE6BFF);
            tintText[2] = juce::Colour (0xFF5CF0FF); tintText[3] = juce::Colour (0xFFFFB43D);

            accent = juce::Colour (0xFFFF2A3F); accentDim = juce::Colour (0xFF7A1626);
            accent2 = juce::Colour (0xFF00E9FF); accentText = juce::Colour (0xFFFF6B7A);

            track = juce::Colour (0xFF2A1E33); knobFace = juce::Colour (0xFF241A2C); knobFace2 = juce::Colour (0xFF160F1D);
            knobEdge = juce::Colour (0xFF4A3358); knobShadow = juce::Colour (0x50000000);
            sheenTop = juce::Colour (0x0EFFFFFF); btnHi2 = juce::Colour (0x1AFFFFFF);

            btnBg = juce::Colour (0xD9201729); btnHi = juce::Colour (0xD9302139); btnOnText = juce::Colour (0xFF120A16);

            meterGreen = juce::Colour (0xFF00E9FF); meterAmber = juce::Colour (0xFFFF9500); meterRed = juce::Colour (0xFFFF2A3F);
        }

        // 四色主题数组必须在这里重填：绘制代码读的是 tint[]，不是 pink/lavender 本身
        tint[0] = pink; tint[1] = lavender; tint[2] = mint; tint[3] = peach;
    }

    //--------------------------------------------------------------------------
    // 全息渐变：粉 → 薰衣草 → 薄荷 → 蜜桃。
    // 所有需要"镭射感"的地方都调它，保证整屏的虹彩方向一致。
    inline juce::ColourGradient holo (juce::Rectangle<float> area, float alpha) noexcept
    {
        juce::ColourGradient g (pink.withAlpha (alpha),     area.getX(),      area.getY(),
                                peach.withAlpha (alpha),    area.getRight(),  area.getBottom(),
                                false);
        g.addColour (0.34, lavender.withAlpha (alpha));
        g.addColour (0.68, mint.withAlpha (alpha));
        return g;
    }

    inline juce::ColourGradient holoHorizontal (float x0, float x1, float alpha) noexcept
    {
        juce::ColourGradient g (pink.withAlpha (alpha),     x0, 0.0f,
                                peach.withAlpha (alpha),    x1, 0.0f, false);
        g.addColour (0.34, lavender.withAlpha (alpha));
        g.addColour (0.68, mint.withAlpha (alpha));
        return g;
    }

    //--------------------------------------------------------------------------
    // 单主题色的渐变：同色由深到浅。
    // 旋钮归属某一色之后就不能再用四色全息了 —— 那样四个旋钮会长得一模一样，
    // 看不出谁是谁。同色系渐变既保留光泽，又守住了色彩身份。
    inline juce::ColourGradient tintGlow (const juce::Colour& c,
                                          juce::Rectangle<float> area,
                                          float alpha) noexcept
    {
        return juce::ColourGradient (c.darker  (0.18f).withAlpha (alpha),
                                     area.getX(),     area.getY(),
                                     c.brighter (0.22f).withAlpha (alpha),
                                     area.getRight(), area.getBottom(),
                                     false);
    }

    // Slider 上挂的自定义属性名，用来告诉 LookAndFeel 这个旋钮属于哪个主题色
    const inline juce::Identifier propTheme { "ozoTint" };
}

//==============================================================================
class OzoLookAndFeel : public juce::LookAndFeel_V4
{
public:
    OzoLookAndFeel() { refreshColours(); }

    // 换主题之后必须重跑一遍：LookAndFeel 的 setColour 是把值拷贝进来的，
    // 不会跟着 OzoCol 的变量变。
    void refreshColours()
    {
        setColour (juce::Slider::rotarySliderFillColourId,     OzoCol::accent);
        setColour (juce::Slider::rotarySliderOutlineColourId,  OzoCol::track);
        setColour (juce::Slider::textBoxTextColourId,          OzoCol::text);
        setColour (juce::Slider::textBoxOutlineColourId,       juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId,    juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId,                  OzoCol::text);
        setColour (juce::TextButton::buttonColourId,           OzoCol::btnBg);
        setColour (juce::TextButton::textColourOffId,          OzoCol::textDim);
        setColour (juce::TextButton::textColourOnId,           OzoCol::btnOnText);
        setColour (juce::ToggleButton::textColourId,           OzoCol::textDim);
        setColour (juce::ToggleButton::tickColourId,           OzoCol::accent);
        setColour (juce::ComboBox::backgroundColourId,         OzoCol::btnBg);
        setColour (juce::ComboBox::textColourId,               OzoCol::text);
        setColour (juce::PopupMenu::backgroundColourId,        OzoCol::popupBg);
        setColour (juce::PopupMenu::textColourId,              OzoCol::text);
    }

    //--------------------------------------------------------------------------
    // 旋钮：轨道弧 + 全息值弧 + 白瓷旋钮本体 + 深紫指针
    // 双极参数（Input / Output）的值弧从 12 点起画，一眼看出是增益还是衰减。
    void drawRotarySlider (juce::Graphics& g,
                           int x, int y, int width, int height,
                           float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider& slider) override
    {
        auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre  = bounds.getCentre();
        const float angle  = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        const bool bipolar = slider.getMinimum() < 0.0;
        const float zeroAngle = rotaryStartAngle
                              + (bipolar ? 0.5f : 0.0f) * (rotaryEndAngle - rotaryStartAngle);

        // 这个旋钮的主题色。没有指定（Input / Output / Mix）就用全息渐变。
        const int   theme = (int) slider.getProperties().getWithDefault (OzoCol::propTheme, -1);
        const bool  themed = theme >= 0 && theme < 4;
        const auto& tintCol = OzoCol::tint[themed ? theme : 0];

        // 1. 轨道
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                             rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (themed ? tintCol.withAlpha (0.16f) : OzoCol::track);
        g.strokePath (track, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        // 2. 值弧 —— 先画一层宽的柔和光晕，再画一层实色，
        //    这样在浅底上也能"亮"起来，而不是一条干巴巴的线。
        if (std::abs (sliderPos - (bipolar ? 0.5f : 0.0f)) > 0.0015f)
        {
            const float a0 = juce::jmin (zeroAngle, angle);
            const float a1 = juce::jmax (zeroAngle, angle);

            juce::Path value;
            value.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, a0, a1, true);

            if (themed)
            {
                g.setGradientFill (OzoCol::tintGlow (tintCol, bounds, 0.30f));
                g.strokePath (value, juce::PathStrokeType (9.0f, juce::PathStrokeType::curved,
                                                           juce::PathStrokeType::rounded));

                g.setGradientFill (OzoCol::tintGlow (tintCol, bounds, 1.0f));
                g.strokePath (value, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved,
                                                           juce::PathStrokeType::rounded));
            }
            else
            {
                g.setGradientFill (OzoCol::holo (bounds, 0.28f));
                g.strokePath (value, juce::PathStrokeType (9.0f, juce::PathStrokeType::curved,
                                                           juce::PathStrokeType::rounded));

                g.setGradientFill (OzoCol::holo (bounds, 1.0f));
                g.strokePath (value, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved,
                                                           juce::PathStrokeType::rounded));
            }
        }

        // 3. 旋钮本体：白瓷 + 极淡的粉紫投影，让它从浅底上"浮"起来
        const float knobR = radius * 0.68f;

        g.setColour (OzoCol::knobShadow);
        g.fillEllipse (centre.x - knobR + 1.0f, centre.y - knobR + 2.0f,
                       knobR * 2.0f, knobR * 2.0f);

        g.setGradientFill (juce::ColourGradient::vertical (
            OzoCol::knobFace, OzoCol::knobFace2,
            juce::Rectangle<float> (centre.x - knobR, centre.y - knobR,
                                    knobR * 2.0f, knobR * 2.0f)));
        g.fillEllipse (centre.x - knobR, centre.y - knobR, knobR * 2.0f, knobR * 2.0f);

        g.setColour (OzoCol::knobEdge);
        g.drawEllipse (centre.x - knobR, centre.y - knobR, knobR * 2.0f, knobR * 2.0f, 1.0f);

        // 4. 中心的主题色圆点 —— 不看标签也能一眼认出这个旋钮归哪一色
        if (themed)
        {
            g.setColour (tintCol.withAlpha (0.55f));
            g.fillEllipse (centre.x - radius * 0.10f, centre.y - radius * 0.10f,
                           radius * 0.20f, radius * 0.20f);
        }
        else
        {
            g.setColour (OzoCol::knobEdge);
            g.fillEllipse (centre.x - radius * 0.07f, centre.y - radius * 0.07f,
                           radius * 0.14f, radius * 0.14f);
        }

        // 5. 指针
        const float sinA = std::sin (angle);
        const float cosA = std::cos (angle);
        juce::Line<float> pointer (
            centre.x + sinA * knobR * 0.30f, centre.y - cosA * knobR * 0.30f,
            centre.x + sinA * knobR * 0.82f, centre.y - cosA * knobR * 0.82f);

        g.setColour (themed ? OzoCol::tintText[theme] : OzoCol::text);
        g.drawLine (pointer, 3.0f);
    }

    //--------------------------------------------------------------------------
    // 按钮：预设与 Character。选中态填全息渐变，文字转白。
    void drawButtonBackground (juce::Graphics& g, juce::Button& b,
                               const juce::Colour&,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override
    {
        auto bounds = b.getLocalBounds().toFloat().reduced (0.5f);
        const float corner = 8.0f;

        if (b.getToggleState())
        {
            g.setGradientFill (OzoCol::holo (bounds, shouldDrawButtonAsDown ? 0.75f : 1.0f));
            g.fillRoundedRectangle (bounds, corner);

            // 顶部一道高光，让选中态有"玻璃"感
            g.setColour (OzoCol::btnHi2);
            g.fillRoundedRectangle (bounds.withHeight (bounds.getHeight() * 0.45f), corner);
        }
        else
        {
            g.setColour (shouldDrawButtonAsHighlighted ? OzoCol::btnHi : OzoCol::btnBg);
            g.fillRoundedRectangle (bounds, corner);
            g.setColour (OzoCol::panelEdge);
            g.drawRoundedRectangle (bounds, corner, 1.0f);
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b,
                         bool, bool) override
    {
        const bool on = b.getToggleState();

        g.setColour (on ? OzoCol::btnOnText
                        : (b.isMouseOver() ? OzoCol::text : OzoCol::textDim));
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 0),
                          juce::Justification::centred, 1);
    }

    //--------------------------------------------------------------------------
    // 开关：小胶囊，开时填薄荷→粉的渐变
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b,
                           bool shouldDrawButtonAsHighlighted,
                           bool) override
    {
        const auto bounds = b.getLocalBounds().toFloat();
        const float h = juce::jmin (22.0f, bounds.getHeight());
        const float w = 40.0f;

        auto capsule = juce::Rectangle<float> (bounds.getX(), bounds.getCentreY() - h * 0.5f, w, h);
        const bool on = b.getToggleState();

        if (on)
        {
            g.setGradientFill (OzoCol::holoHorizontal (capsule.getX(), capsule.getRight(), 1.0f));
        }
        else
        {
            g.setColour (shouldDrawButtonAsHighlighted ? OzoCol::btnHi : OzoCol::track);
        }
        g.fillRoundedRectangle (capsule, h * 0.5f);

        const float knobD = h - 5.0f;
        auto knob = juce::Rectangle<float> (on ? capsule.getRight() - 3.0f - knobD
                                               : capsule.getX() + 3.0f,
                                            capsule.getCentreY() - knobD * 0.5f,
                                            knobD, knobD);
        g.setColour (juce::Colour (0xFFFFFFFF));
        g.fillEllipse (knob);

        auto textArea = bounds.withTrimmedLeft (w + 8.0f);
        g.setColour (on ? OzoCol::text : OzoCol::textDim);
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawFittedText (b.getButtonText(), textArea.toNearestInt(),
                          juce::Justification::centredLeft, 1);
    }

    //--------------------------------------------------------------------------
    juce::Font getLabelFont (juce::Label& label) override
    {
        // 尊重 Label 自己设置的字号，只补一个家族，避免和 Editor 里的 setFont 打架
        return juce::Font (juce::FontOptions (label.getFont().getHeight()));
    }
};

} // namespace ozo
