#pragma once

#include "dsp/SignalChain.h"

namespace ozo
{

//==============================================================================
// 预设。每个预设都是"这一路信号该有的样子"，而不是把旋钮随便拨一组数字。
// 注意 drive / weight / air / glue 全部是 0..1 的归一化值，
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
    float       glue       = 0.30f;
    float       outputDb   = 0.0f;
    float       mix        = 1.0f;
    bool        autoMatch  = true;
    bool        hq         = true;
};

class PresetFactory
{
public:
    static constexpr int getNumPresets() { return 6; }

    static const Preset& get (int index);
    static const char*   getName (int index);
    static const char*   getHint (int index);

    static void applyToParams (int index, ChainParams& out);
};

} // namespace ozo
