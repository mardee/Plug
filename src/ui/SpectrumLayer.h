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
    void setExpandedMode (bool exp) { if (expanded != exp) { expanded = exp; repaint(); } }
    void setWildMode (bool wild) { if (isWild != wild) { isWild = wild; buildBlobImages(); repaint(); } }

    // fresh = 这一帧是否真的有新数据。宿主停止调用 processBlock 时用它让柱子落下来。
    // peakValues = 分析器算好的峰值保持（可空）。衰减在分析器里做，这里只画。
    void setSpectrum (const float* values, int num, bool fresh,
                      const float* peakValues = nullptr)
    {
        const int n = juce::jmin (num, kNumBins);

        for (int i = 0; i < n; ++i)
        {
            spec[(size_t) i] = values[i];
            peaks[(size_t) i] = peakValues != nullptr
                              ? juce::jmax (values[i], peakValues[i])
                              : values[i];
        }

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

        // ---- 2. 深色狂野模式：Netflix 风格动态彩色纵贯光束与穿梭条纹 ----
        if (isWild)
            drawNetflixLightBeams (g, w, h);

        drawBlobs    (g, w, h);
        drawSweep    (g, w, h);

        // 深色狂野模式下开启背景波形图（频谱发光曲面与峰值线）
        if (isWild)
            drawSpectrum (g, w, h);
    }

private:
    bool expanded = false;
    bool isWild   = false;
    void timerCallback() override
    {
        // 时间推进和峰值衰减放在这里，不放 paint ——
        // paint 应该是只读的，否则每次重绘都会多推进一步，节奏会飘。
        if (! isShowing())
            return;

        t += 1.0f / 30.0f;
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
    // 类似 Netflix 开场特写的贯穿全屏动态彩色纵贯光束与穿梭条纹
    //--------------------------------------------------------------------------
    void drawNetflixLightBeams (juce::Graphics& g, float w, float h)
    {
        const int numBeams = 24;
        for (int i = 0; i < numBeams; ++i)
        {
            // 每根光条具有独立的随机种子相位、频率和宽度
            const float seed = (float) i * 1.6180339f;
            const float xNorm = std::fmod (0.05f + (float) i * 0.042f + std::sin (t * 0.15f + seed) * 0.08f + 1.0f, 1.0f);
            const float bx = xNorm * w;

            // 闪烁生命周期
            const float cycleSpeed = 1.2f + 0.8f * std::sin (seed * 3.7f);
            const float phase = t * cycleSpeed + seed * 2.0f;
            const float rawPulse = 0.5f + 0.5f * std::sin (phase);
            const float flash = std::pow (rawPulse, 4.0f); // 突发闪烁脉冲

            if (flash < 0.02f)
                continue;

            const float beamW = (1.5f + 4.5f * std::sin (seed * 5.1f)) * (1.0f + energy * 1.5f);
            const float alpha = flash * (0.15f + 0.35f * energy);

            // 依据位置和索引赋予全息光谱色
            const float colorPos = std::fmod (xNorm + std::sin (t * 0.3f + seed) * 0.2f + 1.0f, 1.0f);
            const auto beamCol = palette->at (colorPos);

            // 上下贯穿的全屏光柱渐变（中间亮，上下渐隐）
            juce::ColourGradient beamGrad (
                beamCol.withAlpha (0.0f), bx, 0.0f,
                beamCol.withAlpha (0.0f), bx, h, false);
            beamGrad.addColour (0.20, beamCol.withAlpha (alpha * 0.5f));
            beamGrad.addColour (0.50, beamCol.withAlpha (alpha));
            beamGrad.addColour (0.80, beamCol.withAlpha (alpha * 0.5f));

            g.setGradientFill (beamGrad);
            g.fillRect (bx - beamW * 0.5f, 0.0f, beamW, h);

            // 极高能量时的中心炽白细线
            if (flash > 0.65f && energy > 0.15f)
            {
                g.setColour (juce::Colours::white.withAlpha (flash * 0.40f * energy));
                g.fillRect (bx - 0.5f, 0.0f, 1.0f, h);
            }
        }
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

        // 3. 峰值保持线：比曲面更亮更细，落得比实时频谱慢
        juce::Path hold;
        hold.startNewSubPath (0.0f, h - peaks[0] * maxH);
        for (int i = 1; i < kNumBins; ++i)
            hold.lineTo ((float) i / (float) (kNumBins - 1) * w, h - peaks[(size_t) i] * maxH);

        g.setGradientFill (tintGrad (0.85f));
        g.strokePath (hold, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    }

    //--------------------------------------------------------------------------
    // 高保真高斯弥散柔光光斑生成（数学级平滑指数衰减，彻底消除色带与锯齿）
    void buildBlobImages()
    {
        const int size = 256;
        const float half = (float) size * 0.5f;

        for (int i = 0; i < 4; ++i)
        {
            juce::Image img (juce::Image::ARGB, size, size, true);
            juce::Image::BitmapData bm (img, juce::Image::BitmapData::writeOnly);

            const auto c = OzoCol::tint[(size_t) i];

            for (int y = 0; y < size; ++y)
            {
                const float dy = ((float) y - half) / half;
                for (int x = 0; x < size; ++x)
                {
                    const float dx = ((float) x - half) / half;
                    const float distSq = dx * dx + dy * dy;

                    if (distSq < 1.0f)
                    {
                        const float d = std::sqrt (distSq);
                        // 三次 Smoothstep 混合指数高斯衰减
                        const float s = 1.0f - d;
                        const float smooth = s * s * (3.0f - 2.0f * s);
                        const float alpha = smooth * std::exp (-2.4f * d * d) * 0.32f;

                        bm.setPixelColour (x, y, c.withAlpha (alpha));
                    }
                }
            }

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
