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
#include "ui/CharPicker.h"
#include "ui/PresetBar.h"
#include "ui/PrismKnob.h"
#include "ui/ViewToggleBar.h"

namespace ozo
{

//==============================================================================
class EZampEditor : public juce::AudioProcessorEditor,
                    private juce::Timer,
                    private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit EZampEditor (EZampProcessor&);
    ~EZampEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void setViewMode (bool expanded);
    void timerCallback() override;

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

    // 任何参数被改动都清掉预设选中态。加载预设本身也会触发这条，
    // 所以用 applyingPreset 把那一次挡掉。
    void parameterChanged (const juce::String&, float) override;
    bool applyingPreset = false;
    bool updatingMacro  = false;
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

    std::vector<std::unique_ptr<Knob>> knobs;          // 0=Drive 1=Weight 2=Air
    std::vector<std::unique_ptr<Knob>> outKnobs;       // 0=Input 1=Output 2=Mix

    // ONE-KNOB 与形态切换
    PrismKnob        prismKnob;
    ViewToggleBar    viewToggle;
    bool             isExpandedView = false;
    void applyMacroToParams (float macroVal);
    void updateMacroFromParams();

    CharPicker       charPicker;
    PresetBar        presetBar;

    // Weight & Air 智能联动锁按钮
    juce::ToggleButton linkButton;
    bool isToneLinked = true;

    juce::ToggleButton matchButton;
    juce::ToggleButton hqButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> matchAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> hqAttachment;

    // 狂野模式开关（满档时浮现）
    WildButton  wildButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> wildAttachment;
    void applyWildTheme (bool wild);
    bool lastWild = false;
    float wildClock = 0.0f;      // WILD 光晕闪烁的相位累加器

    // 侧边抽屉平滑进出过渡动画插值器
    float drawerProgress = 0.0f;
    float targetDrawerProgress = 0.0f;

    LevelBar inBar   { "IN"  };
    LevelBar outBar  { "OUT" };

    int  lastPresetIndex = -2;   // -1 是合法值（无选中），所以初始用一个对不上的数

    void refreshPresetBar();
    void promptSavePreset();

    // 每帧从分析器里捞出来的频谱副本，交给背景层画
    std::array<float, SpectrumAnalyser::kNumBins> specBuf {};
    std::array<float, SpectrumAnalyser::kNumBins> peakBuf {};
    uint32_t lastSpecTick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EZampEditor)
};

} // namespace ozo
