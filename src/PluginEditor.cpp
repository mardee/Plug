#include "PluginEditor.h"

namespace ozo
{

// 版面尺寸在 ui/Layout.h 里，Editor 和 PanelLayer 共用同一份。

//==============================================================================
EZampEditor::EZampEditor (EZampProcessor& p)
    : juce::AudioProcessorEditor (p),
      processor (p)
{
    setLookAndFeel (&lnf);

    // 背景三层，顺序即层级（JUCE 按加入顺序绘制子组件）：
    //   频谱垫在最底 → 面板夹在中间 → 粒子飘在最上面
    // 面板必须做成独立组件，否则它作为 Editor::paint 的一部分会被频谱盖住。
    spectrumLayer.setPalette (&palette);
    particleLayer.setPalette (&palette);

    addAndMakeVisible (spectrumLayer);
    addAndMakeVisible (panelLayer);
    addAndMakeVisible (particleLayer);

    // 注意：setSize 会立刻触发一次 resized()，所以必须放在所有控件建好之后，
    // 否则 resized() 里访问 knobs / outKnobs 时容器还是空的（空指针解引用直接崩）。
    // 上一版就是把它写在构造函数第一行，宿主一打开界面就 SIGSEGV。

    // ---------------------------------------------------------------------
    // 主旋钮。四个染色旋钮各占一个粉彩主题色：
    //   Drive=亮粉  Weight=薰衣草  Air=薄荷  Glue=蜜桃
    // 同一个色也决定了背景频谱与粒子的配色权重。
    // ---------------------------------------------------------------------
    addKnob (ParamID::input,  "INPUT",  " dB", 1, true,  -1);
    addKnob (ParamID::drive,  "DRIVE",  " %",  0, false,  0);
    addKnob (ParamID::weight, "WEIGHT", " %",  0, false,  1);
    addKnob (ParamID::air,    "AIR",    " %",  0, false,  2);
    addKnob (ParamID::glue,   "GLUE",   " %",  0, false,  3);

    addKnob (ParamID::output, "OUTPUT", " dB", 1, true,  -1);
    addKnob (ParamID::mix,    "MIX",    " %",  0, false, -1);

    // ---------------------------------------------------------------------
    // Character
    // ---------------------------------------------------------------------
    const char* charNames[3] = { "Tape", "Tube", "Console" };

    for (int i = 0; i < 3; ++i)
    {
        auto& b = charButtons[i];
        b.setButtonText (charNames[i]);
        b.setClickingTogglesState (false);
        b.onClick = [this, i]
        {
            if (auto* param = processor.getAPVTS().getParameter (ParamID::character))
                param->setValueNotifyingHost (param->convertTo0to1 ((float) i));
        };
        addAndMakeVisible (b);
    }

    // ---------------------------------------------------------------------
    // 预设
    // ---------------------------------------------------------------------
    for (int i = 0; i < PresetFactory::getNumPresets(); ++i)
    {
        auto& b = presetButtons[i];
        b.setButtonText (PresetFactory::getName (i));
        b.setClickingTogglesState (false);
        b.onClick = [this, i] { processor.applyPreset (i); };
        addAndMakeVisible (b);
    }

    // ---------------------------------------------------------------------
    // 开关
    // ---------------------------------------------------------------------
    matchButton.setButtonText ("Auto Match");
    hqButton.setButtonText ("HQ");

    matchAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.getAPVTS(), ParamID::match, matchButton);
    hqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.getAPVTS(), ParamID::hq, hqButton);

    addAndMakeVisible (matchButton);
    addAndMakeVisible (hqButton);

    // ---------------------------------------------------------------------
    // 狂野模式开关
    //
    // 它不是"多一个效果旋钮"：打开后 DSP 换算法（波形折叠 + 次八度）、
    // 各级参数加倍，界面整套转成炽热暗色。所以点击之后要刷新整个主题。
    // ---------------------------------------------------------------------
    // 字形是画出来的（Graffiti 字体），不走 setButtonText —— juce::Button 没有这个方法
    wildAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.getAPVTS(), ParamID::wild, wildButton);

    // 立即生效，不等下一帧 —— 开关的手感不能拖
    wildButton.onClick = [this] { applyWildTheme (wildButton.getToggleState()); };
    addAndMakeVisible (wildButton);

    // ---------------------------------------------------------------------
    addAndMakeVisible (grMeter);
    addAndMakeVisible (inBar);
    addAndMakeVisible (outBar);

    // 所有控件就位后再定尺寸——这一行会触发第一次 resized()
    setSize (Layout::width, Layout::height);

    // 恢复工程时可能已经是狂野状态，先把界面同步成参数当前值
    bool wildNow = false;
    if (auto* p = processor.getAPVTS().getRawParameterValue (ParamID::wild))
        wildNow = p->load() > 0.5f;
    applyWildTheme (wildNow);

    startTimerHz (30);
}

