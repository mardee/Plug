#pragma once
#include <juce_core/juce_core.h>
#include <functional>
#include <vector>

namespace ozo
{
// PRISM-style file catalogue, with full NIGHTSHIFT state XML instead of audio ChainParams.
// Message thread only; constructing/refreshing never creates the directory.
class UserPresets
{
public:
    static constexpr int kMaxCount = 32;
    static constexpr int kMaxBytes = 64 * 1024;
    using Validator = std::function<bool (const juce::String&)>;
    struct Entry { juce::String name, xml; juce::File file; };
    explicit UserPresets (juce::File directory = defaultDirectory(), Validator validator = {});
    static juce::File defaultDirectory();
    juce::Result refresh();
    int size() const noexcept { return static_cast<int> (entries.size()); }
    const Entry& get (int index) const { return entries[static_cast<size_t> (index)]; }
    int indexOfName (const juce::String& name) const;
    juce::Result save (const juce::String& name, const juce::String& xml, bool overwrite = false);
    juce::Result remove (int index);
private:
    static bool validName (const juce::String&);
    bool validXml (const juce::String&) const;
    bool safeFile (const juce::File&) const;
    juce::String lockName() const;
    juce::File root;
    Validator validator;
    std::vector<Entry> entries;
};
}
