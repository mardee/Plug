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
    userPresets.refresh();
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
    // 出厂预设在前，用户预设紧随其后
    ChainParams params;
    const int userIndex = index - PresetFactory::kFactoryCount;

    if (index >= 0 && index < PresetFactory::kFactoryCount)
        PresetFactory::applyToParams (index, params);
    else if (userIndex >= 0 && userIndex < userPresets.size())
        params = userPresets.get (userIndex).params;
    else
        return;

    currentPreset.store (index);

    auto set = [this] (const juce::String& id, float v)
    {
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (v));
    };

    set (ParamID::input,     params.inputDb);
    set (ParamID::drive,     params.drive     * 100.0f);
    set (ParamID::character, (float) (int) params.character);
    set (ParamID::weight,    params.weight    * 100.0f);
    set (ParamID::air,       params.air       * 100.0f);
    set (ParamID::output,    params.outputDb);
    set (ParamID::mix,       params.mix       * 100.0f);
    set (ParamID::match,     params.autoMatch ? 1.0f : 0.0f);
    set (ParamID::hq,        params.hq        ? 1.0f : 0.0f);
    set (ParamID::wild,      params.wild      ? 1.0f : 0.0f);
}

ChainParams EZampProcessor::captureParams() const
{
    // 从 APVTS 读，而不是从 SignalChain 读：平滑还没走完时链里的值是旧的。
    auto raw = [this] (const juce::String& id, float fallback)
    {
        auto* p = apvts.getRawParameterValue (id);
        return p != nullptr ? p->load() : fallback;
    };

    ChainParams params;
    params.inputDb   = raw (ParamID::input,  0.0f);
    params.drive     = raw (ParamID::drive,  35.0f) * 0.01f;
    params.weight    = raw (ParamID::weight, 40.0f) * 0.01f;
    params.air       = raw (ParamID::air,    35.0f) * 0.01f;
    params.outputDb  = raw (ParamID::output, 0.0f);
    params.mix       = raw (ParamID::mix,    100.0f) * 0.01f;
    params.autoMatch = raw (ParamID::match,  1.0f) > 0.5f;
    params.hq        = raw (ParamID::hq,     1.0f) > 0.5f;
    params.wild      = raw (ParamID::wild,   0.0f) > 0.5f;
    params.character = (Character) juce::jlimit (0, 2, (int) std::lround (raw (ParamID::character, 0.0f)));

    return params;
}

bool EZampProcessor::saveUserPreset (const juce::String& name)
{
    if (! userPresets.save (name, captureParams()))
        return false;

    // 存完即选中，这样预设条立刻显示这个名字
    const int idx = userPresets.indexOfName (name);
    if (idx >= 0)
        currentPreset.store (PresetFactory::kFactoryCount + idx);

    return true;
}

bool EZampProcessor::deleteUserPreset (int index)
{
    const int userIndex = index - PresetFactory::kFactoryCount;

    if (! userPresets.remove (userIndex))
        return false;

    currentPreset.store (-1);
    return true;
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
    state.setProperty ("presetIndex", currentPreset.load(), nullptr);

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
    {
        const int idx = (int) state.getProperty ("presetIndex");

        // 用户预设的下标跨会话不稳定（别人的电脑上没有这些文件），
        // 所以只恢复出厂预设的选中态，其余一律当作"手动调过"。
        currentPreset.store ((idx >= 0 && idx < PresetFactory::kFactoryCount) ? idx : -1);
    }

    // Strip the retired parameter from old sessions without changing surviving IDs.
    state.removeProperty ("glue", nullptr);
    for (int i = state.getNumChildren(); --i >= 0;)
        if (state.getChild (i).getProperty ("id").toString() == "glue")
            state.removeChild (i, nullptr);

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
