#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "OzoLookAndFeel.h"

namespace ozo
{

//==============================================================================
// ViewToggleBar —— 用于切换 ONE-KNOB 与 EXPERT 形态的极简无边框毛玻璃胶囊
// 彻底去除多余生硬描边线，采用纯净轻盈的微光背景与高保真字形排版
//==============================================================================
class ViewToggleBar : public juce::Component
{
public:
    ViewToggleBar()
    {
        setInterceptsMouseClicks (true, false);
    }

    std::function<void(bool)> onViewModeChanged;

    bool isExpanded() const noexcept { return expanded; }

    void setExpanded (bool exp)
    {
        if (expanded != exp)
        {
            expanded = exp;
            repaint();
            if (onViewModeChanged)
                onViewModeChanged (expanded);
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

    void mouseDown (const juce::MouseEvent&) override
    {
        setExpanded (! expanded);
    }

    void mouseEnter (const juce::MouseEvent&) override
    {
        hovered = true;
        repaint();
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hovered = false;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
        const float r = bounds.getHeight() * 0.5f;

        if (isWild)
        {
            // =================================================================
            // 狂野模式：暗黑高透流光底板
            // =================================================================
            if (hovered || expanded)
            {
                juce::ColourGradient wildGrad (
                    juce::Colour (0x50FF2A55), bounds.getX(), bounds.getY(),
                    juce::Colour (0x3500E5FF), bounds.getRight(), bounds.getBottom(), false);
                g.setGradientFill (wildGrad);
                g.fillRoundedRectangle (bounds, r);
            }
            else
            {
                g.setColour (juce::Colour (0x2A180816));
                g.fillRoundedRectangle (bounds, r);
            }

            drawContent (g, bounds, hovered ? juce::Colours::white : (expanded ? juce::Colour (0xFFFF4070) : juce::Colour (0xFFFF80A0)));
        }
        else
        {
            // =================================================================
            // 常规浅色模式：缎面微透晶体底板
            // =================================================================
            if (hovered || expanded)
            {
                g.setColour (juce::Colour (0x30B49CFF));
                g.fillRoundedRectangle (bounds, r);
            }
            else
            {
                g.setColour (juce::Colour (0x14000000));
                g.fillRoundedRectangle (bounds, r);
            }

            drawContent (g, bounds, expanded ? OzoCol::tintText[1] : (hovered ? OzoCol::text : OzoCol::textDim));
        }
    }

private:
    void drawContent (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour col)
    {
        g.setColour (col);
        const float iconSize = 8.0f;
        const float iconCx = bounds.getX() + 14.0f;
        const float iconCy = bounds.getCentreY();

        if (expanded)
        {
            // 绘制标准无锯齿矢量叉号 ✕
            juce::Path p;
            p.addLineSegment (juce::Line<float> (iconCx - 3.5f, iconCy - 3.5f, iconCx + 3.5f, iconCy + 3.5f), 1.6f);
            p.addLineSegment (juce::Line<float> (iconCx - 3.5f, iconCy + 3.5f, iconCx + 3.5f, iconCy - 3.5f), 1.6f);
            g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
            g.drawText ("CLOSE", bounds.withTrimmedLeft (24.0f), juce::Justification::centredLeft, false);
        }
        else
        {
            // 绘制精致四角星芒 ✦
            juce::Path p;
            const float R = 4.2f;
            p.startNewSubPath (iconCx, iconCy - R);
            p.quadraticTo (iconCx, iconCy, iconCx + R, iconCy);
            p.quadraticTo (iconCx, iconCy, iconCx, iconCy + R);
            p.quadraticTo (iconCx, iconCy, iconCx - R, iconCy);
            p.quadraticTo (iconCx, iconCy, iconCx, iconCy - R);
            p.closeSubPath();
            g.fillPath (p);

            g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
            g.drawText ("EXPERT", bounds.withTrimmedLeft (24.0f), juce::Justification::centredLeft, false);
        }
    }

    bool expanded = false;
    bool hovered  = false;
    bool isWild   = false;
};

} // namespace ozo
