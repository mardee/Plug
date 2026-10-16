#include "UserPresets.h"
#include <algorithm>

namespace ozo
{
UserPresets::UserPresets (juce::File directory, Validator check) : root (directory), validator (std::move (check)) { refresh(); }
juce::File UserPresets::defaultDirectory()
{
    auto directory = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    directory = directory.getChildFile ("Application Support");
   #endif
    return directory.getChildFile ("ozo/NIGHTSHIFT/Presets");
}
bool UserPresets::validName (const juce::String& name)
{
    if (name.isEmpty() || name.getNumBytesAsUTF8() > 80 || name != name.trim() || name.startsWithChar ('.')
        || name.endsWithChar ('.') || name.contains ("..") || name.containsAnyOf ("/\\:*?\"<>|")) return false;
    for (auto c : name) if (c < 32 || (c >= 127 && c <= 159)) return false;
    return true;
}
bool UserPresets::safeFile (const juce::File& file) const
{
    return file.getParentDirectory() == root && ! file.isSymbolicLink() && validName (file.getFileNameWithoutExtension());
}
juce::String UserPresets::lockName() const { return "ozo.NIGHTSHIFT.Presets." + juce::String::toHexString (root.getFullPathName().toLowerCase().hashCode64()); }
bool UserPresets::validXml (const juce::String& text) const
{
    if (text.isEmpty() || text.getNumBytesAsUTF8() > kMaxBytes) return false;
    if (validator) return validator (text); // Processor reuses its existing bounded parser and v6 validator.
    if (text.contains ("<!") || text.containsChar ('&')) return false;
    int tags = 0;
    for (auto c : text) if (c == '<' && ++tags > 128) return false; // Bound recursion even without a callback.
    const auto xml = juce::parseXML (text);
    const auto* params = xml ? xml->getChildByName ("PARAMETERS") : nullptr;
    return xml && xml->hasTagName ("OZO_MIDI_DELAY_STATE") && xml->getIntAttribute ("version") == 6
        && params && params->getNumChildElements() == 60; // Actual load always goes through processor import.
}
int UserPresets::indexOfName (const juce::String& name) const
{
    for (int i = 0; i < size(); ++i) if (entries[static_cast<size_t> (i)].name.equalsIgnoreCase (name)) return i;
    return -1;
}
juce::Result UserPresets::refresh()
{
    if (root.isSymbolicLink()) return juce::Result::fail ("Preset directory must not be a symbolic link.");
    if (! root.exists()) { entries.clear(); return juce::Result::ok(); }
    if (! root.isDirectory() || ! root.hasReadAccess()) return juce::Result::fail ("Cannot read preset directory.");
    std::vector<Entry> next;
    for (const auto& file : root.findChildFiles (juce::File::findFiles, false, "*"))
    {
        if (! file.hasFileExtension ("xml") || ! safeFile (file) || file.getSize() <= 0 || file.getSize() > kMaxBytes) continue;
        juce::FileInputStream input (file);
        juce::MemoryBlock data;
        input.readIntoMemoryBlock (data, kMaxBytes + 1);
        if (input.getStatus().failed() || data.getSize() == 0 || data.getSize() > kMaxBytes) continue;
        const auto* bytes = static_cast<const char*> (data.getData());
        if (std::find (bytes, bytes + data.getSize(), '\0') != bytes + data.getSize()
            || ! juce::CharPointer_UTF8::isValidString (bytes, static_cast<int> (data.getSize()))) continue;
        auto xml = juce::String::fromUTF8 (bytes, static_cast<int> (data.getSize()));
        if (validXml (xml)) next.push_back ({file.getFileNameWithoutExtension(), std::move (xml), file});
    }
    std::sort (next.begin(), next.end(), [] (const Entry& a, const Entry& b)
    { const auto order = a.name.compareIgnoreCase (b.name); return order != 0 ? order < 0 : a.name < b.name; });
    if (next.size() > kMaxCount) next.resize (kMaxCount);
    entries = std::move (next);
    return juce::Result::ok();
}
juce::Result UserPresets::save (const juce::String& name, const juce::String& xml, bool overwrite)
{
    if (! validName (name)) return juce::Result::fail ("Invalid preset name: use 1-80 UTF-8 bytes, no path/control characters, '..', or outer spaces/dots.");
    if (! validXml (xml)) return juce::Result::fail ("Invalid or oversized NIGHTSHIFT v6 XML.");
    juce::InterProcessLock lock (lockName());
    if (! lock.enter (0)) return juce::Result::fail ("Preset store is busy; try again.");
    const auto scanned = refresh();
    if (scanned.failed()) return scanned;
    const auto existing = indexOfName (name);
    if (existing >= 0 && ! overwrite) return juce::Result::fail ("Preset already exists: " + name);
    if (existing < 0 && size() >= kMaxCount) return juce::Result::fail ("User preset limit reached (32).");
    const auto target = existing >= 0 ? get (existing).file : root.getChildFile (name + ".xml");
    if (! safeFile (target) || target.isDirectory()) return juce::Result::fail ("Unsafe preset file location.");
    if (target.exists() && ! overwrite) return juce::Result::fail ("Preset already exists: " + name);
    const auto created = root.createDirectory();
    if (created.failed()) return created;
    juce::TemporaryFile temporary (target, juce::TemporaryFile::useHiddenFile);
    {
        juce::FileOutputStream output (temporary.getFile());
        if (output.getStatus().failed()) return output.getStatus();
        if (! output.write (xml.toRawUTF8(), xml.getNumBytesAsUTF8())) return juce::Result::fail ("Cannot write temporary preset: " + output.getStatus().getErrorMessage());
        output.flush();
        if (output.getStatus().failed()) return output.getStatus();
    }
    if (root.isSymbolicLink() || ! safeFile (target)) return juce::Result::fail ("Preset location changed during save.");
    if (target.exists() && ! overwrite) return juce::Result::fail ("Preset already exists: " + name);
    if (! temporary.overwriteTargetFileWithTemporary()) return juce::Result::fail ("Cannot replace preset file: " + target.getFullPathName());
    return refresh();
}
juce::Result UserPresets::remove (int index)
{
    if (index < 0 || index >= size()) return juce::Result::fail ("Unknown user preset.");
    juce::InterProcessLock lock (lockName());
    if (! lock.enter (0)) return juce::Result::fail ("Preset store is busy; try again.");
    const auto file = get (index).file;
    if (root.isSymbolicLink() || ! safeFile (file) || ! file.existsAsFile()) return juce::Result::fail ("Preset file is missing or unsafe.");
    if (! file.deleteFile()) return juce::Result::fail ("Cannot delete preset file: " + file.getFullPathName());
    return refresh();
}
}
