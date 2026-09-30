#include "PluginEditor.h"

namespace ozo
{

//==============================================================================
EZampEditor::EZampEditor (EZampProcessor& p)
    : juce::AudioProcessorEditor (p),
      processor (p)
{
    setLookAndFeel (&lnf);

    // 背景三层，顺序即层级：频谱 → 面板 → 粒子
    spectrumLayer.setPalette (&palette);
    particleLayer.setPalette (&palette);

    addAndMakeVisible (spectrumLayer);
    addAndMakeVisible (panelLayer);
    addAndMakeVisible (particleLayer);

    // ---------------------------------------------------------------------
    // 专家模式旋钮 (0=Drive, 1=Weight, 2=Air)
    // ---------------------------------------------------------------------
    addKnob (ParamID::input,  "INPUT",  " dB", 1, true,  -1);
    addKnob (ParamID::drive,  "DRIVE",  " %",  0, false,  0);
    addKnob (ParamID::weight, "WEIGHT", " %",  0, false,  1);
    addKnob (ParamID::air,    "AIR",    " %",  0, false,  2);

    addKnob (ParamID::output, "OUTPUT", " dB", 1, true,  -1);
    addKnob (ParamID::mix,    "MIX",    " %",  0, false, -1);

    // ---------------------------------------------------------------------
    // ONE-KNOB 巨型主控旋钮
    // ---------------------------------------------------------------------
    prismKnob.onValueChanged = [this] (float v)
    {
        if (! isExpandedView || isToneLinked)
            applyMacroToParams (v);
        else
        {
            // 解锁状态下大光球仅独立控制 Drive
            if (auto* dp = processor.getAPVTS().getParameter (ParamID::drive))
                dp->setValueNotifyingHost (std::pow (v, 0.95f));
        }
    };

    prismKnob.onDoubleClicked = [this]
    {
        setViewMode (! isExpandedView);
    };
    addAndMakeVisible (prismKnob);

    // ---------------------------------------------------------------------
    // 形态切换按钮 ViewToggleBar
    // ---------------------------------------------------------------------
    viewToggle.onViewModeChanged = [this] (bool expanded)
    {
        setViewMode (expanded);
    };
    addAndMakeVisible (viewToggle);

    // ---------------------------------------------------------------------
    // Character 拨钮
    // ---------------------------------------------------------------------
    charPicker.onSelect = [this] (int i)
    {
        if (auto* param = processor.getAPVTS().getParameter (ParamID::character))
            param->setValueNotifyingHost (param->convertTo0to1 ((float) i));
    };
    addAndMakeVisible (charPicker);

    // ---------------------------------------------------------------------
    // 预设条
    // ---------------------------------------------------------------------
    presetBar.onSelect = [this] (int index)
    {
        const juce::ScopedValueSetter<bool> guard (applyingPreset, true);
        processor.applyPreset (index);
        updateMacroFromParams();
    };
    presetBar.onSave   = [this] { promptSavePreset(); };
    presetBar.onDelete = [this] (int index)
    {
        processor.deleteUserPreset (index);
        refreshPresetBar();
    };

    refreshPresetBar();
    addAndMakeVisible (presetBar);

    // 监听全部参数
    const juce::String* ids[] = {
        &ParamID::input, &ParamID::drive, &ParamID::character, &ParamID::weight,
        &ParamID::air,   &ParamID::output, &ParamID::mix,
        &ParamID::match, &ParamID::hq,    &ParamID::wild
    };

    for (auto* id : ids)
        processor.getAPVTS().addParameterListener (*id, this);

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
    // Weight & Air 智能联动锁按钮
    // ---------------------------------------------------------------------
    linkButton.setButtonText ("LINKED");
    linkButton.setToggleState (true, juce::dontSendNotification);
    linkButton.onClick = [this]
    {
        isToneLinked = linkButton.getToggleState();
        linkButton.setButtonText (isToneLinked ? "LINKED" : "UNLINK");
        if (isToneLinked)
        {
            if (auto* dp = processor.getAPVTS().getParameter (ParamID::drive))
                applyMacroToParams (dp->getValue());
        }
    };
    addAndMakeVisible (linkButton);

    // ---------------------------------------------------------------------
    // 狂野模式开关（默认隐藏，满档时浮现）
    // ---------------------------------------------------------------------
    wildAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.getAPVTS(), ParamID::wild, wildButton);

