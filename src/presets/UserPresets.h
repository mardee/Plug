#pragma once

#include <juce_core/juce_core.h>
#include "PresetFactory.h"

namespace ozo
{

//==============================================================================
// 用户自己存的预设。
//
// 一个预设一个 XML 文件，放在
//   ~/Library/Application Support/ozo/EZamp/presets/
// 不写进 DAW 工程。工程里只记当前的全部参数值，所以换电脑打开工程声音不变，
// 只是预设名字对不上时显示成未命名。
//
// 上限 kMaxCount 个。再多列表就不好看了，也没人真的需要更多。
class UserPresets
{
public:
    static constexpr int kMaxCount = 32;

    struct Entry
    {
        juce::String name;
        ChainParams  params;
        juce::File   file;     // 对应的 XML，删除时用
    };

    // 扫一遍目录。构造时调一次，增删之后再调。
    void refresh();

    int size() const noexcept { return (int) entries.size(); }
    const Entry& get (int index) const { return entries[(size_t) index]; }

    // 按名字存。重名就覆盖那个文件。满了返回 false。
    bool save (const juce::String& name, const ChainParams& params);

    // 删掉第 index 个。出厂预设不在这里，删不掉。
    bool remove (int index);

    // 名字是否和已有的重复（大小写不敏感）
    int indexOfName (const juce::String& name) const;

private:
    static juce::File directory();
    static juce::String sanitise (const juce::String& name);

    std::vector<Entry> entries;
};

} // namespace ozo
