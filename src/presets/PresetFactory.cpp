#include "PresetFactory.h"

namespace ozo
{
namespace
{
    // ---------------------------------------------------------------------
    // 出厂预设两组，组内按"染色有多狠"排，不按乐器或总线分。
    // 前五个常规，后五个狂野。狂野不是"常规再加一个开关"——
    // 波形折叠、次八度和毛刺是另一套声音，每个都按狂野重新配过参数。
    //
    // 前六个的下标和旧版本对齐（Subtle..Destroy），旧工程加载不会串档。
    // 注意 name 必须是 ASCII —— juce::String(const char*) 按 Latin-1 解释字节，
    // 中文经 String 往返会散架。
    // ---------------------------------------------------------------------

    // Subtle：只把数字味的毛边磨掉，不改变动态
    const Preset subtlePreset
    {
        "Subtle", "barely there",
        0.0f,       // inputDb
        0.16f,      // drive
        Character::Tape,
        0.35f,      // weight
        0.45f,      // air
        0.0f,       // outputDb
        1.0f,
        true, true
    };

    // Warm：偶次谐波开始明显，声音"厚"起来
    const Preset warmPreset
    {
        "Warm", "even-order glow",
        0.0f,
        0.34f,
        Character::Tube,
        0.42f,
        0.40f,
        0.0f,
        1.0f,
        true, true
    };

    // Color：默认档。挂上就能听出"过了设备"
    const Preset colorPreset
    {
        "Color", "the default go-to",
        0.0f,
        0.55f,
        Character::Tape,
        0.40f,
        0.45f,
        0.0f,
        1.0f,
        true, true
    };

    // Crush：峰值被压平，密度上来了
    const Preset crushPreset
    {
        "Crush", "peaks flattened",
        0.0f,
        0.88f,
        Character::Tube,
        0.55f,
        0.35f,
        0.0f,
        1.0f,
        true, true
    };

    // Destroy：不要怕失真。这一档就是拿来听它烂掉的
    const Preset destroyPreset
    {
        "Destroy", "no safety net",
        0.0f,
        1.00f,
        Character::Console,
        0.62f,
        0.30f,
        0.0f,
        1.0f,
        true, true
    };

    // -----------------------------------------------------------------
    // 狂野五档。wild 打开之后各级参数已经加倍，所以这里的 drive 起点
    // 比常规低一截，否则第一档就过头了。
    // -----------------------------------------------------------------

    // Spark：狂野只露一角，折叠刚能听见
    const Preset sparkPreset
    {
        "Spark", "wild, just a taste",
        0.0f,
        0.30f,
        Character::Tape,
        0.34f,
        0.42f,
        0.0f,
        1.0f,
        true, true,
        true        // wild
    };

    // Fold：波形折叠成为主体，谐波开始翻上来
    const Preset foldPreset
    {
        "Fold", "the fold takes over",
        0.0f,
        0.50f,
        Character::Tube,
        0.46f,
        0.36f,
        0.0f,
        1.0f,
        true, true,
        true
    };

    // Octave：次八度压到最前，低频往下掉一截
    const Preset octavePreset
    {
        "Octave", "sub octave up front",
        0.0f,
        0.66f,
        Character::Console,
        0.62f,
        0.30f,
        0.0f,
        1.0f,
        true, true,
        true
    };

    // Scorch：毛刺和折叠一起上，中高频发焦
    const Preset scorchPreset
    {
        "Scorch", "grit on top of fold",
        0.0f,
        0.84f,
        Character::Tube,
        0.55f,
        0.46f,
        0.0f,
        1.0f,
        true, true,
        true
    };

    // Melt：全部拉满。这一档不是拿来用的，是拿来听它化掉的
    const Preset meltPreset
    {
        "Melt", "everything, all the way",
        0.0f,
        1.00f,
        Character::Console,
        0.70f,
        0.40f,
        0.0f,
        1.0f,
        true, true,
        true
    };

    const Preset* const allPresets[] =
    {
        &subtlePreset, &warmPreset,   &colorPreset, &crushPreset,
        &destroyPreset,
        &sparkPreset,  &foldPreset,   &octavePreset, &scorchPreset, &meltPreset
    };

    static_assert (std::size (allPresets) == (size_t) PresetFactory::kFactoryCount,
                   "factory preset count out of sync");
}

const Preset& PresetFactory::get (int index)
{
    return *allPresets [(size_t) juce::jlimit (0, kFactoryCount - 1, index)];
}

const char* PresetFactory::getName (int index)
{
    return get (index).name;
}

const char* PresetFactory::getHint (int index)
{
    return get (index).hint;
}

bool PresetFactory::isWild (int index)
{
    return get (index).wild;
}

void PresetFactory::applyToParams (int index, ChainParams& out)
{
    const Preset& p = get (index);
    out.inputDb   = p.inputDb;
    out.drive     = p.drive;
    out.character = p.character;
    out.weight    = p.weight;
    out.air       = p.air;
    out.outputDb  = p.outputDb;
    out.mix       = p.mix;
    out.autoMatch = p.autoMatch;
    out.hq        = p.hq;
    out.wild      = p.wild;
}

} // namespace ozo
