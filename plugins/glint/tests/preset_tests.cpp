#include "PluginProcessor.h"
#include <iostream>
#include <stdexcept>

namespace
{
void require (bool condition, const char* message) { if (! condition) throw std::runtime_error (message); }
struct TemporaryDirectory
{
    juce::File file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nightshift-presets-" + juce::Uuid().toString());
    ~TemporaryDirectory() { file.deleteRecursively(); }
};
}
int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    try
    {
        TemporaryDirectory temporary;
        OzoGlintProcessor processor;
        const auto original = processor.exportSettingsText();
        auto changed = juce::parseXML (original);
        for (auto* parameter : changed->getChildByName ("PARAMETERS")->getChildIterator()) parameter->setAttribute ("value", 0);
        for (int i = 0; i < ozo::PresetFactory::kFactoryCount; ++i)
        {
            const auto text = ozo::PresetFactory::createXml (i, original);
            const auto xml = juce::parseXML (text);
            require (xml && xml->getNumAttributes() == 2 && xml->getIntAttribute ("version") == 6
                && xml->getChildByName ("PARAMETERS")->getNumChildElements() == 60, "Incomplete v6 factory state");
            require (xml->getStringAttribute ("seed") == changed->getStringAttribute ("seed"), "Factory changed seed");
            require (text == ozo::PresetFactory::createXml (i, changed->toString()), "Factory inherited current controls");
            require (processor.importSettingsText (text), "Processor rejected factory XML");
            require (processor.applyPreset (i).wasOk() && processor.getPresetIndex() == i, "Factory selection lost in callbacks");
            require (processor.getAPVTS().getRawParameterValue ("scale")->load() != 0, "Harmonic factory key is unset");
        }
        auto* amount = processor.getAPVTS().getParameter ("amount");
        amount->setValueNotifyingHost (0.17f);
        require (processor.getPresetIndex() == -1, "Parameter automation did not clear selection");
        require (processor.applyPreset (0).wasOk(), "Cannot reselect factory");
        juce::MemoryBlock state;
        processor.getStateInformation (state);
        processor.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        require (processor.getPresetIndex() == -1, "Host state restore retained stale selection");
        require (processor.deleteUserPreset (0).failed(), "Deleted a factory preset");

        ozo::UserPresets users (temporary.file);
        require (users.size() == 0 && ! temporary.file.exists(), "Constructor wrote preset directory");
        require (users.save ("../escape", original).failed() && users.save ("bad\nname", original).failed(), "Unsafe name accepted");
        require (users.save ("Bad", "<!DOCTYPE x><x/>").failed()
            && users.save ("Bad", juce::String::repeatedString ("x", 65537)).failed(), "Unsafe XML accepted");
        require (! temporary.file.exists(), "Rejected save created directory");
        require (users.save ("Zulu", original).wasOk(), "User save failed");
        require (users.save ("zULu", original).getErrorMessage().startsWith ("Preset already exists: "), "Missing duplicate conflict");
        const auto replacement = ozo::PresetFactory::createXml (1, original);
        require (users.save ("zULu", replacement, true).wasOk(), "Confirmed overwrite failed");
        require (users.save ("Alpha", original).wasOk(), "Second save failed");
        ozo::UserPresets reopened (temporary.file);
        require (reopened.size() == 2 && reopened.get (0).name == "Alpha" && reopened.get (1).name == "Zulu", "User sorting failed");
        require (reopened.get (1).xml == replacement && processor.importSettingsText (reopened.get (1).xml), "Saved v6 XML import failed");
        require (reopened.remove (reopened.indexOfName ("Zulu")).wasOk()
            && ! temporary.file.getChildFile ("Zulu.xml").exists(), "User deletion failed");
        std::cout << "PASS six v6 factories, processor selection lifecycle, user save/load/overwrite/delete\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}
