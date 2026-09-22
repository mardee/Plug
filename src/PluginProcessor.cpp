#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace ozo
{

//==============================================================================
EZampProcessor::EZampProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    // 默认值 = Color 预设（中间档，挂上就能听出过了设备，又不至于一上来就糊）
    currentPreset = 2;
}

EZampProcessor::~EZampProcessor() {}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout EZampProcessor::createParameterLayout()
{
    using APF = juce::AudioParameterFloat;
    using APC = juce::AudioParameterChoice;
    using APB = juce::AudioParameterBool;
    using AttrsF = juce::AudioParameterFloatAttributes;
    using AttrsB = juce::AudioParameterBoolAttributes;

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto dbRange = [] (float lo, float hi) {
        return juce::NormalisableRange<float> (lo, hi, 0.1f);
    };
    auto pctRange = [] {
        return juce::NormalisableRange<float> (0.0f, 100.0f, 0.5f);
    };

    params.push_back (std::make_unique<APF> (
        juce::ParameterID { ParamID::input, 1 }, "Input", dbRange (-24.0f, 24.0f), 0.0f,
        AttrsF().withLabel ("dB")));

    params.push_back (std::make_unique<APF> (
        juce::ParameterID { ParamID::drive, 1 }, "Drive", pctRange(), 55.0f,
        AttrsF().withLabel ("%")));

    params.push_back (std::make_unique<APC> (
        juce::ParameterID { ParamID::character, 1 }, "Character",
        juce::StringArray { "Tape", "Tube", "Console" }, 0));

    params.push_back (std::make_unique<APF> (
        juce::ParameterID { ParamID::weight, 1 }, "Weight", pctRange(), 40.0f,
        AttrsF().withLabel ("%")));

    params.push_back (std::make_unique<APF> (
        juce::ParameterID { ParamID::air, 1 }, "Air", pctRange(), 45.0f,
        AttrsF().withLabel ("%")));

    params.push_back (std::make_unique<APF> (
        juce::ParameterID { ParamID::glue, 1 }, "Glue", pctRange(), 35.0f,
        AttrsF().withLabel ("%")));

    params.push_back (std::make_unique<APF> (
        juce::ParameterID { ParamID::output, 1 }, "Output", dbRange (-24.0f, 24.0f), 0.0f,
        AttrsF().withLabel ("dB")));

    params.push_back (std::make_unique<APF> (
        juce::ParameterID { ParamID::mix, 1 }, "Mix", pctRange(), 100.0f,
        AttrsF().withLabel ("%")));

    params.push_back (std::make_unique<APB> (
        juce::ParameterID { ParamID::match, 1 }, "Auto Match", true,
        AttrsB().withLabel ("")));

    params.push_back (std::make_unique<APB> (
        juce::ParameterID { ParamID::hq, 1 }, "HQ", true,
        AttrsB().withLabel ("")));

    params.push_back (std::make_unique<APB> (
        juce::ParameterID { ParamID::wild, 1 }, "Wild", false,
        AttrsB().withLabel ("")));

    return { params.begin(), params.end() };
}

//==============================================================================
bool EZampProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainIn  = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();

    if (mainIn != mainOut)
        return false;

    const int n = mainIn.size();
    return n == 1 || n == 2;   // 单声道轨与立体声轨都接得住
}

//==============================================================================
void EZampProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int numCh = juce::jmax (1, getTotalNumInputChannels());
    chain.prepare (sampleRate, samplesPerBlock, numCh);

    spectrum.prepare (sampleRate);

    // 过采样滤波器有群延迟，必须如实报告，否则宿主做延迟补偿时会错位
    setLatencySamples (chain.getLatencySamples());

    syncParamsFromAPVTS();
}

void EZampProcessor::releaseResources()
{
    chain.reset();
    spectrum.reset();
}

void EZampProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    if (buffer.getNumSamples() <= 0)
        return;

    syncParamsFromAPVTS();
    chain.process (buffer);

    // 分析染色**之后**的信号：界面上看到的频谱就是耳朵听到的那个声音，
    // 推 Drive 时频谱的变化能直接对上听觉变化。
    spectrum.push (buffer);
}

//==============================================================================
void EZampProcessor::syncParamsFromAPVTS()
{
    auto* vInput  = apvts.getRawParameterValue (ParamID::input);
    auto* vDrive  = apvts.getRawParameterValue (ParamID::drive);
    auto* vChar   = apvts.getRawParameterValue (ParamID::character);
    auto* vWeight = apvts.getRawParameterValue (ParamID::weight);
    auto* vAir    = apvts.getRawParameterValue (ParamID::air);
    auto* vGlue   = apvts.getRawParameterValue (ParamID::glue);
    auto* vOutput = apvts.getRawParameterValue (ParamID::output);
    auto* vMix    = apvts.getRawParameterValue (ParamID::mix);
    auto* vMatch  = apvts.getRawParameterValue (ParamID::match);
    auto* vHq     = apvts.getRawParameterValue (ParamID::hq);
    auto* vWild   = apvts.getRawParameterValue (ParamID::wild);

    ChainParams p;
    p.inputDb   = vInput->load();
    p.drive     = vDrive->load()  * 0.01f;
    p.character = (Character) juce::jlimit (0, 2, (int) std::lround (vChar->load()));
    p.weight    = vWeight->load() * 0.01f;
    p.air       = vAir->load()    * 0.01f;
    p.glue      = vGlue->load()   * 0.01f;
    p.outputDb  = vOutput->load();
    p.mix       = vMix->load()    * 0.01f;
    p.autoMatch = vMatch->load() > 0.5f;
    p.hq        = vHq->load()    > 0.5f;
    p.wild      = vWild->load()  > 0.5f;

    chain.setParams (p);
}

//==============================================================================
void EZampProcessor::applyPreset (int index)
{
    currentPreset = juce::jlimit (0, getNumPrograms() - 1, index);
    const Preset& p = PresetFactory::get (currentPreset);

    auto set = [this] (const juce::String& id, float v)
    {
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (v));
    };

    set (ParamID::input,     p.inputDb);
    set (ParamID::drive,     p.drive     * 100.0f);
    set (ParamID::character, (float) (int) p.character);
    set (ParamID::weight,    p.weight    * 100.0f);
    set (ParamID::air,       p.air       * 100.0f);
    set (ParamID::glue,      p.glue      * 100.0f);
    set (ParamID::output,    p.outputDb);
    set (ParamID::mix,       p.mix       * 100.0f);
    set (ParamID::match,     p.autoMatch ? 1.0f : 0.0f);
    set (ParamID::hq,        p.hq        ? 1.0f : 0.0f);
}

//==============================================================================
void EZampProcessor::setCurrentProgram (int index)
{
    applyPreset (index);
}

const juce::String EZampProcessor::getProgramName (int index)
{
    return juce::String (PresetFactory::getName (juce::jlimit (0, getNumPrograms() - 1, index)));
}

//==============================================================================
void EZampProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("presetIndex", currentPreset, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void EZampProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.isValid())
        return;

    if (state.hasProperty ("presetIndex"))
        currentPreset = juce::jlimit (0, getNumPrograms() - 1,
                                      (int) state.getProperty ("presetIndex"));

    apvts.replaceState (state);
}

//==============================================================================
juce::AudioProcessorEditor* EZampProcessor::createEditor()
{
    return new EZampEditor (*this);
}

} // namespace ozo

//==============================================================================
// JUCE 8 插件入口。VST3 / AU / Standalone 三种格式都从这里取实例。
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ozo::EZampProcessor();
}
