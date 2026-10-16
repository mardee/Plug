#pragma once
#include <juce_core/juce_core.h>
#include <array>

namespace ozo
{
struct Preset
{
    const char* name;
    const char* hint;
    bool wild;
    std::array<double, 4> globals; // Amount, output, root, scale.
    std::array<double, 8> lanes;   // Enabled mask, first, repeat, feedback, level, gate, motion, cycle.
};

class PresetFactory
{
public:
    static constexpr int kFactoryCount = 6;
    static const Preset& get (int index);
    static const char* getName (int index);
    static const char* getHint (int index);
    static bool isWild (int index);
    // templateXml must be the processor's current export; all 60 values are reset.
    static juce::String createXml (int index, const juce::String& templateXml);
};
}