EZampEditor::~EZampEditor()
{
    setLookAndFeel (nullptr);
}

//==============================================================================
void EZampEditor::addKnob (const juce::String& paramId, const juce::String& displayName,
                           const juce::String& suffix, int decimals, bool showSign,
                           int theme)
{
    auto k = std::make_unique<Knob>();
    k->suffix   = suffix;
    k->decimals = decimals;
    k->showSign = showSign;
    k->theme    = theme;

    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    k->slider.setRotaryParameters (juce::degreesToRadians (-140.0f),
                                   juce::degreesToRadians ( 140.0f), true);
    k->slider.setDoubleClickReturnValue (true, paramId == ParamID::input
                                            || paramId == ParamID::output ? 0.0 : 50.0);

    // 主题色通过 Slider 的属性传给 LookAndFeel —— 让绘制层自己去查，
    // 比在 Editor 里硬塞颜色干净，换皮肤时也不用改这里。
    if (theme >= 0)
        k->slider.getProperties().set (OzoCol::propTheme, theme);

    k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.getAPVTS(), paramId, k->slider);

    k->name.setText (displayName, juce::dontSendNotification);
    k->name.setJustificationType (juce::Justification::centred);
    k->name.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    k->name.setColour (juce::Label::textColourId,
                       theme >= 0 ? OzoCol::tintText[(size_t) theme] : OzoCol::textDim);

    k->value.setJustificationType (juce::Justification::centred);
    k->value.setFont (juce::Font (juce::FontOptions (12.0f)));
    k->value.setColour (juce::Label::textColourId,
                        theme >= 0 ? OzoCol::tintText[(size_t) theme] : OzoCol::accentText);

    addAndMakeVisible (k->slider);
    addAndMakeVisible (k->name);
    addAndMakeVisible (k->value);

    if (paramId == ParamID::output || paramId == ParamID::mix)
        outKnobs.push_back (std::move (k));
    else
        knobs.push_back (std::move (k));
}

//==============================================================================
void EZampEditor::paint (juce::Graphics& g)
{
    // 面板、品牌、页脚全都搬去 PanelLayer 了（那里才能被夹在频谱和粒子中间）。
    // 这里只剩一层兜底底色，防止背景层还没铺开时露出残影。
    g.fillAll (OzoCol::bg);
}

//==============================================================================
void EZampEditor::layoutKnob (Knob& k, float centreX, float y, float size)
{
    k.slider.setBounds (juce::roundToInt (centreX - size * 0.5f), juce::roundToInt (y),
                        juce::roundToInt (size), juce::roundToInt (size));

    k.name.setBounds  (juce::roundToInt (centreX - 60.0f), juce::roundToInt (y + size + 1.0f),
                      120, 13);
    k.value.setBounds (juce::roundToInt (centreX - 60.0f), juce::roundToInt (y + size + 14.0f),
                      120, 13);
}