    wildButton.onClick = [this]
    {
        const bool w = wildButton.getToggleState();
        if (w != lastWild)
        {
            applyWildTheme (w);
            if (! isExpandedView)
            {
                prismKnob.setValue (0.30f, juce::sendNotification);
                applyMacroToParams (0.30f);
            }
            else
            {
                if (auto* dp = processor.getAPVTS().getParameter (ParamID::drive))
                    dp->setValueNotifyingHost (0.30f);
            }
        }
    };
    addChildComponent (wildButton); // 初始隐藏

    addAndMakeVisible (inBar);
    addAndMakeVisible (outBar);

    // 默认初始为 ONE-KNOB 聚焦形态
    setViewMode (false);

    setSize (Layout::width, Layout::height);

    // 恢复工程时若已是狂野状态则同步主题与显示
    bool wildNow = false;
    if (auto* p = processor.getAPVTS().getRawParameterValue (ParamID::wild))
        wildNow = p->load() > 0.5f;
    applyWildTheme (wildNow);

    updateMacroFromParams();
    startTimerHz (30);
}

EZampEditor::~EZampEditor()
{
    const juce::String* ids[] = {
        &ParamID::input, &ParamID::drive, &ParamID::character, &ParamID::weight,
        &ParamID::air,   &ParamID::output, &ParamID::mix,
        &ParamID::match, &ParamID::hq,    &ParamID::wild
    };

    for (auto* id : ids)
        processor.getAPVTS().removeParameterListener (*id, this);

    setLookAndFeel (nullptr);
}

//==============================================================================
void EZampEditor::setViewMode (bool expanded)
{
    isExpandedView = expanded;
    targetDrawerProgress = expanded ? 1.0f : 0.0f;
    viewToggle.setExpanded (expanded);

    // 唤醒组件可见性（动画结束前保持可见）
    prismKnob.setVisible (true);

    if (knobs.size() >= 3)
    {
        knobs[0]->slider.setVisible (false); // Drive 旋钮隐藏（由大光球主控）
        knobs[0]->name.setVisible   (false);
        knobs[0]->value.setVisible  (false);

        knobs[1]->slider.setVisible (true); // Weight
        knobs[1]->name.setVisible   (true);
        knobs[1]->value.setVisible  (true);

        knobs[2]->slider.setVisible (true); // Air
        knobs[2]->name.setVisible   (true);
        knobs[2]->value.setVisible  (true);
    }

    for (size_t i = 0; i < outKnobs.size(); ++i)
    {
        outKnobs[i]->slider.setVisible (true);
        outKnobs[i]->name.setVisible (true);
        outKnobs[i]->value.setVisible (true);
    }

    linkButton.setVisible (true);
    charPicker.setVisible (true);
    presetBar.setVisible  (true);
    inBar.setVisible      (true);
    outBar.setVisible     (true);
    matchButton.setVisible (true);
    hqButton.setVisible   (true);

    // 若直接初始化（未开定时器前），直接对齐
    if (drawerProgress == 0.0f && ! expanded)
    {
        for (size_t i = 1; i < knobs.size(); ++i)
        {
            knobs[i]->slider.setVisible (false);
            knobs[i]->name.setVisible (false);
            knobs[i]->value.setVisible (false);
        }
        for (size_t i = 0; i < outKnobs.size(); ++i)
        {
            outKnobs[i]->slider.setVisible (false);
            outKnobs[i]->name.setVisible (false);
            outKnobs[i]->value.setVisible (false);
        }
        linkButton.setVisible (false);
        charPicker.setVisible (false);
        presetBar.setVisible (false);
        inBar.setVisible (false);
        outBar.setVisible (false);
        matchButton.setVisible (false);
        hqButton.setVisible (false);
    }

    resized();
    repaint();
}

