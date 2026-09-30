#include "UserPresets.h"

namespace ozo
{

juce::File UserPresets::directory()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("ozo")
                   .getChildFile ("PRISM")
                   .getChildFile ("presets");

    dir.createDirectory();
    return dir;
}

juce::String UserPresets::sanitise (const juce::String& name)
{
    // 文件名里不能有路径分隔符，其余保留——名字是用户起的，别替他改太多
    auto s = name.trim().substring (0, 40);

    for (auto& bad : { "/", "\\", ":", "*" })
        s = s.replace (bad, "-");

    return s.isEmpty() ? juce::String ("Untitled") : s;
}

int UserPresets::indexOfName (const juce::String& name) const
{
    const auto wanted = sanitise (name);

    for (int i = 0; i < size(); ++i)
        if (entries[(size_t) i].name.equalsIgnoreCase (wanted))
            return i;

    return -1;
}

//==============================================================================
void UserPresets::refresh()
{
    entries.clear();

    for (const auto& f : directory().findChildFiles (juce::File::findFiles, false, "*.xml"))
    {
        auto xml = juce::XmlDocument::parse (f);
        if (xml == nullptr || (! xml->hasTagName ("PrismPreset") && ! xml->hasTagName ("EZampPreset")))
            continue;

        Entry e;
        e.file = f;
        e.name = xml->getStringAttribute ("name", f.getFileNameWithoutExtension());

        // Unknown attributes from older presets are intentionally ignored.
        auto& p = e.params;
        p.inputDb   = (float) xml->getDoubleAttribute ("input",   0.0);
        p.drive     = (float) xml->getDoubleAttribute ("drive",   0.35);
        p.weight    = (float) xml->getDoubleAttribute ("weight",  0.40);
        p.air       = (float) xml->getDoubleAttribute ("air",     0.35);
        p.outputDb  = (float) xml->getDoubleAttribute ("output",  0.0);
        p.mix       = (float) xml->getDoubleAttribute ("mix",     1.0);
        p.autoMatch = xml->getBoolAttribute ("match", true);
        p.hq        = xml->getBoolAttribute ("hq",    true);
        p.wild      = xml->getBoolAttribute ("wild",  false);
        p.character = (Character) juce::jlimit (0, 2, xml->getIntAttribute ("character", 0));

        entries.push_back (std::move (e));

        if ((int) entries.size() >= kMaxCount)
            break;
    }

    std::sort (entries.begin(), entries.end(),
               [] (const Entry& a, const Entry& b) { return a.name.compareIgnoreCase (b.name) < 0; });
}

//==============================================================================
bool UserPresets::save (const juce::String& name, const ChainParams& params)
{
    const auto clean = sanitise (name);

    // 重名：覆盖已有的那个文件，不另存一份
    juce::File target;

    if (const int existing = indexOfName (clean); existing >= 0)
        target = entries[(size_t) existing].file;
    else if (size() >= kMaxCount)
        return false;
    else
        target = directory().getChildFile (clean + ".xml");

    juce::XmlElement xml ("PrismPreset");
    xml.setAttribute ("name",      clean);
    xml.setAttribute ("input",     params.inputDb);
    xml.setAttribute ("drive",     params.drive);
    xml.setAttribute ("character", (int) params.character);
    xml.setAttribute ("weight",    params.weight);
    xml.setAttribute ("air",       params.air);
    xml.setAttribute ("output",    params.outputDb);
    xml.setAttribute ("mix",       params.mix);
    xml.setAttribute ("match",     params.autoMatch);
    xml.setAttribute ("hq",        params.hq);
    xml.setAttribute ("wild",      params.wild);

    const bool ok = xml.writeTo (target);
    if (ok)
        refresh();

    return ok;
}

bool UserPresets::remove (int index)
{
    if (index < 0 || index >= size())
        return false;

    const bool ok = entries[(size_t) index].file.deleteFile();
    if (ok)
        refresh();

    return ok;
}

} // namespace ozo