void EZampEditor::resized()
{
    // 三层背景铺满整个窗口（即使控件还没建齐也要铺，所以放在兜底判断之前）
    spectrumLayer.setBounds (getLocalBounds());
    panelLayer.setBounds    (getLocalBounds());
    particleLayer.setBounds (getLocalBounds());

    // 兜底：控件还没建齐时不要布局（防御未来有人在构造中途触发 resized）
    if (knobs.size() < 5 || outKnobs.size() < 2)
        return;

    // ---- 主旋钮：5 个，均分面板宽度 ----
    const int panelX = Layout::margin;
    const int panelW = getWidth() - 2 * Layout::margin;
    const float step = (float) panelW / 5.0f;
    const float centre0 = (float) panelX + step * 0.5f;

    for (size_t i = 0; i < knobs.size(); ++i)
        layoutKnob (*knobs[i], centre0 + step * (float) i, (float) (Layout::panel1Y + 12), 84.0f);

    // ---- Character 按钮 ----
    const int charY = Layout::panel1Y + 152;   // 底部留 6 px，别顶出面板
    const int charW = 110, charGap = 10, charH = 28;
    const int charTotal = 3 * charW + 2 * charGap;
    int charX = panelX + (panelW - charTotal) / 2;

    for (int i = 0; i < 3; ++i)
    {
        charButtons[i].setBounds (charX, charY, charW, charH);
        charX += charW + charGap;
    }

    // ---- 输出旋钮（比主旋钮小一号，且要让名称+数值都在面板内）----
    layoutKnob (*outKnobs[0], (float) panelX + 78.0f,  (float) (Layout::panel2Y + 14), 68.0f);
    layoutKnob (*outKnobs[1], (float) panelX + 196.0f, (float) (Layout::panel2Y + 14), 68.0f);

    // ---- 表 ----
    grMeter.setBounds (290, Layout::panel2Y + 28, getWidth() - 290 - Layout::margin, 34);
    inBar.setBounds   (290, Layout::panel2Y + 68, getWidth() - 290 - Layout::margin, 15);
    outBar.setBounds  (290, Layout::panel2Y + 87, getWidth() - 290 - Layout::margin, 15);

    // ---- 预设按钮 ----
    const int numPresets = PresetFactory::getNumPresets();
    const int presetW = 116, presetGap = 8;
    const int presetTotal = numPresets * presetW + (numPresets - 1) * presetGap;
    int presetX = panelX + (panelW - presetTotal) / 2;

    for (int i = 0; i < numPresets; ++i)
    {
        presetButtons[i].setBounds (presetX, Layout::presetY, presetW, Layout::presetH);
        presetX += presetW + presetGap;
    }

    // ---- 头部开关 ----
    // WILD 比另外两个大一号：它是模式开关，不是小选项
    // WILD 比另外两个大一号：它是模式开关，不是小选项。
    // 涂鸦字形比普通字体占地方，所以再放宽一点，否则描边会被切掉。
    wildButton.setBounds  (getWidth() - 330, 16, 112, 30);
    matchButton.setBounds (getWidth() - 210, 20, 96, 22);
    hqButton.setBounds    (getWidth() - 104, 20, 80, 22);
}

//==============================================================================
// 切换狂野主题。换色之后有三件事必须做，缺一件界面就会卡在旧主题上：
//   1. OzoCol::apply       —— 换掉颜色变量本身
//   2. lnf.refreshColours  —— LookAndFeel 的 setColour 是拷贝值，不会自动跟
//   3. palette.refresh     —— 频谱配色 LUT 只在权重变化时重算，换色不触发
void EZampEditor::applyWildTheme (bool wild)
{
    lastWild = wild;

    OzoCol::apply (wild);
    lnf.refreshColours();
    palette.refresh();

    // 旋钮标签的颜色是建控件时按当时的 tintText 定死的，得重设
    auto retheme = [] (Knob& k)
    {
        const int t = k.theme;
        k.name.setColour  (juce::Label::textColourId,
                           t >= 0 ? OzoCol::tintText[(size_t) t] : OzoCol::textDim);
        k.value.setColour (juce::Label::textColourId,
                           t >= 0 ? OzoCol::tintText[(size_t) t] : OzoCol::accentText);
    };

    for (auto& k : knobs)    retheme (*k);
    for (auto& k : outKnobs) retheme (*k);

    // 父组件 repaint 不会带走子组件，得逐个刷
    for (int i = 0; i < getNumChildComponents(); ++i)
        if (auto* c = getChildComponent (i))
            c->repaint();

    repaint();
}

//==============================================================================
bool EZampEditor::paramCloseTo (const juce::String& id, float value, float tol) const
{
    auto* p = processor.getAPVTS().getParameter (id);
    if (p == nullptr)
        return false;
    return std::abs (p->getValue() - p->convertTo0to1 (value)) < tol;
}

int EZampEditor::detectPreset() const
{
    auto* charParam = processor.getAPVTS().getParameter (ParamID::character);
    if (charParam == nullptr)
        return -1;

    const int charIdx = juce::roundToInt (charParam->convertFrom0to1 (charParam->getValue()));

    for (int i = 0; i < PresetFactory::getNumPresets(); ++i)
    {
        const Preset& p = PresetFactory::get (i);

        if (charIdx != (int) p.character)
            continue;

        if (! paramCloseTo (ParamID::input,  p.inputDb,        0.01f)) continue;
        if (! paramCloseTo (ParamID::drive,  p.drive  * 100.0f, 0.01f)) continue;
        if (! paramCloseTo (ParamID::weight, p.weight * 100.0f, 0.01f)) continue;
        if (! paramCloseTo (ParamID::air,    p.air    * 100.0f, 0.01f)) continue;
        if (! paramCloseTo (ParamID::glue,   p.glue   * 100.0f, 0.01f)) continue;
        if (! paramCloseTo (ParamID::output, p.outputDb,        0.01f)) continue;
        if (! paramCloseTo (ParamID::mix,    p.mix    * 100.0f, 0.01f)) continue;

        return i;
    }

    return -1;
}