//==============================================================================
// ONE-KNOB 宏映射算法：驱动 Drive / Weight / Air 协同推进
//==============================================================================
void EZampEditor::applyMacroToParams (float m)
{
    if (updatingMacro)
        return;

    const juce::ScopedValueSetter<bool> guard (updatingMacro, true);

    auto setP = [this] (const juce::String& id, float normVal)
    {
        if (auto* p = processor.getAPVTS().getParameter (id))
            p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normVal));
    };

    // Drive: 0~1 线性带微上翘
    const float driveVal  = std::pow (m, 0.95f);
    // Weight: 前期迅速铺开厚度 (0~0.8)
    const float weightVal = std::sin (m * 1.57079f) * 0.85f;
    // Air: 中前期上升到通透区 (0~0.75)
    const float airVal    = std::sin (m * 1.57079f) * 0.75f;

    setP (ParamID::drive,  driveVal);
    setP (ParamID::weight, weightVal);
    setP (ParamID::air,    airVal);
}

void EZampEditor::updateMacroFromParams()
{
    if (updatingMacro)
        return;

    auto getP = [this] (const juce::String& id) -> float
    {
        if (auto* p = processor.getAPVTS().getParameter (id))
            return p->getValue();
        return 0.5f;
    };

    const float d = getP (ParamID::drive);
    const float w = getP (ParamID::weight);
    const float a = getP (ParamID::air);

    // 估算等效宏值：保留原权重比例，并将三个参数的权重归一化。
    const float avg = (d * 0.5f) + (w * 0.25f) + (a * 0.25f);
    prismKnob.setValue (avg, juce::dontSendNotification);
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

    if (paramId == ParamID::input || paramId == ParamID::output || paramId == ParamID::mix)
        outKnobs.push_back (std::move (k));
    else
        knobs.push_back (std::move (k));
}

//==============================================================================
void EZampEditor::paint (juce::Graphics& g)
{
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
    spectrumLayer.setBounds (getLocalBounds());
    panelLayer.setBounds    (getLocalBounds());
    particleLayer.setBounds (getLocalBounds());

    if (knobs.size() < 3 || outKnobs.size() < 3)
        return;

    const float fullW = (float) getWidth();
    const float fullH = (float) getHeight();
    const float dw = (float) Layout::drawerW;

    // 当前抽屉起始 X 坐标（动画中从 fullW 平滑滑入到 Layout::drawerX）
    const float dx = fullW - dw * drawerProgress;
    const float mainW = dx; // 左侧主视区宽度
    const float mainCx = mainW * 0.5f;

    // 1. 主控全息 PRISM 光球平滑位移
    prismKnob.setBounds (0, 0, juce::roundToInt (mainW), juce::roundToInt (fullH));

    // 2. WILD 按钮跟随主视区中心平滑位移
    wildButton.setBounds (juce::roundToInt (mainCx - 60.0f), getHeight() - 56, 120, 32);

    // 3. 右上角形态切换胶囊
    if (drawerProgress > 0.5f)
        viewToggle.setBounds (juce::roundToInt (dx + dw - 104.0f), 14, 90, 24);
    else
        viewToggle.setBounds (getWidth() - Layout::margin - 96, 18, 96, 24);

    // 4. 抽屉内部子组件随 dx 实时动态排布
    if (drawerProgress > 0.005f)
    {
        // ---------------------------------------------------------------------
        // 分区 1: TONE SHAPING (Weight & Air + Link 按钮)
        // ---------------------------------------------------------------------
        linkButton.setBounds (juce::roundToInt (dx + (dw - 96.0f) * 0.5f), 70, 96, 20);

        const float knobY = 96.0f;
        const float knobSize = 54.0f;
        const float toneKnob1X = dx + dw * 0.30f;
        const float toneKnob2X = dx + dw * 0.70f;

        layoutKnob (*knobs[1], toneKnob1X, knobY, knobSize); // Weight
        layoutKnob (*knobs[2], toneKnob2X, knobY, knobSize); // Air

        // ---------------------------------------------------------------------
        // 分区 2: CHARACTER & ROUTING (Character + In/Out/Mix)
        // ---------------------------------------------------------------------
        charPicker.setBounds (juce::roundToInt (dx + 26.0f), 214, juce::roundToInt (dw - 52.0f), 28);

        const float routeY = 250.0f;
        const float routeKnobSize = 48.0f;
        const float routeStep = (dw - 36.0f) / 3.0f;
        const float routeX0   = (dx + 18.0f) + routeStep * 0.5f;

        for (size_t i = 0; i < outKnobs.size(); ++i)
            layoutKnob (*outKnobs[i], routeX0 + routeStep * (float) i, routeY, routeKnobSize);

        // ---------------------------------------------------------------------
        // 分区 3: LEVELS & PRESETS (In/Out 双联竖表 + 预设条 + 辅助开关)
        // ---------------------------------------------------------------------
        const int mTop = 364;
        const int mH   = 100;
        const int mW   = 36;
        const int mGap = 6;
        const int mLeft = juce::roundToInt (dx + 26.0f);

        inBar.setBounds  (mLeft,             mTop, mW, mH);
        outBar.setBounds (mLeft + mW + mGap, mTop, mW, mH);

        const int rightControlsX = mLeft + 2 * mW + mGap + 14;
        const int rightControlsW = juce::roundToInt (dx + dw - (float) rightControlsX - 22.0f);

        presetBar.setBounds (rightControlsX, mTop, rightControlsW, 30);

        matchButton.setBounds (rightControlsX, mTop + 40, rightControlsW, 24);
        hqButton.setBounds    (rightControlsX, mTop + 68, rightControlsW, 24);
    }
}

