#include "PresetFactory.h"

namespace ozo
{
namespace
{
    // ---------------------------------------------------------------------
    // 预设按"染色有多狠"排，不按乐器或总线分。
    // 理由：乐器/总线是用法，不是音色；把两者绑在一起只会让人先选错框，
    // 再在错框里调不出来的参数。这里一排到底，从左往右越来越猛。
    //
    // 注意 name 必须是 ASCII —— juce::String(const char*) 按 Latin-1 解释字节，
    // 中文经 String 往返会散架，按钮文字会变乱码。
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
        0.22f,      // glue
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
        0.30f,
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
        0.35f,
        0.0f,
        1.0f,
        true, true
    };

    // Grit：谐波开始咬人，中频顶到前面
    const Preset gritPreset
    {
        "Grit", "harmonics bite",
        0.0f,
        0.72f,
        Character::Console,
        0.48f,
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
        0.55f,
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
        0.65f,
        0.0f,
        1.0f,
        true, true
    };

    const Preset* const allPresets[] =
    {
        &subtlePreset, &warmPreset,  &colorPreset,
        &gritPreset,   &crushPreset, &destroyPreset
    };
}

const Preset& PresetFactory::get (int index)
{
    return *allPresets [(size_t) juce::jlimit (0, getNumPresets() - 1, index)];
}

const char* PresetFactory::getName (int index)
{
    return get (index).name;
}

const char* PresetFactory::getHint (int index)
{
    return get (index).hint;
}

void PresetFactory::applyToParams (int index, ChainParams& out)
{
    const Preset& p = get (index);
    out.inputDb   = p.inputDb;
    out.drive     = p.drive;
    out.character = p.character;
    out.weight    = p.weight;
    out.air       = p.air;
    out.glue      = p.glue;
    out.outputDb  = p.outputDb;
    out.mix       = p.mix;
    out.autoMatch = p.autoMatch;
    out.hq        = p.hq;
}

} // namespace ozo