//==============================================================================
void EZampEditor::timerCallback()
{
    // ---- 表 ----
    const auto& chain = processor.getChain();

    grMeter.setGainReductionDb (chain.getGainReductionDb());
    grMeter.tick();

    inBar.setLevelDb  (chain.getInputLevelDb());
    outBar.setLevelDb (chain.getOutputLevelDb());
    inBar.tick();
    outBar.tick();

    grMeter.repaint();
    inBar.repaint();
    outBar.repaint();

    // ---- 数值标签 ----
    auto updateValue = [] (Knob& k)
    {
        const double v = k.slider.getValue();
        juce::String s = juce::String (v, k.decimals);

        if (k.showSign && v > 0.0)
            s = "+" + s;

        if (k.value.getText() != s + k.suffix)
            k.value.setText (s + k.suffix, juce::dontSendNotification);
    };

    for (auto& k : knobs)    updateValue (*k);
    for (auto& k : outKnobs) updateValue (*k);

    // ---- Character 高亮 ----
    auto* charParam = processor.getAPVTS().getParameter (ParamID::character);
    if (charParam != nullptr)
    {
        const int idx = juce::jlimit (0, 2,
            juce::roundToInt (charParam->convertFrom0to1 (charParam->getValue())));

        if (idx != lastCharIndex)
        {
            lastCharIndex = idx;
            for (int i = 0; i < 3; ++i)
                charButtons[i].setToggleState (i == idx, juce::dontSendNotification);
        }
    }

    // ---- 预设高亮 ----
    const int presetIdx = detectPreset();
    if (presetIdx != lastPresetIndex)
    {
        lastPresetIndex = presetIdx;
        for (int i = 0; i < PresetFactory::getNumPresets(); ++i)
            presetButtons[i].setToggleState (i == presetIdx, juce::dontSendNotification);
    }

    // ---- 狂野模式兜底 ----
    // 按钮点击已经在 onClick 里即时生效了；这条是给"参数被外部改"的情况兜底：
    // 宿主自动化、加载工程、撤销重做都可能绕过按钮直接改参数。
    if (auto* wildParam = processor.getAPVTS().getRawParameterValue (ParamID::wild))
    {
        const bool w = wildParam->load() > 0.5f;
        if (w != lastWild)
            applyWildTheme (w);
    }

    // ---- WILD 开关的光晕闪烁 ----
    // 两个不同频率的正弦相乘 → 看起来像不规则呼吸，而不是机械的脉冲。
    wildClock += 1.0f / 30.0f;
    wildButton.setFlicker (0.5f + 0.5f * std::sin (wildClock * 5.3f)
                                      * std::sin (wildClock * 2.1f + 1.7f));

    // 页脚的匹配增益是活的，跟着重绘
    panelLayer.refreshFooter();

    pushVisualState();
}

//==============================================================================
// 把音频侧的数据喂给背景层。每帧一次，只读不写，不做任何分配。
void EZampEditor::pushVisualState()
{
    const auto& spec = processor.getSpectrum();

    const uint32_t tick = spec.getTick();
    const bool fresh = (tick != lastSpecTick);
    lastSpecTick = tick;

    spec.readInto (specBuf.data(), (int) specBuf.size());
    spec.readPeaksInto (peakBuf.data(), (int) peakBuf.size());

    spectrumLayer.setSpectrum (specBuf.data(), (int) specBuf.size(), fresh, peakBuf.data());
    spectrumLayer.setEnergy (spec.getEnergy());
    particleLayer.setEnergy (spec.getEnergy());

    // 四个染色旋钮各领一色，开多大就占多大的配色权重。
    // 加一点点底噪（*0.92 + 0.08）是为了：旋钮全关到 0 时背景不会变成死板的
    // 四等分，而是仍然跟着旋钮的比例走。
    const juce::String* ids[4] = { &ParamID::drive, &ParamID::weight,
                                   &ParamID::air,   &ParamID::glue };
    float w4[4] = {};

    for (int i = 0; i < 4; ++i)
    {
        float v = 0.0f;

        if (auto* p = processor.getAPVTS().getRawParameterValue (*ids[i]))
            v = p->load() * 0.01f;

        w4[i] = juce::jlimit (0.0f, 1.0f, v) * 0.92f + 0.08f;
    }

    palette.setWeights (w4);
}

} // namespace ozo