//==============================================================================
void EZampEditor::applyWildTheme (bool wild)
{
    lastWild = wild;

    OzoCol::apply (wild);
    lnf.refreshColours();
    palette.refresh();
    panelLayer.setWildMode (wild);
    spectrumLayer.setWildMode (wild);
    viewToggle.setWildMode (wild);
    prismKnob.setWildMode (wild);

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

    for (int i = 0; i < getNumChildComponents(); ++i)
        if (auto* c = getChildComponent (i))
            c->repaint();

    repaint();
}

//==============================================================================
void EZampEditor::parameterChanged (const juce::String&, float)
{
    if (! applyingPreset)
        processor.clearPresetIndex();
}

void EZampEditor::refreshPresetBar()
{
    std::vector<PresetEntry> list;
    list.reserve ((size_t) (PresetFactory::kFactoryCount + processor.getUserPresets().size()));

    for (int i = 0; i < PresetFactory::kFactoryCount; ++i)
        list.push_back ({ PresetFactory::getName (i), true, PresetFactory::isWild (i) });

    for (int i = 0; i < processor.getUserPresets().size(); ++i)
    {
        const auto& e = processor.getUserPresets().get (i);
        list.push_back ({ e.name, false, e.params.wild });
    }

    presetBar.setEntries (std::move (list), processor.getPresetIndex());
    lastPresetIndex = processor.getPresetIndex();
}

void EZampEditor::promptSavePreset()
{
    auto* dialog = new juce::AlertWindow ("Save Preset",
                                           "Name this sound.",
                                           juce::AlertWindow::NoIcon);

    dialog->addTextEditor ("name", "My Preset", "Name:");
    dialog->addButton ("Save",   1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    dialog->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, dialog] (int result)
        {
            if (result != 1)
                return;

            const auto name = dialog->getTextEditorContents ("name").trim();
            if (name.isEmpty())
                return;

            processor.saveUserPreset (name);
            refreshPresetBar();
        }), true);
}

