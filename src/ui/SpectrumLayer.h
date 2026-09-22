#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <cmath>

#include "OzoLookAndFeel.h"
#include "TintPalette.h"

namespace ozo
{

//==============================================================================
// 频谱背景层 —— 垫在面板**底下**的最底层
//
// 四层自下而上：底色渐变 → 粉彩光晕 → 镭射扫光 → 频谱面（连成片的发光曲面）
//
// 为什么必须独立成一个组件：JUCE 的子组件永远画在父组件之上，
// 而面板是 Editor::paint 画出来的。如果把频谱画在 Editor::paint 里，
// 就得跟着面板一起每帧全屏重绘；做成子组件则可以只重绘自己这一层。
//
// 30 fps、不吃鼠标事件、窗口不可见时停画 —— 它是装饰，不该抢音频线程的余量。
class SpectrumLayer : public juce::Component,
                      private juce::Timer
{
public:
    static constexpr int kNumBins = 80;

    SpectrumLayer()
    {
        setInterceptsMouseClicks (false, false);
        buildBlobImages();
        startTimerHz (30);
    }

    void setPalette (const TintPalette* p) noexcept { palette = p; }

    // fresh = 这一帧是否真的有新数据。宿主停止调用 processBlock 时用它让柱子落下来。
    void setSpectrum (const float* values, int num, bool fresh)
    {
        const int n = juce::jmin (num, kNumBins);

        for (int i = 0; i < n; ++i)
            spec[(size_t) i] = values[i];

        if (! fresh)
            for (auto& v : spec)
                v *= 0.94f;
    }

    void setEnergy (float e)
    {
        energy += (juce::jlimit (0.0f, 1.0f, e) - energy) * 0.25f;
    }

    void paint (juce::Graphics& g) override
    {
        const float w = (float) getWidth();
        const float h = (float) getHeight();

        if (w <= 0.0f || h <= 0.0f)
            return;

        // ---- 1. 底色 ----
        g.setGradientFill (juce::ColourGradient::vertical (
            OzoCol::bgTop, OzoCol::bg, getLocalBounds().toFloat()));
        g.fillAll();

        if (palette == nullptr)
            return;

        drawBlobs    (g, w, h);
        drawSweep    (g, w, h);
        drawSpectrum (g, w, h);
    }

private:
    void timerCallback() override
    {
        // 时间推进和峰值衰减放在这里，不放 paint ——
        // paint 应该是只读的，否则每次重绘都会多推进一步，节奏会飘。
        if (! isShowing())
            return;

        t += 1.0f / 30.0f;

        for (auto& p : peaks)
            p *= 0.955f;

        repaint();
    }

    //--------------------------------------------------------------------------
    void drawBlobs (juce::Graphics& g, float w, float h)
    {
        for (int i = 0; i < 4; ++i)
        {
            const auto& b = blobs[(size_t) i];

            const float cx = w * (0.5f + 0.40f * std::sin (t * b.fx + b.px));
            const float cy = h * (0.5f + 0.38f * std::cos (t * b.fy + b.py));
            const float r  = std::max (w, h) * b.r * (0.94f + 0.10f * energy);

            g.setOpacity (1.0f);
            g.drawImage (blobImg[(size_t) i],
                         juce::Rectangle<float> (cx - r, cy - r, r * 2.0f, r * 2.0f));
        }

        g.setOpacity (1.0f);
    }

    void drawSweep (juce::Graphics& g, float w, float h)
    {
        const float band = w * 0.60f;
        const float x0   = std::sin (t * 0.22f) * (w * 0.55f) - band * 0.5f;

        juce::ColourGradient sweep (palette->at (0.0f).withAlpha (0.0f),  x0,        0.0f,
                                    palette->at (1.0f).withAlpha (0.0f),  x0 + band, h,   false);
        sweep.addColour (0.28, palette->at (0.34f).withAlpha (0.13f));
        sweep.addColour (0.52, palette->at (0.62f).withAlpha (0.11f));
        sweep.addColour (0.76, palette->at (0.85f).withAlpha (0.08f));

        g.setGradientFill (sweep);
        g.fillRect (getLocalBounds());
    }

    //--------------------------------------------------------------------------
    // 频谱画成连成一片的发光曲面，而不是一根根柱子。
    // 整片用横轴四色炫彩渐变铺满，顶部一条连续亮线收边，底部竖向压淡融进背景。
    void drawSpectrum (juce::Graphics& g, float w, float h)
    {
        const float maxH = h * 0.72f;

        // 平滑包络：相邻格做加权，让曲线连成片而不是锯齿
        std::array<float, kNumBins> env {};
        for (int i = 0; i < kNumBins; ++i)
        {
            float v = spec[(size_t) i];
            if (i > 0 && i < kNumBins - 1)
                v = 0.20f * spec[(size_t) (i - 1)] + 0.60f * v
                  + 0.20f * spec[(size_t) (i + 1)];
            env[(size_t) i] = v;
        }

        // 横轴四色炫彩渐变（四色沿宽度铺开，alpha 由调用处指定）
        auto tintGrad = [&] (float a) -> juce::ColourGradient
        {
            juce::ColourGradient grad (palette->at (0.0f).withAlpha (a),  0.0f, 0.0f,
                                       palette->at (1.0f).withAlpha (a),  w,    0.0f, false);
            grad.addColour (0.33f, palette->at (0.33f).withAlpha (a));
            grad.addColour (0.66f, palette->at (0.66f).withAlpha (a));
            grad.addColour (1.0f,  palette->at (1.0f).withAlpha (a));
            return grad;
        };

        // 1. 连成片的发光曲面 —— 整片用横轴四色炫彩渐变铺满
        juce::Path area;
        area.startNewSubPath (0.0f, h);
        for (int i = 0; i < kNumBins; ++i)
            area.lineTo ((float) i / (float) (kNumBins - 1) * w, h - env[(size_t) i] * maxH);
        area.lineTo (w, h);
        area.closeSubPath();

        // 主体填充：四色沿宽度铺开。alpha 拉到 0.62 让颜色真正显出来——
        // 之前只有 0.32，在浅底上被洗成灰白，所以"面积发灰、只有线炫彩"。
        // 不再用 bg 色做底部压淡（那会把下半截糊成灰底），整片保持炫彩。
        g.setGradientFill (tintGrad (0.62f));
        g.fillPath (area);

        // 2. 顶边亮线：连成一条连续曲线（数字 EQ 的灵魂），先粗后细做发光
        juce::Path cap;
        cap.startNewSubPath (0.0f, h - env[0] * maxH);
        for (int i = 1; i < kNumBins; ++i)
            cap.lineTo ((float) i / (float) (kNumBins - 1) * w, h - env[(size_t) i] * maxH);

        g.setGradientFill (tintGrad (0.45f));
        g.strokePath (cap, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

        g.setGradientFill (tintGrad (0.95f));
        g.strokePath (cap, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }

    //--------------------------------------------------------------------------
    void buildBlobImages()
    {
        for (int i = 0; i < 4; ++i)
        {
            juce::Image img (juce::Image::ARGB, 128, 128, true);
            juce::Graphics ig (img);

            const auto c = OzoCol::tint[(size_t) i];

            // 外圈必须用"同色 + alpha 0"，不能用 transparentBlack，
            // 否则混合中途会发灰（JUCE 文档里专门警告过这一点）。
            juce::ColourGradient grad (c.withAlpha (0.22f), 64.0f, 64.0f,
                                       c.withAlpha (0.0f),  128.0f, 64.0f, true);
            ig.setGradientFill (grad);
            ig.fillEllipse (0.0f, 0.0f, 128.0f, 128.0f);

            blobImg[(size_t) i] = std::move (img);
        }
    }

    //--------------------------------------------------------------------------
    struct Blob
    {
        float fx = 0.0f, fy = 0.0f, px = 0.0f, py = 0.0f, r = 0.5f;
    };

    const Blob blobs[4] =
    {
        { 0.055f, 0.041f, 0.0f, 1.1f, 0.46f },
        { 0.043f, 0.062f, 2.2f, 0.3f, 0.52f },
        { 0.061f, 0.037f, 4.1f, 2.7f, 0.40f },
        { 0.035f, 0.052f, 1.4f, 4.0f, 0.44f }
    };

    const TintPalette* palette = nullptr;

    std::array<juce::Image, 4>  blobImg;
    std::array<float, kNumBins> spec  {};
    std::array<float, kNumBins> peaks {};

    float energy = 0.0f;
    float t = 0.0f;
};

} // namespace ozo
