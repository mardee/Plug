#include "PluginEditor.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

#if ! JUCE_MODAL_LOOPS_PERMITTED
 #error "Editor animation tests require JUCE_MODAL_LOOPS_PERMITTED=1 for the test and JUCE modules"
#endif

// Only deterministic fixtures stop the timer. The mounted fixture uses real JUCE dispatch.
struct NightShiftEditorTestAccess
{
    static void start (OzoGlintEditor& e, double now)
    {
        e.stopTimer(); e.lighting.stop(); e.revision = e.processor.visualStateRevision();
        e.lighting.start (now, e.enabledMask(), e.revision); e.frameTime = now;
    }
    static juce::Image paint (OzoGlintEditor& e)
    {
        juce::Image image (juce::Image::RGB, 1672, 941, true);
        juce::Graphics graphics (image); e.surface.paint (graphics); return image;
    }
    static juce::Image at (OzoGlintEditor& e, double now)
    {
        e.frameTime = now;
        e.lighting.syncEnabled (e.enabledMask(), e.processor.visualStateRevision(), now);
        e.lighting.update (now); return paint (e);
    }
    static auto& lights (OzoGlintEditor& e) { return e.lighting; }
    static auto frames (const OzoGlintEditor& e) { return e.editorFrameCount; }
    static double time (const OzoGlintEditor& e) { return e.frameTime; }
    static bool active (const OzoGlintEditor& e) { return e.sessionActive; }
    static bool pending (const OzoGlintEditor& e) { return e.hasPendingEvent; }
    static auto event (const OzoGlintEditor& e) { return e.pendingEvent; }
    static int velocity (const OzoGlintEditor& e, std::size_t lane) { return e.lastVelocity[lane]; }
};

