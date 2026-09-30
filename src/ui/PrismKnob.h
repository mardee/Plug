#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <vector>
#include <array>
#include "OzoLookAndFeel.h"

namespace ozo
{

//==============================================================================
// PrismKnob (PRISM ORB) —— 全画幅未来主义全息动态光球与环形波形光冠
//
// 核心视效体系：
// 1. 无生硬蓝色边缘：边缘为无硬边等离子自然光辉
// 2. 宏大四周放射线芒 (Radial Spikes / Rays)：长短交错，全方位向外辐射
// 3. 内部大尺寸多彩动态旋转全息椭圆：85%~95% 饱满直径，四色全息渐变交错流动
// 4. 360° 环形音频波浪光冠 + 悬浮极简 HUD + 丝滑交互
//==============================================================================
class PrismKnob : public juce::Component,
                  private juce::Timer
{
public:
    static constexpr int kSpectrumBins = 80;

    PrismKnob()
    {
        setInterceptsMouseClicks (true, false);
        setRepaintsOnMouseActivity (true);

        buildTextures();

        specSmooth.fill (0.0f);
        specPeaks.fill (0.0f);

        startTimerHz (60);
    }

    ~PrismKnob() override
    {
        stopTimer();
    }

    std::function<void(float)> onValueChanged;
    std::function<void()>      onDoubleClicked;

    void setValue (float v, juce::NotificationType notify = juce::dontSendNotification)
    {
        const float clamped = juce::jlimit (0.0f, 1.0f, v);
        if (std::abs (targetValue - clamped) > 0.0001f)
        {
            targetValue = clamped;
            if (notify != juce::dontSendNotification && onValueChanged)
                onValueChanged (targetValue);
            repaint();
        }
    }

    float getValue() const noexcept { return targetValue; }

    void setWildMode (bool wild)
    {
        if (isWild != wild)
        {
            isWild = wild;
            repaint();
        }
    }

    //--------------------------------------------------------------------------
    // 音频频谱注入接口
    //--------------------------------------------------------------------------
    void setSpectrum (const float* bins, int numBins, float audioEnergy)
    {
        const int n = juce::jmin (numBins, kSpectrumBins);
        for (int i = 0; i < n; ++i)
        {
            const float target = bins[i];
            const float cur = specSmooth[(size_t) i];
            const float co = target > cur ? 0.50f : 0.15f;
            specSmooth[(size_t) i] = cur + (target - cur) * co;

            if (specSmooth[(size_t) i] > specPeaks[(size_t) i])
                specPeaks[(size_t) i] = specSmooth[(size_t) i];
            else
                specPeaks[(size_t) i] *= 0.94f;
        }

        const float targetE = juce::jlimit (0.0f, 1.0f, audioEnergy);
        const float eCoeff = targetE > energy ? 0.55f : 0.20f;
        energy += (targetE - energy) * eCoeff;
    }

    //--------------------------------------------------------------------------
    // 鼠标交互
    //--------------------------------------------------------------------------
    void mouseDown (const juce::MouseEvent& e) override
    {
        dragStartValue = targetValue;
        dragStartPos = e.position;
        isDragging = true;
        ripplePhase = 0.0f;
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! isDragging)
            return;

        const float deltaY = (dragStartPos.y - e.position.y);
        const float deltaX = (e.position.x - dragStartPos.x);
        const float sensitivity = 0.0040f;
        const float delta = (deltaY + deltaX * 0.35f) * sensitivity;
        
        setValue (juce::jlimit (0.0f, 1.0f, dragStartValue + delta), juce::sendNotification);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        isDragging = false;
        repaint();
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        const float delta = wheel.deltaY * 0.12f;
        setValue (juce::jlimit (0.0f, 1.0f, targetValue + delta), juce::sendNotification);
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (onDoubleClicked)
            onDoubleClicked();
    }

    void mouseEnter (const juce::MouseEvent&) override
    {
        isHovered = true;
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        isHovered = false;
    }

    //--------------------------------------------------------------------------
    // 动画定时器
    //--------------------------------------------------------------------------
    void timerCallback() override
    {
        animTime += 0.016f;
        if (animTime > 10000.0f)
            animTime = 0.0f;

        smoothValue += (targetValue - smoothValue) * 0.20f;

        const float targetHover = (isHovered || isDragging) ? 1.0f : 0.0f;
        hoverAlpha += (targetHover - hoverAlpha) * 0.15f;

        if (ripplePhase >= 0.0f && ripplePhase < 1.0f)
            ripplePhase += 0.026f;

        repaint();
    }

    //--------------------------------------------------------------------------
    // 渲染绘制
    //--------------------------------------------------------------------------
    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY() + 6.0f;

        const float val = smoothValue;
        const bool isMax = val >= 0.99f;

        // 1. 动态球体基础半径（70px -> 135px 随百分比扩展，随响度音频包络明显扩张）
        const float baseRadius = 70.0f + 65.0f * std::pow (val, 0.85f);
        const float r = baseRadius + energy * 16.0f;

        // 2. 动态色彩管道
        const auto cMain = getSpectrumColor (val, 0.0f);
        const auto cSecond = getSpectrumColor (val, 0.35f);

        // 3. 全画幅环形全息日冕漫反射（外缘灵动向外呼吸，随音频响度忽明忽暗）
        drawAtmosphericBloom (g, cx, cy, r, val, cMain, isMax);

        // 4. 冲击波扩散环（交互触发）
        if (ripplePhase > 0.0f && ripplePhase < 1.0f)
        {
            const float ripR = r * (1.0f + ripplePhase * 2.5f);
            const float ripAlpha = (1.0f - ripplePhase) * (isWild ? 0.60f : 0.35f);
            g.setColour (cMain.withAlpha (ripAlpha));
            g.drawEllipse (cx - ripR, cy - ripR, ripR * 2.0f, ripR * 2.0f, 2.0f * (1.0f - ripplePhase * 0.5f));
        }

        // 5. 3D 水晶等离子能量球（随响度音频包络动态呼吸亮灭）
        drawCrystalPlasmaSphere (g, cx, cy, r, val, isMax);
    }

