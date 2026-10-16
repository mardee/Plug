#include "PresetFactory.h"
#include <algorithm>

namespace ozo
{
namespace
{
constexpr Preset presets[] {
    {"Init - Clear Apron", "C major / a clear octave", false, {.30,0,0,1}, {8,6,6,.35,.38,4,.05,1}},
    {"Night Taxi", "A minor / moving lights", false, {.44,0,9,2}, {15,2,4,.56,.42,2,.26,1}},
    {"Glass Canopy", "D major / suspended harmony", false, {.38,0,2,1}, {14,5,6,.73,.36,5,.38,2}},
    {"Red Eye", "E minor / quick departures", false, {.56,0,4,2}, {75,0,2,.62,.44,1,.72,0}},
    {"Slow Orbit", "G minor / long arcs", false, {.42,0,7,2}, {122,7,8,.86,.28,8,.50,3}},
    {"Last Departure", "F major / particles only", false, {.48,1,5,1}, {108,6,7,.80,.32,7,.28,3}}
};
static_assert (std::size (presets) == PresetFactory::kFactoryCount);
}
const Preset& PresetFactory::get (int index) { return presets[juce::jlimit (0, kFactoryCount - 1, index)]; }
const char* PresetFactory::getName (int index) { return get (index).name; }
const char* PresetFactory::getHint (int index) { return get (index).hint; }
bool PresetFactory::isWild (int index) { return get (index).wild; }

juce::String PresetFactory::createXml (int index, const juce::String& templateXml)
{
    if (index < 0 || index >= kFactoryCount || templateXml.getNumBytesAsUTF8() > 65536) return {};
    auto xml = juce::parseXML (templateXml); // Trusted export, not file input. Processor validates the result.
    if (! xml || ! xml->hasTagName ("OZO_MIDI_DELAY_STATE") || xml->getIntAttribute ("version") != 6) return {};
    auto* parameters = xml->getChildByName ("PARAMETERS");
    if (! parameters || parameters->getNumChildElements() != 60) return {};
    const auto& preset = get (index);
    constexpr const char* globalIds[] {"amount", "outputMode", "root", "scale"};
    constexpr const char* fields[] {"enabled", "first", "repeat", "feedback", "level", "gate", "motion", "cycle"};
    constexpr int firstOffsets[] {0,1,2,0,2,1,3}, repeatOffsets[] {0,0,1,0,1,0,1};
    constexpr double levelScale[] {.65,.90,.90,1,.65,.65,.70};
    for (auto* parameter : parameters->getChildIterator())
    {
        const auto id = parameter->getStringAttribute ("id");
        bool found = false;
        for (int i = 0; i < 4; ++i) if (id == globalIds[i])
        { parameter->setAttribute ("value", preset.globals[static_cast<size_t> (i)]); found = true; }
        for (int lane = 0; lane < 7 && ! found; ++lane)
            for (int field = 0; field < 8; ++field)
                if (id == "lane" + juce::String (lane) + "_" + fields[field])
                {
                    double value = preset.lanes[static_cast<size_t> (field)];
                    if (field == 0) value = (static_cast<int> (value) & (1 << lane)) != 0 ? 1.0 : 0.0;
                    if (field == 1) value = std::min (9.0, value + firstOffsets[lane]);
                    if (field == 2) value = std::min (9.0, value + repeatOffsets[lane]);
                    if (field == 4) value *= levelScale[lane];
                    parameter->setAttribute ("value", value);
                    found = true;
                }
        if (! found) return {};
    }
    return xml->toString(); // Root type, v6 and seed remain unchanged.
}
}