namespace
{
using Access = NightShiftEditorTestAccess;
using Lighting = glint::NightShiftLighting;
using Grid = glint::NightShiftTimeGrid;
constexpr std::array<int, 7> centres {{ 190, 282, 369, 461, 551, 635, 725 }};
constexpr std::array<std::size_t, 7> order {{ 3, 2, 1, 0, 4, 5, 6 }};
void require (bool ok, const std::string& message) { if (! ok) throw std::runtime_error (message); }
std::string laneName (std::size_t lane) { return "engine lane " + std::to_string (lane); }
void set (OzoGlintProcessor& p, const juce::String& id, float value)
{
    auto* parameter = p.getAPVTS().getParameter (id);
    require (parameter != nullptr, "Missing parameter " + id.toStdString());
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}
void configure (OzoGlintProcessor& p, int first)
{
    set (p, "amount", .3f); set (p, "root", 0); set (p, "scale", 1); set (p, "outputMode", 1);
    for (std::size_t lane = 0; lane < 7; ++lane)
        for (const auto& field : std::array<std::pair<const char*, float>, 8> {{
            { "enabled", 1 }, { "first", float (first) }, { "repeat", 6 }, { "feedback", 0 },
            { "level", .4f }, { "gate", 0 }, { "motion", 0 }, { "cycle", 0 } }})
            set (p, OzoGlintProcessor::laneParameterID (lane, field.first), field.second);
}
juce::Rectangle<int> runway (std::size_t lane) { return { 600, centres[order[lane]] - 15, 937, 31 }; }
juce::Rectangle<int> cellCentre (std::size_t lane, int cell)
{
    // Independent design-coordinate oracle: 936 px / 64 intervals, NOT the renderer helper.
    return juce::Rectangle<int> (juce::roundToInt (600.0 + 14.625 * cell) - 5,
                                centres[order[lane]] - 4, 11, 9).getIntersection (runway (lane));
}
int changed (const juce::Image& a, const juce::Image& b, juce::Rectangle<int> roi)
{
    int count = 0;
    for (int y = roi.getY(); y < roi.getBottom(); ++y)
        for (int x = roi.getX(); x < roi.getRight(); ++x)
        {
            const auto p = a.getPixelAt (x, y), q = b.getPixelAt (x, y);
            count += std::abs (int (p.getRed()) - q.getRed()) + std::abs (int (p.getGreen()) - q.getGreen())
                   + std::abs (int (p.getBlue()) - q.getBlue()) >= 15;
        }
    return count;
}
juce::Component* find (juce::Component& parent, const juce::String& id)
{
    if (parent.getComponentID() == id) return &parent;
    for (auto* child : parent.getChildren()) if (auto* result = find (*child, id)) return result;
    return nullptr;
}
void checkControls (OzoGlintEditor& editor)
{
    auto* close = find (editor, "closeDetails");
    require (close && ! close->isShowing() && ! close->getParentComponent()->isVisible(), "Details must start hidden");
    for (std::size_t lane = 0; lane < 7; ++lane)
    {
        for (auto field : { "first", "repeat", "feedback", "level" })
        {
            auto* control = find (editor, OzoGlintProcessor::laneParameterID (lane, field));
            require (control && control->isShowing() && ! control->getBounds().isEmpty()
                     && control->getParentComponent() != close->getParentComponent(),
                     laneName (lane) + " main control missing/hidden: " + field);
        }
        for (auto field : { "gate", "motion", "cycle" })
        {
            auto* control = find (editor, OzoGlintProcessor::laneParameterID (lane, field));
            require (control && ! control->isShowing(), laneName (lane) + " expression details unexpectedly visible");
        }
    }
}
void deterministicPixels()
{
    OzoGlintProcessor processor; configure (processor, 6); OzoGlintEditor editor (processor);
    require (editor.hasAirportResources(), "Missing embedded airport/lamp resources");
    require (OzoGlintEditor::displayLaneOrder == order, "Display-to-engine lane mapping changed");
    constexpr double start = 1000;
    Access::start (editor, start);
    const auto early = Access::at (editor, start + 300), late = Access::at (editor, start + 1050);
    const auto baseline = Access::at (editor, start + 2000);
    for (std::size_t lane = 0; lane < 7; ++lane)
    {
        require (changed (early, late, runway (lane)) > 80, laneName (lane) + " welcome changed no runway pixels");
        const auto idle = Access::at (editor, start + 4000 + lane * 45 + 275);
        require (changed (baseline, idle, runway (lane)) > 60, laneName (lane) + " idle changed only text, not lamps");
    }
    const auto beforeEcho = Access::at (editor, start + 6000);
    for (std::size_t lane = 0; lane < 7; ++lane)
        require (Access::lights (editor).echo (lane, lane % 2 ? 1.0f : .6f, start + 6000, start + 6000, 1.5),
                 laneName (lane) + " deterministic echo rejected");
    const auto echo = Access::at (editor, start + 6020);
    for (std::size_t lane = 0; lane < 7; ++lane)
    {
        const auto sample = Access::lights (editor).sample (lane, start + 6020);
        require (sample.echoCount == 1 && sample.echoes[0].cell == 24, laneName (lane) + " echo cell is not onset-relative");
        require (Lighting::cellLevels (sample, 24).center == (lane % 2 ? Lighting::Level::peak : Lighting::Level::echo),
                 laneName (lane) + " echo velocity tier is not 100%/70%");
        for (int cell = 0; cell < Grid::positionCount; ++cell)
            require (cell == 24 ? changed (beforeEcho, echo, cellCentre (lane, cell)) > 8
                                : changed (beforeEcho, echo, cellCentre (lane, cell)) == 0,
                     laneName (lane) + " actual echo centre is misaligned at cell " + std::to_string (cell));
        set (processor, OzoGlintProcessor::laneParameterID (lane, "enabled"), 0);
    }
    const auto disabled = Access::at (editor, start + 6100); // Disable while echoes are still alive.
    Access::start (editor, start + 7000); // Initially all-OFF must also finish its welcome.
    const auto allOff = Access::at (editor, start + 8501);
    for (std::size_t lane = 0; lane < 7; ++lane)
    {
        const auto sample = Access::lights (editor).sample (lane, start + 8501);
        require (! sample.enabled && sample.steady == 0 && ! sample.echoActive && sample.welcomeProgress < 0,
                 laneName (lane) + " all-OFF welcome did not settle");
        for (int cell = 0; cell < Grid::positionCount; ++cell)
            require (Lighting::cellLevel (sample, cell, true) == Lighting::Level::off,
                     laneName (lane) + " disabled deterministic sample still lights a cell");
    }
    Access::lights (editor).stop(); const auto blank = Access::paint (editor);
    for (std::size_t lane = 0; lane < 7; ++lane)
    {
        require (changed (disabled, blank, runway (lane)) == 0 && changed (allOff, blank, runway (lane)) == 0,
                 laneName (lane) + " disabled lamps differ from the same unlit scene");
        require (changed (baseline, blank, runway (lane)) > 120, laneName (lane) + " enabled grid is not visibly lit");
        for (int cell = 0; cell < Grid::positionCount; ++cell)
        {
            const bool target = cell > 0 && cell % 16 == 0; // FIRST=1 beat, REPEAT=1 beat.
            const auto pixels = changed (baseline, blank, cellCentre (lane, cell));
            require (target ? pixels > 4 : pixels == 0,
                     laneName (lane) + " shared 4-beat target grid misaligned at cell " + std::to_string (cell)
                     + " changed=" + std::to_string (pixels));
        }
    }
    std::cout << "PASS in-memory runway-only welcome/idle/70%-100% echo pixels, target alignment and OFF blanking\n";
}
void pump (int milliseconds)
{
    require (juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds), "JUCE dispatch loop quit unexpectedly");
}
struct TestWindow final : juce::DocumentWindow
{
    explicit TestWindow (OzoGlintEditor& editor) : DocumentWindow ("NIGHTSHIFT animation test", juce::Colours::black, 0)
    {
        setUsingNativeTitleBar (true); editor.setSize (1254, 706);
        setContentNonOwned (&editor, true); centreWithSize (getWidth(), getHeight()); setVisible (true);
    }
    ~TestWindow() override { setVisible (false); clearContentComponent(); }
    void closeButtonPressed() override { setVisible (false); }
};
void liveMessageLoop()
{
    require (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr, "No GUI display: live editor test cannot run");
    constexpr int frames = 4096;
    OzoGlintProcessor processor; configure (processor, 0);
    for (std::size_t lane = 0; lane < 7; ++lane)
        set (processor, OzoGlintProcessor::laneParameterID (lane, "enabled"), lane == 3 ? 1.0f : 0.0f);
    processor.prepareToPlay (48000, frames);
    OzoGlintEditor editor (processor);
    const auto initialFrames = Access::frames (editor); TestWindow window (editor);
    pump (600);
    require (window.getPeer() && editor.isShowing() && Access::active (editor), "Editor visibility session did not start");
    require (Access::frames (editor) >= initialFrames + 3, "Visible editor timer did not advance");
    checkControls (editor);
    const auto before = Access::paint (editor);
    juce::AudioBuffer<float> audio (2, frames); audio.clear(); juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 74, juce::uint8 (100)), 117);
    midi.addEvent (juce::MidiMessage::noteOff (1, 74), 118);
    processor.processBlock (audio, midi);
    int generated = 0;
    for (const auto event : midi) generated += event.getMessage().isNoteOn();
    require (generated == 1, "Single-lane fixture did not emit exactly one echo");
    pump (180);
    const auto after = Access::paint (editor);
    const auto sample = Access::lights (editor).sample (3, Access::time (editor));
    require (sample.echoCount == 1 && sample.echoes[0].cell == 2 && Access::velocity (editor, 3) > 0,
             "Real echo did not reach its timed lamp cell");
    require (changed (before, after, cellCentre (3, 2)) > 4, "Only status text changed, not the echo lamp");
    window.setVisible (false); pump (40);
    require (! Access::active (editor), "Hidden editor left its visual session active");
    std::cout << "PASS limited native-window check: timer, one real echo lamp, four visible controls and close\n";
    processor.releaseResources();
}
}
int main()
{
    try
    {
        juce::ScopedJuceInitialiser_GUI gui;
        require (juce::MessageManager::getInstance()->isThisTheMessageThread(), "Test must run on the JUCE message thread");
        deterministicPixels(); liveMessageLoop(); return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