//==============================================================================
void EZampEditor::timerCallback()
{
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

    auto* charParam = processor.getAPVTS().getParameter (ParamID::character);
    if (charParam != nullptr)
        charPicker.setIndex (juce::roundToInt (
            charParam->convertFrom0to1 (charParam->getValue())));

    charPicker.tick();

    if (processor.getPresetIndex() != lastPresetIndex)
        refreshPresetBar();

    // -------------------------------------------------------------------------
    // 狂野模式开关彩蛋：满档 (One-Knob 100% 或 Drive 100%) 时才浮现
    // -------------------------------------------------------------------------
    float driveVal = 0.0f;
    if (auto* dp = processor.getAPVTS().getParameter (ParamID::drive))
        driveVal = dp->getValue();

    const bool isMaxOverdrive = (! isExpandedView && prismKnob.getValue() >= 0.995f)
                             || (isExpandedView && driveVal >= 0.995f);
    const bool isWildActive = wildButton.getToggleState();

    // 只要达到 100% 满档，或者 WILD 当前已被开启，开关就必须保持可见
    const bool shouldShowWild = isMaxOverdrive || isWildActive;

    wildButton.setTargetVisible (shouldShowWild);

    if (auto* wildParam = processor.getAPVTS().getRawParameterValue (ParamID::wild))
    {
        const bool w = wildParam->load() > 0.5f;
        if (w != lastWild)
        {
            applyWildTheme (w);
            if (! isExpandedView)
            {
                prismKnob.setValue (0.30f, juce::sendNotification);
                applyMacroToParams (0.30f);
            }
            else
            {
                if (auto* dp = processor.getAPVTS().getParameter (ParamID::drive))
                    dp->setValueNotifyingHost (0.30f);
            }
        }
    }

    wildClock += 1.0f / 30.0f;
    wildButton.setFlicker (0.5f + 0.5f * std::sin (wildClock * 5.3f)
                                      * std::sin (wildClock * 2.1f + 1.7f));
    wildButton.setAnimationTime (wildClock);

    // -------------------------------------------------------------------------
    // 抽屉平滑过渡插值驱动 (Drawer Slide Animation)
    // -------------------------------------------------------------------------
    if (std::abs (drawerProgress - targetDrawerProgress) > 0.002f)
    {
        drawerProgress += (targetDrawerProgress - drawerProgress) * 0.28f;
        panelLayer.setDrawerProgress (drawerProgress);
        resized();
        repaint();
    }
    else if (drawerProgress != targetDrawerProgress)
    {
        drawerProgress = targetDrawerProgress;
        panelLayer.setDrawerProgress (drawerProgress);

        if (drawerProgress == 0.0f)
        {
            for (size_t i = 1; i < knobs.size(); ++i)
            {
                knobs[i]->slider.setVisible (false);
                knobs[i]->name.setVisible (false);
                knobs[i]->value.setVisible (false);
            }
            for (size_t i = 0; i < outKnobs.size(); ++i)
            {
                outKnobs[i]->slider.setVisible (false);
                outKnobs[i]->name.setVisible (false);
                outKnobs[i]->value.setVisible (false);
            }
            linkButton.setVisible (false);
            charPicker.setVisible (false);
            presetBar.setVisible (false);
            inBar.setVisible (false);
            outBar.setVisible (false);
            matchButton.setVisible (false);
            hqButton.setVisible (false);
        }

        resized();
        repaint();
    }

    panelLayer.refreshFooter();
    pushVisualState();
}

//==============================================================================
void EZampEditor::pushVisualState()
{
    const auto& spec = processor.getSpectrum();

    const uint32_t tick = spec.getTick();
    const bool fresh = (tick != lastSpecTick);
    lastSpecTick = tick;

    if (fresh)
    {
        spec.readInto      (specBuf.data(), (int) specBuf.size());
        spec.readPeaksInto (peakBuf.data(), (int) peakBuf.size());
    }

    const float d = (float) knobs[0]->slider.getValue() / 100.0f;
    const float w = (float) knobs[1]->slider.getValue() / 100.0f;
    const float a = (float) knobs[2]->slider.getValue() / 100.0f;
    // 第四色保留：从现有染色参数合成视觉权重，不对应额外音频参数。
    const float blend = d * 0.5f + w * 0.25f + a * 0.25f;
    const float w4[4] = { d, w, a, blend };
    palette.setWeights (w4);

    spectrumLayer.setSpectrum (specBuf.data(), (int) specBuf.size(), fresh, peakBuf.data());

    const float energy = spec.getEnergy();
    const float visualEnergy = juce::jmax (energy, isExpandedView ? (d * 0.35f) : (prismKnob.getValue() * 0.45f));
    spectrumLayer.setEnergy (visualEnergy);
    particleLayer.setEnergy (visualEnergy);
    prismKnob.setSpectrum (specBuf.data(), (int) specBuf.size(), energy);

    // Meters & Bar
    const auto& chain = processor.getChain();
    inBar.setLevelDb   (chain.getInputLevelDb());
    outBar.setLevelDb  (chain.getOutputLevelDb());

    inBar.tick();
    outBar.tick();
}

} // namespace ozo
