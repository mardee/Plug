#pragma once

#include "PluginProcessor.h"
#include "NightShiftLookAndFeel.h"
#include "NightShiftLighting.h"
#include "PresetBar.h"
#include "../presets/PresetFactory.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <memory>

class OzoGlintEditor final : public juce::AudioProcessorEditor,
                             private juce::Timer, private juce::KeyListener
{
public:
    explicit OzoGlintEditor (OzoGlintProcessor&);
    ~OzoGlintEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    bool hasAirportResources() const noexcept;
    static constexpr std::array<std::size_t, 7> displayLaneOrder {{ 3, 2, 1, 0, 4, 5, 6 }};

private:
    struct Surface final : juce::Component
    {
        explicit Surface (OzoGlintEditor& e) : owner (e) {}
        void paint (juce::Graphics& g) override { owner.paintScene (g); }
        OzoGlintEditor& owner;
    };
    struct ControlPanels final : juce::Component
    {
        explicit ControlPanels (OzoGlintEditor& e) : owner (e) { setInterceptsMouseClicks (false, true); }
        void paint (juce::Graphics& g) override { owner.paintControlPanels (g); }
        OzoGlintEditor& owner;
    };
    struct Details final : juce::Component
    {
        explicit Details (OzoGlintEditor& e) : owner (e) {}
        void paint (juce::Graphics&) override;
        OzoGlintEditor& owner;
    };
    struct RunwaySlider final : juce::Slider
    {
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;
        double valueToProportionOfLength (double) override;
        double proportionOfLengthToValue (double) override;
        std::function<void (bool)> select;
        bool selected = false;
    };
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    struct LaneRow
    {
        juce::ToggleButton enabled;
        juce::Slider first, repeat, feedback, level, gate, motion, cycle;
        juce::ToggleButton detailEnabled;
        juce::Slider detailFirst, detailRepeat, detailFeedback, detailLevel;
        juce::TextButton detail;
        RunwaySlider runway;
        std::unique_ptr<ButtonAttachment> enabledAttachment;
        std::unique_ptr<SliderAttachment> firstAttachment, repeatAttachment, feedbackAttachment, levelAttachment,
                                          gateAttachment, motionAttachment, cycleAttachment, runwayAttachment;
        std::unique_ptr<ButtonAttachment> detailEnabledAttachment;
        std::unique_ptr<SliderAttachment> detailFirstAttachment, detailRepeatAttachment,
                                          detailFeedbackAttachment, detailLevelAttachment;
    };

    void timerCallback() override;
    void paintScene (juce::Graphics&);
    void paintControlPanels (juce::Graphics&);
    void updatePanelOpacity (double now);
    void restorePanelOpacity (double now);
    bool keyPressed (const juce::KeyPress&, juce::Component*) override;
    void paintLights (juce::Graphics&, double now);
    void buildLampSprites();
    void updateLampAfterglow (double now);
    void updateKnobTooltip (juce::Slider&, juce::Point<int> screenPosition);
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void selectLane (std::size_t engineIndex, bool showDetails);
    void setupSlider (juce::Slider&, const juce::String& id, const juce::String& title,
                      juce::Component&, juce::Colour accent, int focus);
    void setupCombo (juce::ComboBox&, const juce::String& id, const juce::String& title,
                     juce::Component&, int focus);
    void syncLighting (double now);
    void updateVisibilitySession();
    void refreshPresetBar();
    void loadPreset (int index);
    void promptSavePreset();
    void savePreset (const juce::String& name, bool overwrite);
    void promptDeletePreset (int index);
    void showPresetMessage (const juce::String& message);
    std::uint8_t enabledMask() const noexcept;

    OzoGlintProcessor& processor;
    NightShiftLookAndFeel lookAndFeel;
    Surface surface { *this };
    ControlPanels controlPanels { *this };
    Details details { *this };
    juce::TooltipWindow tooltips { this, 650 };
    juce::Image airport, welcomeLights;
    // The padded lamp cache leaves room for tier-specific outer bloom.
    std::array<std::array<juce::Image, 5>, 7> lampSprites;
    std::array<std::array<std::array<juce::Image, 5>, 65>, 7> groundLightSprites;
    std::array<LaneRow, 7> rows;
    std::array<std::atomic<float>*, 7> enabledParameters {};
    juce::Slider amount;
    juce::ComboBox root, scale, output;
    ozo::PresetBar presetBar;
    juce::TextButton closeDetails { "CLOSE" };
    std::unique_ptr<juce::AlertWindow> presetDialog;
    int lastPresetIndex = -1;
    juce::Label presetStatus, brandTitle, brandSubtitle, knobTooltip;
    juce::Slider* activeKnob = nullptr;
    std::unique_ptr<SliderAttachment> amountAttachment;
    std::unique_ptr<ComboAttachment> rootAttachment, scaleAttachment, outputAttachment;
    glint::NightShiftLighting lighting;
    std::array<float, 7> lightBoost {};
    struct LampAfterglow { float edge = 0, center = 0; };
    std::array<std::array<LampAfterglow, 65>, 7> lampAfterglow {};
    std::array<bool, 7> previousLampEnabled {};
    double lastLampUpdate = 0;
    std::array<int, 7> lastVelocity {};
    std::array<double, 7> lastOnsetBeats {};
    std::uint64_t editorFrameCount = 0;
    friend struct NightShiftEditorTestAccess;
    std::size_t selectedLane = 0;
    bool sessionActive = false;
    std::uint64_t revision = 0;
    OzoGlintProcessor::LaneVisualEvent pendingEvent {};
    bool hasPendingEvent = false;
    double frameTime = 0;
    double presetMessageUntil = 0;
    juce::Point<int> lastMousePosition;
    double lastPanelActivity = 0, lastPanelUpdate = 0;
    float panelOpacity = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OzoGlintEditor)
};
