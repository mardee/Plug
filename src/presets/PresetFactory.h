#pragma once

#include "dsp/SignalChain.h"

namespace ozo
{

//==============================================================================
// 预设。每个预设都是"这一路信号该有的样子"，而不是把旋钮随便拨一组数字。
// 注意 drive / weight / air 全部是 0..1 的归一化值，
// 它们的真实物理量由 SignalChain::applyParams 按 Character 换算。
struct Preset
{
    const char* name       = "Init";
    const char* hint       = "";
    float       inputDb    = 0.0f;
    float       drive      = 0.35f;
    Character   character  = Character::Tape;
    float       weight     = 0.40f;
    float       air        = 0.35f;
    float       outputDb   = 0.0f;
    float       mix        = 1.0f;
    bool        autoMatch  = true;
    bool        hq         = true;
    bool        wild       = false;
};

class PresetFactory
{
public:
    // 出厂预设：前 5 个常规、后 5 个狂野。顺序不能改 ——
    // 旧工程里存的 presetIndex 按这个下标解释。
    static constexpr int kFactoryCount = 10;
    static constexpr int kWildStart    = 5;   // 后五个是狂野档

    static const Preset& get (int index);
    static const char*   getName (int index);
    static const char*   getHint (int index);
    static bool          isWild (int index);

    static void applyToParams (int index, ChainParams& out);
};

} // namespace ozo
