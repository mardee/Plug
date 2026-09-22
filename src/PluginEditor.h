#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <memory>
#include <array>

#include "PluginProcessor.h"
#include "ui/OzoLookAndFeel.h"
#include "ui/Meters.h"
#include "ui/Layout.h"
#include "ui/TintPalette.h"
#include "ui/SpectrumLayer.h"
#include "ui/ParticleLayer.h"
#include "ui/PanelLayer.h"
#include "ui/WildButton.h"

namespace ozo
{

//==============================================================================
class EZampEditor : public juce::AudioProcessorEditor,
                    private juce::Timer
{
public:
    explicit EZampEditor (EZampProcessor&);
    ~EZampEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    //--------------------------------------------------------------------------
    struct Knob
    {
        juce::Slider slider;
        juce::Label  name;
        juce::Label  value;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        juce::String suffix;
        int          decimals = 1;
        bool         showSign = false;
        int          theme = -1;       // 0..3 = 粉彩四色之一；-1 = 无主题（用全息渐变）
    };

    void addKnob (const juce::String& paramId, const juce::String& displayName,
                  const juce::String& suffix, int decimals, bool showSign, int theme);
    void layoutKnob (Knob& k, float centreX, float y, float size);

    void timerCallback() override;
    int  detectPreset() const;
    bool paramCloseTo (const juce::String& id, float value, float tol) const;
    void pushVisualState();

    EZampProcessor& processor;
    OzoLookAndFeel  lnf;

    // 三层背景，按加入顺序自下而上：频谱 → 面板 → 粒子。
    // JUCE 按 addAndMakeVisible 的顺序绘制子组件，所以顺序就是层级。
    SpectrumLayer   spectrumLayer;
    PanelLayer      panelLayer { processor };
    ParticleLayer   particleLayer;

    // 频谱层和粒子层共用这一份配色，整屏颜色才不会打架
    TintPalette     palette;

    std::vector<std::unique_ptr<Knob>> knobs;          // 0=Input 1=Drive 2=Weight 3=Air 4=Glue
    std::vector<std::unique_ptr<Knob>> outKnobs;       // 0=Output 1=Mix

    juce::TextButton charButtons[3];
    juce::TextButton presetButtons[6];

    juce::ToggleButton matchButton;
    juce::ToggleButton hqButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> matchAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> hqAttachment;

    // 狂野模式开关。它不是"多一个效果旋钮"，而是换算法 + 换配色，
    // 所以点击之后要整套刷新主题，不只是改个参数。
    WildButton  wildButton;   // 涂鸦喷漆风格的狂野模式开关
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> wildAttachment;
    void applyWildTheme (bool wild);
    bool lastWild = false;
    float wildClock = 0.0f;      // WILD 光晕闪烁的相位累加器

    GrMeter grMeter;
    LevelBar inBar   { "IN"  };
    LevelBar outBar  { "OUT" };

    int  lastPresetIndex = -1;
    int  lastCharIndex   = -1;

    // 每帧从分析器里捞出来的频谱副本，交给背景层画
    std::array<float, SpectrumAnalyser::kNumBins> specBuf {};
    std::array<float, SpectrumAnalyser::kNumBins> peakBuf {};
    uint32_t lastSpecTick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EZampEditor)
};

} // namespace ozo