private:
    std::array<float, kSpectrumBins> specSmooth {};
    std::array<float, kSpectrumBins> specPeaks  {};
    float energy = 0.0f;
    bool  isWild = false;

    juce::Image whiteGlowImg;
    juce::Image pinkGlowImg;
    juce::Image lavenderGlowImg;
    juce::Image mintGlowImg;
    juce::Image peachGlowImg;

    juce::Image pinkRingImg;
    juce::Image lavenderRingImg;
    juce::Image mintRingImg;
    juce::Image peachRingImg;

    // 狂野暗色模式专用全息色散光斑与环光（兰花紫、珊瑚绯红、珍珠冰蓝、天鹅绒深红）
    juce::Image wildOrchidGlowImg;
    juce::Image wildCoralGlowImg;
    juce::Image wildPearlGlowImg;
    juce::Image wildPlumGlowImg;

    juce::Image wildOrchidRingImg;
    juce::Image wildCoralRingImg;
    juce::Image wildPearlRingImg;
    juce::Image wildPlumRingImg;

    //==============================================================================
    // 纹理材质生成
    //==============================================================================
    juce::Image createColorBloom (juce::Colour col, int size = 256)
    {
        juce::Image img (juce::Image::ARGB, size, size, true);
        const float half = (float) size * 0.5f;
        juce::Image::BitmapData bm (img, juce::Image::BitmapData::writeOnly);
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
                    const float s = 1.0f - d;
                    const float smooth = s * s * (3.0f - 2.0f * s);
                    const float alpha = smooth * std::exp (-2.4f * d * d);
                    bm.setPixelColour (x, y, col.withAlpha (alpha));
                }
            }
        }
        return img;
    }

    // 环形全息色散光晕：中心严格为 0（彻底消除死白/浓色硬斑），在 0.65~0.75 半径处达到峰值
    juce::Image createRingBloom (juce::Colour col, float peakRadius = 0.70f, int size = 256)
    {
        juce::Image img (juce::Image::ARGB, size, size, true);
        const float half = (float) size * 0.5f;
        juce::Image::BitmapData bm (img, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < size; ++y)
        {
            const float dy = ((float) y - half) / half;
            for (int x = 0; x < size; ++x)
            {
                const float dx = ((float) x - half) / half;
                const float dist = std::sqrt (dx * dx + dy * dy);
                if (dist < 1.0f)
                {
                    float ringAlpha = 0.0f;
                    if (dist < peakRadius)
                    {
                        const float norm = dist / peakRadius;
                        ringAlpha = norm * norm * (3.0f - 2.0f * norm);
                    }
                    else
                    {
                        const float norm = (1.0f - dist) / (1.0f - peakRadius);
                        ringAlpha = norm * norm * (3.0f - 2.0f * norm);
                    }
                    bm.setPixelColour (x, y, col.withAlpha (juce::jlimit (0.0f, 1.0f, ringAlpha)));
                }
            }
        }
        return img;
    }

    void buildTextures()
    {
        whiteGlowImg    = createColorBloom (juce::Colour (0xFFF5F5FA), 256);

        // 亮色模式：通透柔和的马卡龙粉彩光环与微色散
        pinkRingImg     = createRingBloom (juce::Colour (0xFFE8BCCE), 0.70f, 256);
        lavenderRingImg = createRingBloom (juce::Colour (0xFFCEBFF0), 0.65f, 256);
        mintRingImg     = createRingBloom (juce::Colour (0xFFB6E6DC), 0.72f, 256);
        peachRingImg    = createRingBloom (juce::Colour (0xFFF2D6C2), 0.68f, 256);

        pinkGlowImg     = createColorBloom (juce::Colour (0xFFE8BCCE), 256);
        lavenderGlowImg = createColorBloom (juce::Colour (0xFFCEBFF0), 256);
        mintGlowImg     = createColorBloom (juce::Colour (0xFFB6E6DC), 256);
        peachGlowImg    = createColorBloom (juce::Colour (0xFFF2D6C2), 256);

        // 狂野暗色模式：高能霓虹色散（耀眼亮紫、高能炽红/珊瑚绯红、珍珠冷白冰蓝、高贵深红宝石）
        wildOrchidRingImg = createRingBloom (juce::Colour (0xFFD62BFF), 0.70f, 256);
        wildCoralRingImg  = createRingBloom (juce::Colour (0xFFFF2655), 0.65f, 256);
        wildPearlRingImg  = createRingBloom (juce::Colour (0xFFDBE6F9), 0.72f, 256);
        wildPlumRingImg   = createRingBloom (juce::Colour (0xFF5A1028), 0.68f, 256);

        wildOrchidGlowImg = createColorBloom (juce::Colour (0xFFD62BFF), 256);
        wildCoralGlowImg  = createColorBloom (juce::Colour (0xFFFF2655), 256);
        wildPearlGlowImg  = createColorBloom (juce::Colour (0xFFDBE6F9), 256);
        wildPlumGlowImg   = createColorBloom (juce::Colour (0xFF5A1028), 256);
    }

    //==============================================================================
    // 全息色彩流动
    //==============================================================================
    juce::Colour getSpectrumColor (float v, float offset) const
    {
        if (isWild)
        {
            if (v >= 0.99f)
                return juce::Colour (0xFFFF2655);

            const float t = v + offset + std::sin (animTime * 1.2f) * 0.08f;
            const float wrapped = std::fmod (std::abs (t), 1.0f);

            if (wrapped < 0.40f)
                return juce::Colour (0xFFFF2655); // 鲜亮炽红
            else if (wrapped < 0.70f)
                return juce::Colour (0xFFD62BFF); // 耀眼亮紫
            else
                return juce::Colour (0xFFFF4D75); // 霓虹绯红
        }

        // 亮色模式：饱和度随百分比推进 (0% -> 100%) 从极浅珍珠粉彩平滑增加到鲜亮全息色
        const float sat = juce::jmap (v, 0.18f, 0.68f);
        const float bri = juce::jmap (v, 0.98f, 0.88f);

        const float t = v + offset + std::sin (animTime * 0.8f) * 0.05f;
        const float hue = std::fmod (std::abs (t * 0.85f + 0.70f), 1.0f);

        return juce::Colour::fromHSV (hue, sat, bri, 1.0f);
    }

    const juce::Image& getGlowImage (int idx) const noexcept
    {
        if (isWild)
        {
            switch (idx % 4)
            {
                case 0: return wildOrchidGlowImg;
                case 1: return wildCoralGlowImg;
                case 2: return wildPearlGlowImg;
                default: return wildPlumGlowImg;
            }
        }
        switch (idx % 4)
        {
            case 0: return pinkGlowImg;
            case 1: return mintGlowImg;
            case 2: return lavenderGlowImg;
            default: return peachGlowImg;
        }
    }

    const juce::Image& getRingImage (int idx) const noexcept
    {
        if (isWild)
        {
            switch (idx % 4)
            {
                case 0: return wildOrchidRingImg;
                case 1: return wildCoralRingImg;
                case 2: return wildPearlRingImg;
                default: return wildPlumRingImg;
            }
        }
        switch (idx % 4)
        {
            case 0: return pinkRingImg;
            case 1: return mintRingImg;
            case 2: return lavenderRingImg;
            default: return peachRingImg;
        }
    }

    //==============================================================================
    // 3. 全画幅环形全息日冕漫反射（随音频响度动态忽明忽暗向外漫射律动）
    //==============================================================================
    void drawAtmosphericBloom (juce::Graphics& g, float cx, float cy, float r,
                              float val, juce::Colour c, bool isMax)
    {
        const float breath = 1.0f + 0.035f * std::sin (animTime * 2.6f + val * 3.5f);
        const float audioPulse = energy * 1.40f;
        const float washAlpha = (0.28f + val * 0.38f + hoverAlpha * 0.12f + audioPulse * 0.80f) * (isMax ? 1.35f : 1.0f);

        // 外层大日冕发光环（随响度忽明忽暗、向外伸展：兰花紫幻光）
        const float outerCoronaR = r * (1.60f + val * 0.65f + audioPulse * 0.60f) * breath;
        g.setOpacity (isWild ? juce::jmin (0.85f, washAlpha * 0.85f) : juce::jmin (0.60f, washAlpha * 0.70f));
        g.drawImage (isWild ? wildOrchidRingImg : lavenderRingImg,
                     juce::Rectangle<float> (cx - outerCoronaR, cy - outerCoronaR, outerCoronaR * 2.0f, outerCoronaR * 2.0f));

        // 中层柔光光环（珊瑚红霞 / 蜜桃粉彩）
        const float midCoronaR = r * (1.28f + val * 0.38f + audioPulse * 0.40f);
        g.setOpacity (isWild ? washAlpha * 0.75f : washAlpha * 0.55f);
        g.drawImage (isWild ? wildCoralRingImg : pinkRingImg,
                     juce::Rectangle<float> (cx - midCoronaR, cy - midCoronaR, midCoronaR * 2.0f, midCoronaR * 2.0f));

        // 内层贴边微光（珍珠冷白冰蓝 / 薄荷）
        const float inCoronaR = r * (1.08f + val * 0.18f + audioPulse * 0.25f);
        g.setOpacity (isWild ? washAlpha * 0.70f : washAlpha * 0.50f);
        g.drawImage (isWild ? wildPearlRingImg : mintRingImg,
                     juce::Rectangle<float> (cx - inCoronaR, cy - inCoronaR, inCoronaR * 2.0f, inCoronaR * 2.0f));

        g.setOpacity (1.0f);
    }

    //==============================================================================
    // 生成 360° 环形动态波形路径 (Waveform Path)
    // 浅色模式为 4 波形顺时针循环与三次样条圆润过渡；狂野暗色模式在有音频时动态迸发失真毛刺
    //==============================================================================
    juce::Path createWavePath (float cx, float cy, float r, float val) const
    {
        constexpr int numPoints = 128;
        const float baseWave = 0.6f + val * 0.8f;
        // 适度缩减波形对边缘的影响（6~18px），保持光球主体流体张力
        const float dynamicWave = 6.0f + val * 7.0f + energy * 8.0f + (isWild ? 3.0f : 0.0f);

        std::array<juce::Point<float>, numPoints> pts;

        // 顺时针温和流转推进相位（降低转速，优雅巡游）
        const float rotPhase = animTime * 0.55f;
        const float audioGritFactor = isWild ? juce::jlimit (0.0f, 1.0f, (energy - 0.015f) * 3.5f) : 0.0f;

        for (int i = 0; i < numPoints; ++i)
        {
            const float normAngle = (float) i / (float) numPoints;
            const float angle = normAngle * 6.2831853f - 1.5707963f;

            // 4 波形顺时针循环：将 360° 等分为 4 个象限（每 90° 一组波形），同向顺时针推进
            const float phase4 = normAngle * 4.0f - rotPhase;
            const float norm4 = std::fmod (std::fmod (phase4, 1.0f) + 1.0f, 1.0f); // 0.0 -> 1.0 顺时针单向循环

            // 顺时针流体波形不对称包络：波头平缓抬升，波尾平滑收拢（打破镜像轴对称）
            const float envelope = std::sin (norm4 * 3.14159265f);
            const float binPos = norm4 * (float) (kSpectrumBins - 1);

            const int b0 = (int) binPos;
            const int b1 = juce::jmin (kSpectrumBins - 1, b0 + 1);
            const float frac = binPos - (float) b0;
            const float rawMag = specSmooth[(size_t) b0] * (1.0f - frac) + specSmooth[(size_t) b1] * frac;
            const float mag = rawMag * envelope;

            // 4 倍频顺时针行波呼吸（温和转速，严格保持 4-fold 旋转中心对称）
            const float w1 = std::sin (angle * 4.0f - animTime * 1.0f) * 0.70f;
            const float w2 = std::sin (angle * 8.0f - animTime * 1.8f) * 0.30f;
            const float idleWave = (w1 + w2) * baseWave;

            // 狂野暗色模式：失真毛刺跟随响度与局部频谱动态有机扰动（非机械生硬，随音频信号自适应爆发）
            float gritOffset = 0.0f;
            if (audioGritFactor > 0.001f)
            {
                // 1. 局部频谱与多频段混沌抖动合成（随音频能量与频段幅度动态爆发）
                const float s1 = std::sin (angle * 21.0f + animTime * 6.0f + mag * 15.0f);
                const float s2 = std::sin (angle * 39.0f - animTime * 9.5f + rawMag * 10.0f);
                const float s3 = std::cos (angle * 57.0f + animTime * 13.0f + (float) (i % 5));
                const float chaos = s1 * 0.50f + s2 * 0.35f + s3 * 0.25f;

                // 尖刺强度跟随该位置的频谱能量与全局响度
                const float locDrive = (mag * 0.70f + energy * 0.60f) * audioGritFactor;
                const float spikePow = std::pow (std::abs (chaos), 3.0f) * (4.2f * locDrive);

                // 2. 模拟电路失真微波形折叠（Wavefolding）毛刺
                const float fold = (std::abs (std::sin (angle * 13.0f - animTime * 3.0f + mag * 8.0f)) - 0.45f) * (2.2f * locDrive);

                gritOffset = (spikePow + fold) * (0.75f + 0.25f * std::sin (animTime * 10.0f + (float) i * 0.5f));
            }

            const float curR = r + idleWave + mag * dynamicWave + gritOffset;
            pts[(size_t) i] = { cx + curR * std::cos (angle), cy + curR * std::sin (angle) };
        }

        // 使用闭合三次贝塞尔样条曲线，在狂野有音频时采用紧致切线表现锐利毛刺弹性，无音频/浅色时柔滑圆润
        juce::Path p;
        p.startNewSubPath (pts[0]);
        for (int i = 0; i < numPoints; ++i)
        {
            const auto pPrev  = pts[(size_t) ((i - 1 + numPoints) % numPoints)];
            const auto pCurr  = pts[(size_t) i];
            const auto pNext  = pts[(size_t) ((i + 1) % numPoints)];
            const auto pNext2 = pts[(size_t) ((i + 2) % numPoints)];

            const float tension = (isWild && audioGritFactor > 0.08f) ? (1.0f / 10.0f) : (1.0f / 6.0f);
            const float c1x = pCurr.x + (pNext.x - pPrev.x) * tension;
            const float c1y = pCurr.y + (pNext.y - pPrev.y) * tension;
            const float c2x = pNext.x - (pNext2.x - pCurr.x) * tension;
            const float c2y = pNext.y - (pNext2.y - pCurr.y) * tension;

            p.cubicTo (c1x, c1y, c2x, c2y, pNext.x, pNext.y);
        }
        p.closeSubPath();

        return p;
    }

    //==============================================================================
    // 7. 3D 水晶等离子能量光球（亮色温润全息粉彩 / 暗色深邃赛博炽光，随响度忽明忽暗）
    //==============================================================================
    void drawCrystalPlasmaSphere (juce::Graphics& g, float cx, float cy, float r,
                                 float val, bool isMax)
    {
        const juce::Path waveCore = createWavePath (cx, cy, r, val);

        const float lx = cx - r * 0.32f;
        const float ly = cy - r * 0.35f;
        const float audioGlow = energy * 1.05f;

        // A. 球体底板：3D 径向体积底板（深邃红宝石/霓虹亮紫微晕 + 下半部液态全息色散层，通透灵动）
        if (isWild)
        {
            // 1. 基础体积底板：左上通透夜幕洋红亮紫 -> 右侧全息冰晶 -> 底部深红宝石（通透流光，绝无纯黑死色）
            const auto colTopLeft = juce::Colour::fromFloatRGBA (
                juce::jlimit (0.0f, 1.0f, 0.28f + audioGlow * 0.18f),
                juce::jlimit (0.0f, 1.0f, 0.08f + audioGlow * 0.08f),
                juce::jlimit (0.0f, 1.0f, 0.26f + audioGlow * 0.16f), 1.0f);

            const auto colCenter = juce::Colour::fromFloatRGBA (
                juce::jlimit (0.0f, 1.0f, 0.38f + audioGlow * 0.22f),
                juce::jlimit (0.0f, 1.0f, 0.10f + audioGlow * 0.10f),
                juce::jlimit (0.0f, 1.0f, 0.28f + audioGlow * 0.18f), 1.0f);

            const auto colBottom = juce::Colour::fromFloatRGBA (
                juce::jlimit (0.0f, 1.0f, 0.56f + audioGlow * 0.28f),
                juce::jlimit (0.0f, 1.0f, 0.12f + audioGlow * 0.12f),
                juce::jlimit (0.0f, 1.0f, 0.24f + audioGlow * 0.15f), 1.0f);

            juce::ColourGradient darkGrad (colTopLeft, lx, ly, colBottom, cx, cy + r * 0.85f, false);
            darkGrad.addColour (0.40, colCenter);
            darkGrad.addColour (0.75, juce::Colour (0xFF651238));
            darkGrad.addColour (0.90, juce::Colour (0xFF4A0A26));
            g.setGradientFill (darkGrad);
            g.fillPath (waveCore);

            // 2. 下半部专属液态全息色散漫层（珍珠冰蓝 -> 耀眼亮紫 -> 鲜亮炽红 -> 深红宝石酒韵）
            {
                juce::Graphics::ScopedSaveState sDisp (g);
                g.reduceClipRegion (waveCore);

                juce::ColourGradient dispGrad (
                    juce::Colour (0x00000000), cx, cy - r * 0.05f,
                    juce::Colour (0xF05A122C), cx, cy + r * 0.95f, false);
                dispGrad.addColour (0.30, juce::Colour (0x66DBE6F9)); // 珍珠冰蓝微光
                dispGrad.addColour (0.58, juce::Colour (0xBBD62BFF)); // 耀眼亮紫
                dispGrad.addColour (0.80, juce::Colour (0xEEFF2655)); // 鲜亮炽红
                dispGrad.addColour (0.95, juce::Colour (0xEE62102C)); // 高贵深红宝石
                g.setGradientFill (dispGrad);
                g.fillRect (juce::Rectangle<float> (cx - r, cy - r * 0.05f, r * 2.0f, r * 1.10f));
            }
        }
        else
        {
            // 亮色模式：温润立体马卡龙粉彩光球（色彩饱和度随百分比增大而平滑增加，随响度明亮律动）
            const float s = juce::jlimit (0.0f, 1.0f, val);

            const auto colTopLeft = juce::Colour::fromFloatRGBA (
                juce::jlimit (0.0f, 1.0f, juce::jmap (s, 0.93f, 0.78f) + audioGlow * 0.12f),
                juce::jlimit (0.0f, 1.0f, juce::jmap (s, 0.88f, 0.65f) + audioGlow * 0.12f),
                juce::jlimit (0.0f, 1.0f, juce::jmap (s, 0.98f, 0.98f) + audioGlow * 0.05f), 1.0f);

            const auto colCenter = juce::Colour::fromFloatRGBA (
                juce::jlimit (0.0f, 1.0f, juce::jmap (s, 0.88f, 0.60f) + audioGlow * 0.14f),
                juce::jlimit (0.0f, 1.0f, juce::jmap (s, 0.95f, 0.92f) + audioGlow * 0.08f),
                juce::jlimit (0.0f, 1.0f, juce::jmap (s, 0.92f, 0.84f) + audioGlow * 0.12f), 1.0f);

            // 调淡右下角高光：降低突兀的粉白偏色，使右下角与中心色调柔和过渡
            const auto colBottomRight = juce::Colour::fromFloatRGBA (
                juce::jlimit (0.0f, 1.0f, juce::jmap (s, 0.89f, 0.68f) + audioGlow * 0.10f),
                juce::jlimit (0.0f, 1.0f, juce::jmap (s, 0.86f, 0.64f) + audioGlow * 0.10f),
                juce::jlimit (0.0f, 1.0f, juce::jmap (s, 0.94f, 0.82f) + audioGlow * 0.10f), 1.0f);

            juce::ColourGradient holoGrad (
                colTopLeft, lx, ly,
                colBottomRight, cx + r * 0.55f, cy + r * 0.55f, true);
            holoGrad.addColour (0.45, colCenter);
            g.setGradientFill (holoGrad);
            g.fillPath (waveCore);
        }

        // B. 球体内部：有机全息流体与动态等离子光波
        {
            juce::Graphics::ScopedSaveState sss (g);
            g.reduceClipRegion (waveCore);

            const float plasmaSpeed = 0.35f + val * 1.2f + energy * 2.8f + (isWild ? 0.35f : 0.0f);
            const float t = animTime * plasmaSpeed;

            // 1. 内部环形全息色散光环（随响度增亮）
            {
                const float ringAlpha = (isWild ? (0.45f + val * 0.35f) : (0.24f + val * 0.20f)) * (1.0f + energy * 1.15f);

                g.setOpacity (juce::jmin (1.0f, ringAlpha));
                g.drawImage (getRingImage (0), juce::Rectangle<float> (cx - r, cy - r, r * 2.0f, r * 2.0f));

                g.setOpacity (juce::jmin (1.0f, ringAlpha * 0.80f));
                const float inR = r * 0.85f;
                g.drawImage (getRingImage (1), juce::Rectangle<float> (cx - inR, cy - inR, inR * 2.0f, inR * 2.0f));
            }

            // 2. 狂野模式高能全息等离子闪电（分支分形电弧 + 逐段全息流动渐变 + 6层高斯高光光晕 + 纳米放电火花）
            if (isWild && energy > 0.02f)
            {
                const int numArcs = 3;
                for (int a = 0; a < numArcs; ++a)
                {
                    // 起点位于圆周边缘一侧 (0.88r 处)
                    const float ang1 = animTime * 0.35f + (float) a * (6.2831853f / (float) numArcs) + std::sin (animTime * 1.5f + (float) a) * 0.25f;
                    // 终点贯穿到对侧附近（约 150° ~ 210° 范围对向跨越）
                    const float ang2 = ang1 + 2.7f + std::sin (animTime * 1.9f + (float) a * 1.4f) * 0.50f;

                    const float arcR1 = r * 0.86f;
                    const float arcR2 = r * 0.86f;

                    const float p1x = cx + arcR1 * std::cos (ang1);
                    const float p1y = cy + arcR1 * std::sin (ang1);
                    const float p2x = cx + arcR2 * std::cos (ang2);
                    const float p2y = cy + arcR2 * std::sin (ang2);

                    const float dx = p2x - p1x;
                    const float dy = p2y - p1y;
                    const float chordLen = std::sqrt (dx * dx + dy * dy);
                    const float nx = -dy / (chordLen > 0.001f ? chordLen : 1.0f);
                    const float ny =  dx / (chordLen > 0.001f ? chordLen : 1.0f);

                    const int numSegments = 16;
                    std::vector<juce::Point<float>> arcPoints;
                    arcPoints.reserve (numSegments + 1);
                    arcPoints.push_back ({ p1x, p1y });

                    for (int seg = 1; seg < numSegments; ++seg)
                    {
                        const float frac = (float) seg / (float) numSegments;
                        const float baseX = p1x + dx * frac;
                        const float baseY = p1y + dy * frac;

                        // 依据三次正弦包络与多频高斯分形扰动，呈现极其逼真的真实电弧蜿蜒（绝非生硬面条线）
                        const float env = std::sin (frac * 3.14159265f);
                        const float f1 = std::sin (animTime * 22.0f + (float) (seg * 5 + a * 11));
                        const float f2 = std::sin (animTime * 45.0f + (float) (seg * 13 + a * 7)) * 0.5f;
                        const float f3 = std::sin (animTime * 80.0f + (float) (seg * 29)) * 0.25f;
                        const float jitter = (f1 + f2 + f3) * (r * (0.12f + 0.15f * val) * env);

                        arcPoints.push_back ({ baseX + nx * jitter, baseY + ny * jitter });
                    }
                    arcPoints.push_back ({ p2x, p2y });

                    const float arcAlpha = juce::jlimit (0.0f, 1.0f, (energy - 0.02f) * 3.5f)
                                         * (0.75f + 0.25f * std::sin (animTime * 15.0f + (float) a * 3.0f));

                    // 2.1 沿电弧各分段平滑绘制多色全息流动光谱（珍珠冰蓝 -> 耀眼亮紫 -> 鲜亮炽红 -> 霓虹绯红）
                    auto getHoloArcCol = [&] (float frac) -> juce::Colour
                    {
                        const float colorPhase = std::fmod (frac + animTime * 0.8f + (float) a * 0.33f + 10.0f, 1.0f);
                        if (colorPhase < 0.22f)
                            return juce::Colour (0xFFDBE6F9); // 珍珠冰蓝
                        if (colorPhase < 0.55f)
                            return juce::Colour (0xFFD62BFF); // 耀眼亮紫
                        if (colorPhase < 0.85f)
                            return juce::Colour (0xFFFF2655); // 鲜亮炽红
                        return juce::Colour (0xFFFF5280);     // 霓虹绯红
                    };

                    // 2.2 [第一层外发光]：超宽弥散全息等离子光晕 (Glow Halo: 12px ~ 6px)
                    for (size_t seg = 0; seg + 1 < arcPoints.size(); ++seg)
                    {
                        const float frac = (float) seg / (float) (arcPoints.size() - 1);
                        const auto pA = arcPoints[seg];
                        const auto pB = arcPoints[seg + 1];
                        const auto segCol = getHoloArcCol (frac);

                        g.setColour (segCol.withAlpha (arcAlpha * 0.20f));
                        g.drawLine (juce::Line<float> (pA, pB), 10.0f);

                        g.setColour (segCol.withAlpha (arcAlpha * 0.40f));
                        g.drawLine (juce::Line<float> (pA, pB), 5.5f);

                        // 节点放电柔光球
                        if (seg % 3 == 0)
                        {
                            g.setColour (segCol.withAlpha (arcAlpha * 0.30f));
                            g.fillEllipse (pA.x - 6.0f, pA.y - 6.0f, 12.0f, 12.0f);
                        }
                    }

                    // 2.3 [第二层中芯流光]：高饱和彩色能量光脉 (Core Color: 2.2px)
                    for (size_t seg = 0; seg + 1 < arcPoints.size(); ++seg)
                    {
                        const float frac = (float) seg / (float) (arcPoints.size() - 1);
                        const auto pA = arcPoints[seg];
                        const auto pB = arcPoints[seg + 1];
                        const auto segCol = getHoloArcCol (frac);

                        g.setColour (segCol.withAlpha (arcAlpha * 0.95f));
                        g.drawLine (juce::Line<float> (pA, pB), 2.4f);
                    }

                    // 2.4 [第三层内核]：炽白高能放电细丝 (White Hot Filament: 1.0px)
                    juce::Path coreFilament;
                    coreFilament.startNewSubPath (arcPoints[0]);
                    for (size_t seg = 1; seg < arcPoints.size(); ++seg)
                        coreFilament.lineTo (arcPoints[seg]);

                    g.setColour (juce::Colours::white.withAlpha (arcAlpha * 0.95f));
                    g.strokePath (coreFilament, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

                    // 2.5 [分形子电弧]：从主电弧中段爆裂出的微细分叉电火花 (Forked Lightning Sparks)
                    if (energy > 0.10f)
                    {
                        const int forkMid = numSegments / 2 + (a % 3) - 1;
                        if (forkMid >= 1 && forkMid < (int) arcPoints.size())
                        {
                            const auto forkOrigin = arcPoints[(size_t) forkMid];
                            const float forkAng = std::atan2 (dy, dx) + (a % 2 == 0 ? 0.85f : -0.85f)
                                                + std::sin (animTime * 30.0f) * 0.35f;
                            const float forkLen = r * 0.28f * (0.6f + 0.4f * energy);

                            juce::Point<float> curFork = forkOrigin;
                            const int forkSteps = 4;
                            for (int fs = 1; fs <= forkSteps; ++fs)
                            {
                                const float fFrac = (float) fs / (float) forkSteps;
                                const float fStepLen = forkLen / (float) forkSteps;
                                const float fJitter = std::sin (animTime * 50.0f + (float) fs * 9.0f) * (r * 0.06f);
                                const auto nextFork = curFork + juce::Point<float> (
                                    std::cos (forkAng) * fStepLen - std::sin (forkAng) * fJitter,
                                    std::sin (forkAng) * fStepLen + std::cos (forkAng) * fJitter);

                                const auto forkCol = getHoloArcCol (fFrac);
                                g.setColour (forkCol.withAlpha (arcAlpha * (1.0f - fFrac * 0.8f) * 0.70f));
                                g.drawLine (juce::Line<float> (curFork, nextFork), 4.0f);

                                g.setColour (juce::Colours::white.withAlpha (arcAlpha * (1.0f - fFrac * 0.8f)));
                                g.drawLine (juce::Line<float> (curFork, nextFork), 1.0f);

                                curFork = nextFork;
                            }
                        }
                    }
                }
            }

            // 3. 3 组交错旋转的对数等离子流光带（温和转速，随响度律动加速发光）
            const int numRibbons = 3;
            for (int v = 0; v < numRibbons; ++v)
            {
                const float vDir = (v % 2 == 0) ? 1.0f : -0.85f;
                const float vRot = t * 0.22f * vDir + (float) v * (6.2831853f / (float) numRibbons);
                const float rx = r * 0.85f;
                const float ry = r * (0.42f + 0.12f * std::sin (t * 1.5f + (float) v * 1.4f));

                juce::Graphics::ScopedSaveState vSave (g);
                g.addTransform (juce::AffineTransform::rotation (vRot, cx, cy));
                g.addTransform (juce::AffineTransform::translation (cx, cy));

                const auto& ringImg = getRingImage (v + 2);
                const float ribbonAlpha = (isWild ? (0.45f + val * 0.30f) : (0.18f + val * 0.16f)) * (1.0f + energy * 1.35f);

                g.setOpacity (juce::jmin (1.0f, ribbonAlpha));
                g.drawImage (ringImg, juce::Rectangle<float> (-rx, -ry, rx * 2.0f, ry * 2.0f));

                // 沿流光带游动的柔和微光斑
                const int numNodes = 3;
                for (int n = 0; n < numNodes; ++n)
                {
                    const float nodePhase = t * 0.9f * vDir + (float) n * (6.2831853f / (float) numNodes) + (float) v;
                    const float nx = rx * 0.76f * std::cos (nodePhase);
                    const float ny = ry * 0.76f * std::sin (nodePhase);
                    const float nodeR = r * (0.20f + 0.05f * std::sin (t * 2.2f + (float) n));

                    g.setOpacity (juce::jmin (1.0f, ribbonAlpha * 0.85f));
                    g.drawImage (getGlowImage (v + n), juce::Rectangle<float> (nx - nodeR, ny - nodeR, nodeR * 2.0f, nodeR * 2.0f));
                }
            }

            // =================================================================
            // 4. [高阶混乱特效 A]：量子能量漩涡星尘 + 彗尾拖光 (Quantum Vortex Comet Streaks)
            // 当百分比 val >= 0.25 且处于狂野模式时激活，百分比越大越躁动密集、拖尾越长
            // =================================================================
            if (isWild && val >= 0.25f)
            {
                const float chaosIntensity = (val - 0.25f) / 0.75f; // 0.0 -> 1.0
                const int numSparks = 8 + juce::roundToInt (chaosIntensity * 28.0f);
                const float vortexSpeed = (1.5f + chaosIntensity * 4.5f) * (1.0f + energy * 1.5f);
                const int tailSegments = 6;

                for (int s = 0; s < numSparks; ++s)
                {
                    const float seed = (float) s * 2.3999632f;
                    const float spiralT = animTime * vortexSpeed + seed;

                    // 计算粒子当前头部位置 (Head)
                    const float rNorm = std::fmod (std::abs (std::sin (spiralT * 0.4f + seed)), 1.0f);
                    const float pr = r * (0.15f + rNorm * 0.70f);
                    const float pAngle = spiralT * 1.2f + seed * 3.14f;

                    const float px = cx + pr * std::cos (pAngle);
                    const float py = cy + pr * std::sin (pAngle);
                    const float pSize = (1.5f + 2.5f * std::sin (seed * 4.3f)) * (0.8f + 0.6f * chaosIntensity);

                    const float pAlpha = juce::jlimit (0.0f, 1.0f, std::sin (spiralT * 2.0f + seed) * 0.5f + 0.5f)
                                       * chaosIntensity * (0.50f + 0.50f * energy);

                    const auto pCol = (s % 3 == 0) ? juce::Colour (0xFFDBE6F9) // 珍珠冰蓝
                                    : (s % 3 == 1) ? juce::Colour (0xFFD62BFF) // 耀眼亮紫
                                                   : juce::Colour (0xFFFF2655); // 鲜亮炽红

                    // 4.1 绘制全息平滑渐变彗尾 (Comet Tail Path)
                    juce::Path tailPath;
                    tailPath.startNewSubPath (px, py);

                    const float tailDuration = (0.12f + 0.18f * chaosIntensity) * (1.0f + energy * 0.8f);
                    float tailEndX = px, tailEndY = py;

                    for (int seg = 1; seg <= tailSegments; ++seg)
                    {
                        const float dt = (float) seg / (float) tailSegments * tailDuration;
                        const float pastT = spiralT - dt * vortexSpeed;

                        const float pastRNorm = std::fmod (std::abs (std::sin (pastT * 0.4f + seed)), 1.0f);
                        const float pastPr = r * (0.15f + pastRNorm * 0.70f);
                        const float pastAngle = pastT * 1.2f + seed * 3.14f;

                        const float pastX = cx + pastPr * std::cos (pastAngle);
                        const float pastY = cy + pastPr * std::sin (pastAngle);

                        tailPath.lineTo (pastX, pastY);
                        if (seg == tailSegments)
                        {
                            tailEndX = pastX;
                            tailEndY = pastY;
                        }
                    }

                    // 彗尾从头部至尾部平滑渐隐 + 颜色微色散
                    juce::ColourGradient tailGrad (
                        pCol.withAlpha (pAlpha * 0.75f), px, py,
                        pCol.withAlpha (0.0f), tailEndX, tailEndY, false);
                    tailGrad.addColour (0.50, ((s % 2 == 0) ? juce::Colour (0xFFD62BFF) : juce::Colour (0xFFFF2655)).withAlpha (pAlpha * 0.35f));

                    g.setGradientFill (tailGrad);
                    g.strokePath (tailPath, juce::PathStrokeType (pSize * 0.85f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

                    // 4.2 绘制粒子头部星尘核
                    g.setColour (pCol.withAlpha (pAlpha));
                    g.fillEllipse (px - pSize * 0.5f, py - pSize * 0.5f, pSize, pSize);

                    if (chaosIntensity > 0.5f && (s % 2 == 0))
                    {
                        g.setColour (juce::Colours::white.withAlpha (pAlpha * 0.85f));
                        g.fillEllipse (px - 1.0f, py - 1.0f, 2.0f, 2.0f);
                    }
                }
            }

            // =================================================================
            // 5. [高阶混乱特效 B]：微距亚像素全息色散光栅切片 (Chromatic Dispersion Slit Scan)
            // 采用极细微的亚像素级横向微位移、柔和高斯边缘羽化与 RGB 三原色轻微分离
            // =================================================================
            if (isWild && val >= 0.50f)
            {
                const float glitchIntensity = (val - 0.50f) / 0.50f; // 0.0 -> 1.0
                const int numSlices = 3 + juce::roundToInt (glitchIntensity * 5.0f);

                for (int gIdx = 0; gIdx < numSlices; ++gIdx)
                {
                    const float gSeed = (float) gIdx * 4.7123889f;
                    // 高频柔性突发脉冲
                    const float glitchPulse = std::pow (0.5f + 0.5f * std::sin (animTime * 16.0f + gSeed * 6.3f), 6.0f);

                    if (glitchPulse < 0.20f && energy < 0.15f)
                        continue;

                    const float sliceYNorm = std::sin (animTime * 2.8f + gSeed * 1.5f) * 0.75f;
                    const float sliceY = cy + r * sliceYNorm;
                    // 细微精致的切片高度 (1.2px ~ 3.5px)
                    const float sliceH = 1.2f + 2.3f * std::sin (gSeed * 2.1f + animTime);
                    const float sliceHalfW = std::sqrt (juce::jmax (0.0f, r * r * 0.88f - (sliceY - cy) * (sliceY - cy)));

                    if (sliceHalfW <= 4.0f)
                        continue;

                    // 细腻的微距平移 (1.5px ~ 6px)，绝不粗糙错位
                    const float shift = std::sin (animTime * 24.0f + gSeed) * (1.5f + 4.5f * glitchIntensity);
                    const float sliceAlpha = (glitchPulse * 0.55f + energy * 0.45f) * glitchIntensity;

                    // 5.1 全息色散横向光栅薄膜（两端柔和羽化渐隐）
                    juce::ColourGradient slitGrad (
                        juce::Colour (0x00DBE6F9), cx - sliceHalfW + shift, sliceY,
                        juce::Colour (0x00FF2655), cx + sliceHalfW + shift, sliceY, false);
                    slitGrad.addColour (0.20, juce::Colour (0xFFDBE6F9).withAlpha (sliceAlpha * 0.60f)); // 珍珠冰蓝
                    slitGrad.addColour (0.50, juce::Colour (0xFFD62BFF).withAlpha (sliceAlpha * 0.75f)); // 耀眼亮紫
                    slitGrad.addColour (0.80, juce::Colour (0xFFFF2655).withAlpha (sliceAlpha * 0.60f)); // 鲜亮炽红

                    g.setGradientFill (slitGrad);
                    g.fillRect (cx - sliceHalfW + shift, sliceY, sliceHalfW * 2.0f, sliceH);

                    // 5.2 亚像素级纳米色散边缘分离线 (微红/微蓝通道轻微错开 0.75px)
                    if (glitchIntensity > 0.4f)
                    {
                        // 蓝色色散通道微偏移 (+0.75px)
                        g.setColour (juce::Colour (0xFFDBE6F9).withAlpha (sliceAlpha * 0.45f));
                        g.fillRect (cx - sliceHalfW + shift + 0.75f, sliceY, sliceHalfW * 2.0f, 0.75f);

                        // 红色色散通道微偏移 (-0.75px)
                        g.setColour (juce::Colour (0xFFFF2655).withAlpha (sliceAlpha * 0.45f));
                        g.fillRect (cx - sliceHalfW + shift - 0.75f, sliceY + sliceH - 0.75f, sliceHalfW * 2.0f, 0.75f);
                    }

                    // 5.3 中心极细白金全息微导线
                    g.setColour (juce::Colours::white.withAlpha (sliceAlpha * 0.50f));
                    g.fillRect (cx - sliceHalfW * 0.7f + shift, sliceY + sliceH * 0.5f, sliceHalfW * 1.4f, 0.5f);
                }
            }
        }

        // C. 波形球体边缘的彩色流光轮廓线（清晰跃动的全息光脉）
        {
            const float strokeW = 1.6f + val * 1.0f + energy * 2.4f + (isMax ? 0.8f : 0.0f);
            const float strokeAlpha = juce::jmin (1.0f, (isWild ? (0.75f + val * 0.25f) : (0.45f + val * 0.25f)) * (1.0f + energy * 0.75f));

            // 1. 柔光外晕线
            juce::ColourGradient glowStrokeGrad (
                (isWild ? juce::Colour (0xFFFF2655) : juce::Colour (0xFFE8BCCE)).withAlpha (strokeAlpha * 0.40f), cx - r, cy - r,
                (isWild ? juce::Colour (0xFFFF4D75) : juce::Colour (0xFFB6E6DC)).withAlpha (strokeAlpha * 0.50f), cx + r * 0.4f, cy + r, false);
            glowStrokeGrad.addColour (0.45, (isWild ? juce::Colour (0xFFD62BFF) : juce::Colour (0xFFCEBFF0)).withAlpha (strokeAlpha * 0.50f));
            glowStrokeGrad.addColour (0.75, (isWild ? juce::Colour (0xFFFF2655) : juce::Colour (0xFFB6E6DC)).withAlpha (strokeAlpha * 0.50f));
            g.setGradientFill (glowStrokeGrad);
            g.strokePath (waveCore, juce::PathStrokeType (strokeW * 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // 2. 核心流光线
            juce::ColourGradient coreStrokeGrad (
                (isWild ? (isMax ? juce::Colours::white : juce::Colour (0xFFFF2655)) : juce::Colour (0xFFE8BCCE)).withAlpha (strokeAlpha * 0.70f), cx - r, cy - r,
                (isWild ? juce::Colour (0xFFFF4D75) : juce::Colour (0xFFB6E6DC)).withAlpha (strokeAlpha), cx + r * 0.4f, cy + r, false);
            coreStrokeGrad.addColour (0.45, (isWild ? juce::Colour (0xFFD62BFF) : juce::Colour (0xFFCEBFF0)).withAlpha (strokeAlpha));
            coreStrokeGrad.addColour (0.70, (isWild ? juce::Colour (0xFFFF2655) : juce::Colour (0xFFB6E6DC)).withAlpha (strokeAlpha));
            g.setGradientFill (coreStrokeGrad);
            g.strokePath (waveCore, juce::PathStrokeType (strokeW, isWild && (energy > 0.05f) ? juce::PathStrokeType::mitered : juce::PathStrokeType::curved, isWild && (energy > 0.05f) ? juce::PathStrokeType::butt : juce::PathStrokeType::rounded));
        }
    }

    //==============================================================================
    float targetValue    = 0.5f;
    float smoothValue    = 0.5f;
    float dragStartValue = 0.5f;
    juce::Point<float> dragStartPos;
    bool isDragging      = false;
    bool isHovered       = false;

    float animTime       = 0.0f;
    float hoverAlpha     = 0.0f;
    float ripplePhase    = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrismKnob)
};

} // namespace ozo

