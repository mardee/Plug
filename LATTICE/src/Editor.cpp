#include "Processor.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <array>
#include <cmath>

namespace {
// Wireframe-exact Studio Flat Color Palette
const juce::Colour bgMain(0xff121118);
const juce::Colour bgCard(0xff1a1824);
const juce::Colour bgPanel(0xff222030);
const juce::Colour bgHover(0xff2d2a40);
const juce::Colour borderSubtle(0xff2f2b42);
const juce::Colour borderStrong(0xff453f60);
const juce::Colour textPrimary(0xfff0edf6);
const juce::Colour textMuted(0xff8c859d);
const juce::Colour textDim(0xff5e5770);
const juce::Colour accentPurple(0xffa277ff);
const juce::Colour accentPink(0xffff4d6d);
const juce::Colour accentCyan(0xff38d9a9);
const juce::Colour accentAmber(0xffffbe3b);

constexpr std::array<float,3> developmentValues{0.f,.45f,.85f},fillValues{0.f,.5f,1.f},spaceValues{0.f,.35f,.7f},densityValues{.25f,.55f,.85f};
constexpr std::array<int,3> syncopationValues{0,1,2};
const char* const tierNames[3]={"LOW","MID","HIGH"};
const char* const tierSuffixes[3]={"-low","-mid","-high"};

// Neon Palette matching Wireframe
const juce::Colour colours[8]={
    juce::Colour(0xffff4d6d), // Kick - Pink
    juce::Colour(0xffff7556), // Snare - Coral
    juce::Colour(0xffffbe3b), // Low Tom - Amber
    juce::Colour(0xff20e3b2), // Hat - Mint
    juce::Colour(0xff38bdf8), // Open - Cyan
    juce::Colour(0xffc084fc), // Tom - Violet
    juce::Colour(0xffa3e635), // Ride - Lime
    juce::Colour(0xfff472b6)  // Crash - Rose
};

struct Style: juce::LookAndFeel_V4 {
    Style() {
        setColour(juce::TextButton::buttonColourId, bgPanel);
        setColour(juce::TextButton::textColourOffId, textPrimary);
        setColour(juce::TextButton::buttonOnColourId, accentPurple);
        setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        setColour(juce::ComboBox::backgroundColourId, bgPanel);
        setColour(juce::ComboBox::textColourId, textPrimary);
        setColour(juce::ComboBox::outlineColourId, borderSubtle);
        setColour(juce::PopupMenu::backgroundColourId, bgCard);
        setColour(juce::PopupMenu::textColourId, textPrimary);
        setColour(juce::PopupMenu::headerTextColourId, textMuted);
        setColour(juce::TextEditor::backgroundColourId, bgPanel);
        setColour(juce::TextEditor::textColourId, textPrimary);
        setColour(juce::TextEditor::outlineColourId, borderSubtle);
        setColour(juce::Slider::thumbColourId, accentPurple);
        setColour(juce::Slider::trackColourId, bgMain);
        setColour(juce::Slider::backgroundColourId, bgPanel);
        setColour(juce::Slider::textBoxBackgroundColourId, bgPanel);
        setColour(juce::Slider::textBoxTextColourId, textPrimary);
        setColour(juce::Slider::textBoxOutlineColourId, borderSubtle);
        setColour(juce::Label::backgroundColourId, bgPanel);
        setColour(juce::Label::textColourId, textPrimary);
        setColour(juce::Label::textWhenEditingColourId, textPrimary);
        setColour(juce::TextEditor::highlightColourId, accentPurple.withAlpha(0.4f));
        setColour(juce::TextEditor::highlightedTextColourId, juce::Colours::white);
        setColour(juce::ToggleButton::textColourId, textPrimary);
        setColour(juce::ToggleButton::tickColourId, accentPurple);
    }
    juce::Font getTextButtonFont(juce::TextButton& b, int) override {
        if (b.getButtonText() == "M" || b.getButtonText() == "S" || b.getButtonText() == "L")
            return juce::Font(juce::FontOptions(10.f, juce::Font::bold));
        if (b.getButtonText() == "/3" || b.getButtonText() == "/6" || b.getButtonText() == "16" || b.getButtonText() == "X")
            return juce::Font(juce::FontOptions(11.f, juce::Font::bold));
        if (b.getButtonText() == "GENERATE" || b.getButtonText() == "GENERATE / 16 BARS")
            return juce::Font(juce::FontOptions(14.f, juce::Font::bold));
        if (b.getButtonText() == "DEVELOP BAR 1" || b.getButtonText() == "DEVELOP FROM BAR 1")
            return juce::Font(juce::FontOptions(12.5f, juce::Font::bold));
        return juce::Font(juce::FontOptions(12.f, juce::Font::bold));
    }
    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour& c, bool over, bool down) override {
        auto bounds = b.getLocalBounds().toFloat().reduced(0.5f);
        bool isToggled = b.getToggleState();
        juce::Colour fillCol = isToggled ? b.findColour(juce::TextButton::buttonOnColourId) : c;
        if (down) fillCol = fillCol.withMultipliedBrightness(0.85f);
        else if (over && !isToggled) fillCol = bgHover;
        if (!b.isEnabled()) fillCol = fillCol.withMultipliedAlpha(0.3f);

        g.setColour(fillCol);
        g.fillRoundedRectangle(bounds, 4.f);

        if (isToggled) {
            g.setColour(fillCol.brighter(0.25f));
            g.drawRoundedRectangle(bounds, 4.f, 1.2f);
        } else {
            g.setColour(over ? borderStrong : borderSubtle);
            g.drawRoundedRectangle(bounds, 4.f, 0.8f);
        }
    }
    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) override {
        g.setFont(getTextButtonFont(b, b.getHeight()));
        bool isToggled = b.getToggleState();
        juce::Colour textCol = isToggled ? b.findColour(juce::TextButton::textColourOnId)
                                         : b.findColour(juce::TextButton::textColourOffId);
        if (!b.isEnabled()) textCol = textCol.withAlpha(0.3f);
        g.setColour(textCol);
        g.drawText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, true);
    }
    void drawLabel(juce::Graphics& g, juce::Label& l) override {
        g.fillAll(l.findColour(juce::Label::backgroundColourId));
        if (l.isBeingEdited()) return;
        g.setColour(textPrimary);
        g.setFont(juce::Font(juce::FontOptions(13.f, juce::Font::plain)));
        auto a = l.getBorderSize().subtractedFrom(l.getLocalBounds());
        g.drawText(l.getText(), a, l.getJustificationType(), false);
    }
    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                      int, int, int, int, juce::ComboBox& box) override {
        auto cornerSize = 4.0f;
        juce::Rectangle<int> boxBounds(0, 0, width, height);
        g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle(boxBounds.toFloat(), cornerSize);
        g.setColour(box.findColour(juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle(boxBounds.toFloat().reduced(0.5f), cornerSize, 1.0f);

        juce::Rectangle<int> arrowZone(width - 20, 0, 16, height);
        juce::Path path;
        path.startNewSubPath((float)arrowZone.getX() + 3.0f, (float)arrowZone.getCentreY() - 2.0f);
        path.lineTo((float)arrowZone.getCentreX(), (float)arrowZone.getCentreY() + 3.0f);
        path.lineTo((float)arrowZone.getRight() - 3.0f, (float)arrowZone.getCentreY() - 2.0f);

        g.setColour(textMuted.withAlpha((isButtonDown) ? 0.9f : 0.6f));
        g.strokePath(path, juce::PathStrokeType(1.5f));
    }
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float, float,
                          const juce::Slider::SliderStyle style, juce::Slider&) override {
        if (style == juce::Slider::LinearVertical) {
            auto trackWidth = 8.0f;
            auto trackX = (float)x + ((float)width - trackWidth) * 0.5f;
            auto trackY = (float)y + 4.0f;
            auto trackH = (float)height - 8.0f;

            // Simple Flat Minimalist Fader Track
            g.setColour(bgMain);
            g.fillRoundedRectangle(trackX, trackY, trackWidth, trackH, 3.0f);
            g.setColour(borderSubtle);
            g.drawRoundedRectangle(trackX, trackY, trackWidth, trackH, 3.0f, 0.8f);

            // Clean Solid Fader Block Thumb matching Wireframe
            auto thumbW = 34.0f;
            auto thumbH = 16.0f;
            auto thumbX = (float)x + ((float)width - thumbW) * 0.5f;
            auto thumbY = sliderPos - thumbH * 0.5f;

            g.setColour(accentPurple);
            g.fillRoundedRectangle(thumbX, thumbY, thumbW, thumbH, 3.0f);
            g.setColour(accentPurple.brighter(0.2f));
            g.drawRoundedRectangle(thumbX, thumbY, thumbW, thumbH, 3.0f, 1.0f);
        } else {
            // Horizontal Tempo Slider
            auto trackH = 4.0f;
            auto trackY = (float)y + ((float)height - trackH) * 0.5f;
            g.setColour(bgMain);
            g.fillRoundedRectangle((float)x, trackY, (float)width, trackH, 2.0f);
            g.setColour(borderSubtle);
            g.drawRoundedRectangle((float)x, trackY, (float)width, trackH, 2.0f, 0.8f);

            auto thumbSize = 14.0f;
            g.setColour(accentPurple);
            g.fillEllipse(sliderPos - thumbSize * 0.5f, (float)y + ((float)height - thumbSize) * 0.5f, thumbSize, thumbSize);
        }
    }
};

void identify(juce::Component& component, const juce::String& id) {
    component.setComponentID(id);
    component.setName(id);
}

int groupingTarget(const lattice::Pattern& pattern) {
    if (pattern.numerator < 1 || pattern.numerator > 16
        || (pattern.denominator != 4 && pattern.denominator != 8)) return 0;
    return pattern.numerator * 8 / pattern.denominator;
}

bool validGrouping(const std::vector<int>& groups, int target) {
    if (target <= 0 || groups.empty() || groups.size() > 32) return false;
    int total = 0;
    for (int value : groups) {
        if (value < 1 || value > target || total > target - value) return false;
        total += value;
    }
    return total == target;
}

int editorBarTicks(const lattice::Pattern& pattern) {
    const int target = groupingTarget(pattern);
    return target > 0 ? target * 240 : 1920;
}

const char* trackDisplayName(int row) {
    return row == 2 ? "Tom2" : row == 5 ? "Tom1" : lattice::names[row];
}

juce::String groupingText(const std::vector<int>& groups) {
    juce::StringArray parts;
    for (int value : groups) parts.add(juce::String(value));
    return parts.joinIntoString("+");
}

// 草稿浮层由 Editor 直接持有；隐藏而非在 onClick 内销毁，便于同步调用和安全关闭。
class GroupDraft final: public juce::Component {
    juce::Label title, total, message;
    juce::TextEditor input;
    juce::ComboBox selection;
    juce::TextButton split{"SPLIT"}, merge{"MERGE NEXT"}, apply{"APPLY"}, cancel{"CANCEL"};
    std::array<juce::TextButton, 3> presets;
    int target = 0, selected = 0;

    bool parse(std::vector<int>& result, int& sum) const {
        result.clear();
        sum = 0;
        const auto source = input.getText().trim();
        if (source.isEmpty() || source.length() > 128 || source.startsWithChar('+') || source.endsWithChar('+')
            || source.removeCharacters(" \t\r\n").contains("++")) return false;
        // 保留空字段，拒绝 3++2、尾随加号及超长整数，避免 getIntValue 溢出。
        const auto parts = juce::StringArray::fromTokens(source, "+", "");
        for (auto part : parts) {
            part = part.trim();
            if (part.isEmpty() || part.length() > 2 || !part.containsOnly("0123456789")) return false;
            const int value = part.getIntValue();
            if (value < 1 || value > 32 || result.size() >= 32) return false;
            result.push_back(value);
            sum += value;
        }
        return !result.empty();
    }

    void refresh() {
        std::vector<int> groups;
        int sum = 0;
        const bool parsed = parse(groups, sum);
        const bool valid = parsed && validGrouping(groups, target);
        total.setText("Total " + (parsed ? juce::String(sum) : juce::String("?"))
                      + " / " + juce::String(target) + " eighth notes", juce::dontSendNotification);
        message.setText(target == 0 ? "Invalid meter. Cancel and choose a valid meter."
                        : !parsed ? "Use positive integers joined by +, e.g. 3+2+2."
                        : !valid ? "Total must match the meter. Nothing has been applied."
                                 : "Draft only. Apply to save; Cancel to discard.", juce::dontSendNotification);
        selection.clear(juce::dontSendNotification);
        if (parsed) {
            selected = juce::jlimit(0, int(groups.size()) - 1, selected);
            for (int i = 0; i < int(groups.size()); ++i)
                selection.addItem("Group " + juce::String(i + 1) + " : " + juce::String(groups[i]), i + 1);
            selection.setSelectedId(selected + 1, juce::dontSendNotification);
        }
        selection.setEnabled(parsed);
        split.setEnabled(valid && groups[selected] > 1);
        merge.setEnabled(valid && selected + 1 < int(groups.size()));
        apply.setEnabled(valid);
    }

    void transform(bool splitting) {
        std::vector<int> groups;
        if (!readValid(groups) || selected < 0 || selected >= int(groups.size())) return;
        if (splitting) {
            if (groups[selected] < 2) return;
            const int first = (groups[selected] + 1) / 2;
            const int second = groups[selected] - first;
            groups[selected] = first;
            groups.insert(groups.begin() + selected + 1, second);
        } else {
            if (selected + 1 >= int(groups.size())) return;
            groups[selected] += groups[selected + 1];
            groups.erase(groups.begin() + selected + 1);
        }
        setDraft(groupingText(groups));
    }

public:
    std::function<void()> onApply, onCancel;

    GroupDraft() {
        identify(*this, "group-editor");
        for (auto* label : {&title, &total, &message}) {
            addAndMakeVisible(*label);
            label->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        }
        identify(title, "group-title");
        identify(total, "group-total");
        identify(message, "group-message");
        identify(input, "group-draft");
        identify(selection, "group-selection");
        identify(split, "group-split");
        identify(merge, "group-merge-next");
        identify(apply, "group-apply");
        identify(cancel, "group-cancel");
        title.setText("EDIT GROUPS / eighth-note units", juce::dontSendNotification);
        input.setMultiLine(false);
        input.setInputRestrictions(128);
        input.onTextChange = [this] { refresh(); };
        input.onReturnKey = [this] { apply.onClick(); };
        input.onEscapeKey = [this] { cancel.onClick(); };
        addAndMakeVisible(input);
        addAndMakeVisible(selection);
        selection.onChange = [this] { selected = selection.getSelectedId() - 1; refresh(); };
        for (auto* button : {&split, &merge, &apply, &cancel}) addAndMakeVisible(*button);
        split.setTooltip("Split the selected group into two near-equal parts; total stays unchanged.");
        merge.setTooltip("Merge the selected group with its right neighbour; total stays unchanged.");
        split.onClick = [this] { transform(true); };
        merge.onClick = [this] { transform(false); };
        apply.onClick = [this] {
            std::vector<int> groups;
            if (!isVisible()) return;
            if (!readValid(groups)) { refresh(); return; }
            if (onApply) onApply();
        };
        cancel.onClick = [this] { if (isVisible() && onCancel) onCancel(); };
        apply.setColour(juce::TextButton::buttonColourId, accentPurple);
        for (int i = 0; i < int(presets.size()); ++i) {
            identify(presets[i], "group-preset-" + juce::String(i + 1));
            addAndMakeVisible(presets[i]);
            presets[i].onClick = [this, i] {
                if (presets[i].isVisible()) setDraft(presets[i].getButtonText());
            };
        }
    }

    void begin(int eighths, const std::vector<int>& groups, int index) {
        target = eighths;
        selected = std::max(0, index);
        juce::StringArray choices;
        if (target == 7) choices = {"3+2+2", "2+2+3", "2+3+2"};
        else if (target == 10) choices = {"6+4", "4+6"};
        else if (target == 8) choices = {"3+3+2", "4+4", "2+2+2+2"};
        else if (target > 0) choices.add(juce::String(target));
        for (int i = 0; i < int(presets.size()); ++i) {
            presets[i].setVisible(i < choices.size());
            presets[i].setButtonText(i < choices.size() ? choices[i] : juce::String());
        }
        setDraft(groupingText(groups));
    }

    void selectGroup(int index) {
        if (index >= 0) selected = index;
        refresh();
    }

    void setDraft(const juce::String& value) {
        input.setText(value, false);
        refresh();
    }

    bool readValid(std::vector<int>& groups) const {
        int sum = 0;
        return parse(groups, sum) && validGrouping(groups, target);
    }

    void showConflict() {
        message.setText("Meter/groups changed externally. Cancel and reopen.", juce::dontSendNotification);
    }

    void resized() override {
        title.setBounds(16, 10, 408, 24);
        input.setBounds(16, 40, 408, 30);
        total.setBounds(16, 76, 408, 24);
        for (int i = 0; i < int(presets.size()); ++i) presets[i].setBounds(16 + i * 138, 108, 132, 26);
        selection.setBounds(16, 148, 154, 28);
        split.setBounds(180, 148, 92, 28);
        merge.setBounds(282, 148, 142, 28);
        message.setBounds(16, 184, 408, 24);
        cancel.setBounds(218, 220, 98, 30);
        apply.setBounds(326, 220, 98, 30);
    }

    void paint(juce::Graphics& g) override {
        g.setColour(bgCard);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 8.f);
        g.setColour(borderStrong);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.f), 8.f, 2.f);
    }
};

class Editor final: public juce::AudioProcessorEditor, private juce::Timer, public juce::DragAndDropContainer {
    LatticeProcessor& p;
    Style style;

    // Top Header / Transport
    juce::TextButton play{"PLAY"}, reroll{"REROLL"}, exportButton{"DRAG MIDI"}, saveButton{"SAVE .MID"}, saveWavButton{"SAVE WAV"};

    // 16 Bars Strip & Actions
    std::array<juce::TextButton,16> bars16;
    juce::ToggleButton barLock{"LOCK"}, follow{"FOLLOW"};
    juce::TextButton variate{"VARIATE"}, copyBar{"COPY >"};
    juce::TextButton grid16{"1/16"}, grid32{"1/32"};

    // Track Controls: Mute, Solo, Lock
    std::array<juce::TextButton,8> locks;
    std::array<juce::TextButton,8> mutes;
    std::array<juce::TextButton,8> solos;
    std::array<bool,8> trackMuted{};
    std::array<bool,8> trackSoloed{};

    // Floating Subdivision Action Popover
    juce::TextButton divide3{"/3"}, divide6{"/6"}, restore{"16"}, off{"X"};

    // Bottom Rack: Meter & Presets
    juce::ComboBox meter;
    juce::TextEditor numerator, seedEdit;
    juce::TextButton groupEdit;
    juce::ComboBox denominator;
    juce::TextButton addGroup{"EDIT"};
    juce::TextButton preset7_1{"3+2+2"}, preset7_2{"2+2+3"}, preset7_3{"2+3+2"};
    juce::TextButton preset5_1{"6+4"}, preset5_2{"4+6"};
    std::array<juce::TextButton, 32> groupSegments;
    juce::Label groupStatus;
    GroupDraft groupDraft;
    int draftNumerator = 0, draftDenominator = 0;
    std::vector<int> draftOriginalGroups;

    // 5 Real Vertical Sliders
    juce::Slider devSlider, fillSlider, spaceSlider, densitySlider, syncopSlider;

    // Backward compatibility invisible 15 tier buttons for existing unit tests
    std::array<juce::TextButton,3> developmentButtons, fillButtons, spaceButtons, densityButtons, syncopationButtons;

    juce::Slider tempo;
    juce::ToggleButton ghost{"Ghost notes"}, accent{"Group accents"}, backbeatToggle{"Backbeat Pulse"};
    juce::TextButton generate{"GENERATE"}, developSource{"DEVELOP BAR 1"};
    std::unique_ptr<juce::FileChooser> chooser, wavChooser;
    bool wavDialogPending = false;
    LatticeProcessor::WavExportState shownWavState = LatticeProcessor::WavExportState::idle;
    juce::String shownWavMessage;

    int bar = 0, selectedRow = -1, anchor = -1, endCell = -1;
    bool dragging = false, hasDraggedVelocity = false;
    juce::Point<int> down;
    int dragInitialVelocity = 0;
    juce::String status = "Ready. Set meter and grouping, then push Generate or Develop.";

    // Top-Bottom Layout Geometry: Full-width sequencer on Top (980px grid width)
    juce::Rectangle<int> grid{120, 118, 980, 276};
    juce::Rectangle<int> grouping{120, 90, 980, 20};
    juce::TooltipWindow tooltips{this, 500};

    struct ExportDrag: juce::MouseListener {
        Editor& e; bool done = false;
        ExportDrag(Editor& x): e(x) {}
        void mouseDown(const juce::MouseEvent&) override { done = false; }
        void mouseDrag(const juce::MouseEvent& ev) override {
            if (!done && ev.getDistanceFromDragStart() > 5) {
                done = true;
                auto f = e.p.exportMidi();
                if (f.existsAsFile())
                    juce::DragAndDropContainer::performExternalDragDropOfFiles({f.getFullPathName()}, false, &e);
                else
                    e.status = "MIDI export failed.";
            }
        }
    } dragListener{*this};

    void text(juce::Graphics& g, juce::String s, juce::Rectangle<int> r, float size = 13, juce::Colour c = textPrimary, int flags = juce::Justification::centredLeft) {
        g.setColour(c);
        g.setFont(juce::Font(juce::FontOptions(size, juce::Font::plain)));
        g.drawText(s, r, flags);
    }

    void syncBarControls() {
        juce::ScopedLock lock(p.modelLock);
        auto& m = p.pattern;
        bool sourceHasNotes = false;
        for (const auto& lane : m.bars[0])
            for (const auto& cell : lane)
                if (cell.velocity > 0) sourceHasNotes = true;

        developSource.setEnabled(sourceHasNotes);
        developSource.setTooltip(sourceHasNotes ? "Develop bars 2-16 from bar 1. Source and locks stay unchanged."
                                               : "Add notes to bar 1 to enable development.");
        variate.setEnabled(!m.barLocked[bar]);
        variate.setTooltip(m.barLocked[bar] ? "Unlock this bar to variate."
                                            : "Variate this bar only. Locked tracks stay unchanged.");
        bool canCopy = bar < 15 && !m.barLocked[bar + 1];
        copyBar.setEnabled(canCopy);
        copyBar.setTooltip(bar == 15 ? "Bar 16 has no next bar."
                                     : canCopy ? "Copy to the next bar. Locked tracks stay unchanged."
                                               : "Unlock the next bar to copy into it.");
        for (int b = 0; b < 16; ++b) {
            bars16[b].setToggleState(b == bar, juce::dontSendNotification);
            bars16[b].setTooltip("Bar " + juce::String(b + 1) + (m.barLocked[b] ? " / Locked" : ""));
        }
        barLock.setToggleState(m.barLocked[bar], juce::dontSendNotification);
        barLock.setButtonText("LOCK");
    }

    void commit() {
        p.changed();
        syncBarControls();
        repaint();
    }

    void clearSelection() {
        selectedRow = anchor = endCell = -1;
        updateSelection();
    }

    void updateSelection() {
        bool on = selectedRow >= 0 && anchor >= 0 && endCell >= 0;
        if (on) {
            juce::ScopedLock lock(p.modelLock);
            auto& lane = p.pattern.bars[bar][selectedRow];
            int a = std::min(anchor, endCell);
            int z = std::max(anchor, endCell);
            if (a >= 0 && z < int(lane.size())) {
                const float scale = float(grid.getWidth()) / editorBarTicks(p.pattern);
                float x1 = grid.getX() + lane[a].start * scale;
                float x2 = grid.getX() + (lane[z].start + lane[z].length) * scale;
                int centerX = int((x1 + x2) * 0.5f);
                int centerY = grid.getY() + selectedRow * 34 - 12;
                if (centerY < grid.getY() + 8) centerY = grid.getY() + selectedRow * 34 + 38;

                int startX = std::clamp(centerX - 100, grid.getX() + 5, grid.getRight() - 210);
                divide3.setBounds(startX, centerY, 44, 20);
                divide6.setBounds(startX + 48, centerY, 44, 20);
                restore.setBounds(startX + 96, centerY, 48, 20);
                off.setBounds(startX + 148, centerY, 40, 20);
            }
        }
        for (auto* b : {&divide3, &divide6, &restore, &off})
            b->setVisible(on);
        repaint();
    }

    void subdivision(int n) {
        if (selectedRow < 0) return;
        {
            juce::ScopedLock lock(p.modelLock);
            bool ok = p.pattern.subdivide(bar, selectedRow, std::min(anchor, endCell), std::max(anchor, endCell), n);
            status = ok ? "Timing preserved. Click or drag up/down on notes to adjust velocities."
                        : "Subdivision refused: incompatible duration or existing off-grid notes. Clear those notes first.";
        }
        clearSelection();
        commit();
    }

    std::pair<int,int> snapHit(juce::Point<int> pt) {
        auto h = hit(pt);
        if (h.first < 0) return h;
        juce::ScopedLock lock(p.modelLock);
        auto& lane = p.pattern.bars[bar][h.first];
        int t = int(double(pt.x - grid.getX()) / grid.getWidth() * editorBarTicks(p.pattern));
        auto cell = lane[h.second];
        if (cell.start % 60 != 0 || cell.length % 60 != 0) return h;
        int step = p.pattern.gridStep;
        int snap = (t / step) * step;
        if (cell.velocity && cell.length == 60) return h;
        for (int i = 0; i < int(lane.size()); ++i) {
            if (snap >= lane[i].start && snap < lane[i].start + lane[i].length) {
                auto c = lane[i];
                if (c.length > step && c.start % step == 0 && c.length % step == 0) {
                    int count = c.length / step;
                    lane.erase(lane.begin() + i);
                    for (int k = 0; k < count; ++k)
                        lane.insert(lane.begin() + i + k, {c.start + k * step, step, k == 0 ? c.velocity : 0});
                    return {h.first, i + (snap - c.start) / step};
                }
                return {h.first, i};
            }
        }
        return h;
    }

    void setMeter() {
        if (groupDraft.isVisible()) {
            status = "Apply or cancel the group draft before changing meter.";
            sync();
            repaint();
            return;
        }
        if (meter.getSelectedId() == 4) {
            numerator.setVisible(true);
            denominator.setVisible(true);
            updateGroupingPresets();
            return;
        }
        int n = meter.getSelectedId() == 2 ? 7 : meter.getSelectedId() == 3 ? 5 : 4;
        int d = meter.getSelectedId() == 2 ? 8 : 4;
        applyMeter(n, d);
        numerator.setVisible(false);
        denominator.setVisible(false);
        updateGroupingPresets();
    }

    void updateGroupingPresets() {
        juce::ScopedLock lock(p.modelLock);
        bool is78 = p.pattern.numerator == 7 && p.pattern.denominator == 8;
        bool is54 = p.pattern.numerator == 5 && p.pattern.denominator == 4;

        preset7_1.setVisible(is78);
        preset7_2.setVisible(is78);
        preset7_3.setVisible(is78);
        preset5_1.setVisible(is54);
        preset5_2.setVisible(is54);
    }

    void applyMeter(int n, int d) {
        juce::ScopedLock lock(p.modelLock);
        if (p.pattern.numerator == n && p.pattern.denominator == d) return;
        if (groupDraft.isVisible()) {
            status = "Apply or cancel the group draft before changing meter.";
            sync();
            repaint();
            return;
        }
        p.pattern.meter(n, d);
        for (auto& b : locks) b.setToggleState(false, juce::dontSendNotification);
        barLock.setToggleState(false, juce::dontSendNotification);
        for (auto& b : bars16) b.setToggleState(false, juce::dontSendNotification);
        syncGroupingControls();
        status = "Meter changed: pattern cleared, locks reset. Choose grouping.";
        clearSelection();
        commit();
    }

    juce::Rectangle<int> groupStripBounds() const {
        return grouping;
    }

    void syncGroupingControls() {
        juce::ScopedLock lock(p.modelLock);
        const auto& m = p.pattern;
        const int target = groupingTarget(m);
        const bool valid = validGrouping(m.groups, target);
        groupEdit.setButtonText(valid ? groupingText(m.groups) : "FIX GROUPS");
        groupStatus.setText(valid ? juce::String(m.numerator) + "/" + juce::String(m.denominator) + "  VALID"
                                 : "INVALID", juce::dontSendNotification);
        const auto strip = groupStripBounds();
        int start = 0;
        for (int i = 0; i < int(groupSegments.size()); ++i) {
            auto& button = groupSegments[i];
            button.setVisible(valid ? i < int(m.groups.size()) : i == 0);
            if (!button.isVisible()) continue;
            if (valid) {
                const int left = strip.getX() + strip.getWidth() * start / target;
                start += m.groups[i];
                const int right = strip.getX() + strip.getWidth() * start / target;
                button.setBounds(left + 1, strip.getY(), std::max(1, right - left - 2), strip.getHeight());
                button.setButtonText(juce::String(m.groups[i]));
                button.setTooltip("Group " + juce::String(i + 1) + ": " + juce::String(m.groups[i]) + " eighth notes. Click to edit draft.");
            } else {
                button.setBounds(strip);
                button.setButtonText("Invalid grouping - click to repair");
                button.setTooltip("Invalid external grouping. Editing does not change the model until Apply.");
            }
        }
    }

    void openGroupEditor(int index = 0, const juce::String& preset = {}) {
        clearSelection();
        if (!groupDraft.isVisible()) {
            juce::ScopedLock lock(p.modelLock);
            draftNumerator = p.pattern.numerator;
            draftDenominator = p.pattern.denominator;
            draftOriginalGroups = p.pattern.groups;
            groupDraft.begin(groupingTarget(p.pattern), draftOriginalGroups, index);
            groupDraft.setVisible(true);
        } else {
            groupDraft.selectGroup(index);
        }
        if (preset.isNotEmpty()) groupDraft.setDraft(preset);
        groupDraft.toFront(true);
        status = "Editing group draft. Apply or Cancel; outside clicks do not discard it.";
        repaint();
    }

    void applyGroupDraft() {
        if (!groupDraft.isVisible()) return;
        std::vector<int> groups;
        if (!groupDraft.readValid(groups)) return;
        {
            juce::ScopedLock lock(p.modelLock);
            // 拍号或分组被宿主/其他编辑器修改时，禁止用旧草稿覆盖新状态。
            if (p.pattern.numerator != draftNumerator || p.pattern.denominator != draftDenominator
                || p.pattern.groups != draftOriginalGroups) {
                groupDraft.showConflict();
                return;
            }
            if (!validGrouping(groups, groupingTarget(p.pattern))) return;
            p.pattern.groups = groups;
        }
        groupDraft.setVisible(false);
        syncGroupingControls();
        status = "Grouping applied. Notes unchanged.";
        commit();
    }

    bool groupingReady() {
        if (groupDraft.isVisible()) {
            status = "Apply or cancel the group draft before generating or developing.";
            groupDraft.toFront(true);
            repaint();
            return false;
        }
        juce::ScopedLock lock(p.modelLock);
        if (!validGrouping(p.pattern.groups, groupingTarget(p.pattern))) {
            status = "Invalid grouping. Open EDIT GROUPS and apply a valid draft first.";
            repaint();
            return false;
        }
        return true;
    }

    template<typename T>
    void setupTiers(std::array<juce::TextButton,3>& buttons, const juce::String& name, T lattice::Pattern::* member, const std::array<T,3>& values, int) {
        for (int i = 0; i < 3; ++i) {
            auto& b = buttons[i];
            b.setName(name + tierSuffixes[i]);
            b.setButtonText(tierNames[i]);
            b.setClickingTogglesState(false);
            b.setVisible(false); // keep invisible for tests compatibility
            b.onClick = [this, member, target = values[i]] {
                juce::ScopedLock lock(p.modelLock);
                p.pattern.*member = target;
                syncParameterControls();
                repaint();
            };
            addChildComponent(b);
        }
    }

    template<typename T>
    void syncTiers(std::array<juce::TextButton,3>& buttons, T current, const std::array<T,3>& values, const juce::String& meaning) {
        int nearest = 0;
        for (int i = 1; i < 3; ++i)
            if (std::abs(double(current) - values[i]) < std::abs(double(current) - values[nearest]))
                nearest = i;
        for (int i = 0; i < 3; ++i) {
            buttons[i].setToggleState(i == nearest, juce::dontSendNotification);
            buttons[i].setTooltip(meaning + " Current value: " + juce::String(double(current), 6) + ". " + tierNames[i] + " target: " + juce::String(double(values[i]), 2) + ".");
        }
    }

    void setupFader(juce::Slider& s, double minV, double maxV, double defV, std::function<void(double)> onChange) {
        s.setSliderStyle(juce::Slider::LinearVertical);
        s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s.setRange(minV, maxV, 0.01);
        s.setValue(defV, juce::dontSendNotification);
        s.onValueChange = [this, &s, onChange] {
            onChange(s.getValue());
            syncParameterControls();
            repaint();
        };
        addAndMakeVisible(s);
    }

    void syncParameterControls() {
        const auto& m = p.pattern;
        devSlider.setValue(m.development, juce::dontSendNotification);
        fillSlider.setValue(m.fill, juce::dontSendNotification);
        spaceSlider.setValue(m.space, juce::dontSendNotification);
        densitySlider.setValue(m.density, juce::dontSendNotification);
        syncopSlider.setValue(m.dispersion, juce::dontSendNotification);

        syncTiers(developmentButtons, m.development, developmentValues, "Development");
        syncTiers(fillButtons, m.fill, fillValues, "Fill");
        syncTiers(spaceButtons, m.space, spaceValues, "Space");
        syncTiers(densityButtons, m.density, densityValues, "Density");
        syncopationButtons[0].setToggleState(m.dispersion == 0, juce::dontSendNotification);
        syncopationButtons[1].setToggleState(m.dispersion == 1, juce::dontSendNotification);
        syncopationButtons[2].setToggleState(m.dispersion == 2, juce::dontSendNotification);
    }

    void sync() {
        juce::ScopedLock lock(p.modelLock);
        auto& m = p.pattern;
        tempo.setValue(m.bpm, juce::dontSendNotification);
        syncParameterControls();
        ghost.setToggleState(m.ghost, juce::dontSendNotification);
        accent.setToggleState(m.accents, juce::dontSendNotification);
        seedEdit.setText(juce::String(m.seed), false);
        syncGroupingControls();
        int id = m.numerator == 4 && m.denominator == 4 ? 1 : m.numerator == 7 && m.denominator == 8 ? 2 : m.numerator == 5 && m.denominator == 4 ? 3 : 4;
        meter.setSelectedId(id, juce::dontSendNotification);
        numerator.setText(juce::String(m.numerator), false);
        denominator.setSelectedId(m.denominator, juce::dontSendNotification);
        numerator.setVisible(id == 4);
        denominator.setVisible(id == 4);
        for (int r = 0; r < 8; ++r) locks[r].setToggleState(m.locked[r], juce::dontSendNotification);
        syncBarControls();
        grid16.setToggleState(m.gridStep == 120, juce::dontSendNotification);
        grid32.setToggleState(m.gridStep == 60, juce::dontSendNotification);
        backbeatToggle.setToggleState(m.backbeat, juce::dontSendNotification);
        updateGroupingPresets();
    }

    void beginWavExport(const juce::File& destination) {
        wavDialogPending = false;
        const bool started = p.startWavExport(destination);
        status = started ? "WAV export started. " + p.getWavExportResult().message
                        : p.isWavExporting() ? "A WAV export is already running."
                                            : p.getWavExportResult().message;
        shownWavState = LatticeProcessor::WavExportState::idle;
        shownWavMessage.clear();
        saveWavButton.setEnabled(!p.isWavExporting());
        repaint();
    }

    void timerCallback() override {
        const bool exporting = p.isWavExporting();
        const auto wav = p.getWavExportResult();
        saveWavButton.setEnabled(!exporting && !wavDialogPending);
        saveWavButton.setButtonText(exporting ? "WAV " + juce::String(int(p.getWavExportProgress() * 100)) + "%" : "SAVE WAV");
        if (wav.state == LatticeProcessor::WavExportState::running) {
            status = "WAV " + juce::String(int(p.getWavExportProgress() * 100)) + "% | " + wav.message;
        } else if (wav.state != LatticeProcessor::WavExportState::idle
                   && (wav.state != shownWavState || wav.message != shownWavMessage)) {
            status = wav.message;
        }
        shownWavState = wav.state;
        shownWavMessage = wav.message;
        play.setButtonText(p.preview.load() ? "STOP" : "PLAY");
        bool idle = !isMouseButtonDownAnywhere() && !groupDraft.hasKeyboardFocus(true) && !seedEdit.hasKeyboardFocus(true) && !numerator.hasKeyboardFocus(true) && !tempo.isMouseOverOrDragging()
            && !devSlider.isMouseOverOrDragging() && !fillSlider.isMouseOverOrDragging() && !spaceSlider.isMouseOverOrDragging() && !densitySlider.isMouseOverOrDragging() && !syncopSlider.isMouseOverOrDragging();
        int pt = p.playTick.load();
        if (follow.getToggleState() && idle && pt >= 0) {
            juce::ScopedLock lock(p.modelLock);
            int pb = juce::jlimit(0, 15, pt / editorBarTicks(p.pattern));
            if (pb != bar) {
                bar = pb;
                clearSelection();
            }
        }
        if (idle) sync(); else syncBarControls();
        repaint();
    }

    std::pair<int,int> hit(juce::Point<int> pt) {
        if (!grid.contains(pt)) return {-1, -1};
        int r = (pt.y - grid.getY()) / 34;
        if (r < 0 || r >= 8) return {-1, -1};
        juce::ScopedLock lock(p.modelLock);
        auto& l = p.pattern.bars[bar][r];
        double t = double(pt.x - grid.getX()) / grid.getWidth() * editorBarTicks(p.pattern);
        for (int i = 0; i < int(l.size()); ++i)
            if (t >= l[i].start && t < l[i].start + l[i].length) return {r, i};
        return {-1, -1};
    }

public:
    Editor(LatticeProcessor& proc): AudioProcessorEditor(proc), p(proc) {
        setLookAndFeel(&style);
        setOpaque(true);

        for (auto* b : {&play, &generate, &reroll, &exportButton, &saveButton, &saveWavButton, &addGroup, &divide3, &divide6, &restore, &off, &grid16, &grid32, &variate, &copyBar, &developSource, &preset7_1, &preset7_2, &preset7_3, &preset5_1, &preset5_2})
            addAndMakeVisible(*b);

        addAndMakeVisible(barLock);
        addAndMakeVisible(ghost);
        addAndMakeVisible(accent);
        addAndMakeVisible(follow);

        follow.setToggleState(true, juce::dontSendNotification);
        follow.setTooltip("Follow the playing bar. Turn off to keep editing a chosen bar.");
        follow.onClick = [this] {
            status = follow.getToggleState() ? "Follow on." : "Follow off. Selected bar stays in view.";
            repaint();
        };

        play.setColour(juce::TextButton::buttonColourId, bgPanel);
        play.setColour(juce::TextButton::textColourOffId, accentCyan);

        generate.setColour(juce::TextButton::buttonColourId, accentPurple);
        generate.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        developSource.setColour(juce::TextButton::buttonColourId, bgPanel);
        developSource.setColour(juce::TextButton::textColourOffId, accentCyan);

        ghost.setTooltip("GENERATE: enable ghost notes (vel 40). DEVELOP: allow new ghost notes without erasing source velocities.");
        backbeatToggle.setTooltip("GENERATE only: use a backbeat skeleton. DEVELOP keeps your bar 1 skeleton.");
        accent.setTooltip("GENERATE only: group accents (vel 127). DEVELOP preserves source velocities.");
        generate.setTooltip("Create a new 16-bar phrase. All bar and track locks are preserved.");
        reroll.setTooltip("Choose a new random seed without regenerating notes.");

        for (int i = 0; i < 16; ++i) {
            auto& b = bars16[i];
            b.setButtonText(juce::String(i + 1));
            b.setClickingTogglesState(false);
            b.setColour(juce::TextButton::buttonColourId, bgPanel);
            b.setColour(juce::TextButton::buttonOnColourId, accentPurple);
            b.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
            b.onClick = [this, i] {
                bar = i;
                clearSelection();
                syncBarControls();
                repaint();
            };
            addAndMakeVisible(b);
        }

        for (int r = 0; r < 8; ++r) {
            // Lock
            auto& lb = locks[r];
            lb.setButtonText("L");
            lb.setClickingTogglesState(true);
            lb.setColour(juce::TextButton::buttonColourId, bgPanel);
            lb.setColour(juce::TextButton::buttonOnColourId, colours[r]);
            lb.setColour(juce::TextButton::textColourOnId, bgMain);
            lb.setTooltip("Lock " + juce::String(trackDisplayName(r)) + " track across all 16 bars during generation");
            lb.onClick = [this, r] {
                juce::ScopedLock lock(p.modelLock);
                p.pattern.locked[r] = locks[r].getToggleState();
                commit();
            };
            addAndMakeVisible(lb);

            // Mute
            auto& mb = mutes[r];
            mb.setButtonText("M");
            mb.setClickingTogglesState(true);
            mb.setColour(juce::TextButton::buttonColourId, bgPanel);
            mb.setColour(juce::TextButton::buttonOnColourId, accentPink);
            mb.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
            mb.setTooltip("Mute " + juce::String(trackDisplayName(r)) + " track");
            mb.onClick = [this, r] {
                trackMuted[r] = mutes[r].getToggleState();
                repaint();
            };
            addAndMakeVisible(mb);

            // Solo
            auto& sb = solos[r];
            sb.setButtonText("S");
            sb.setClickingTogglesState(true);
            sb.setColour(juce::TextButton::buttonColourId, bgPanel);
            sb.setColour(juce::TextButton::buttonOnColourId, accentAmber);
            sb.setColour(juce::TextButton::textColourOnId, bgMain);
            sb.setTooltip("Solo " + juce::String(trackDisplayName(r)) + " track");
            sb.onClick = [this, r] {
                trackSoloed[r] = solos[r].getToggleState();
                repaint();
            };
            addAndMakeVisible(sb);
        }

        // 上下入口共用同一草稿，预设也必须显式应用。
        auto stagePreset = [this](const juce::String& value) { openGroupEditor(0, value); };
        preset7_1.onClick = [stagePreset] { stagePreset("3+2+2"); };
        preset7_2.onClick = [stagePreset] { stagePreset("2+2+3"); };
        preset7_3.onClick = [stagePreset] { stagePreset("2+3+2"); };
        preset5_1.onClick = [stagePreset] { stagePreset("6+4"); };
        preset5_2.onClick = [stagePreset] { stagePreset("4+6"); };
        identify(preset7_1, "group-78-322");
        identify(preset7_2, "group-78-223");
        identify(preset7_3, "group-78-232");
        identify(preset5_1, "group-54-64");
        identify(preset5_2, "group-54-46");
        identify(groupEdit, "group-summary");
        identify(addGroup, "group-open");
        identify(groupStatus, "group-status");
        groupStatus.setColour(juce::Label::backgroundColourId, bgPanel);
        groupStatus.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(groupStatus);
        for (int i = 0; i < int(groupSegments.size()); ++i) {
            auto& button = groupSegments[i];
            identify(button, "group-segment-" + juce::String(i));
            button.setColour(juce::TextButton::textColourOffId, colours[i % 8]);
            button.onClick = [this, i] { openGroupEditor(i); };
            addChildComponent(button);
        }
        addChildComponent(groupDraft);
        groupDraft.setVisible(false);
        groupDraft.onApply = [this] { applyGroupDraft(); };
        groupDraft.onCancel = [this] {
            groupDraft.setVisible(false);
            syncGroupingControls();
            status = "Group draft cancelled. Model unchanged.";
            repaint();
        };

        meter.addItem("4 / 4", 1);
        meter.addItem("7 / 8", 2);
        meter.addItem("5 / 4", 3);
        meter.addItem("CUSTOM", 4);
        meter.onChange = [this] { setMeter(); };
        addAndMakeVisible(meter);

        denominator.addItem("/ 4", 4);
        denominator.addItem("/ 8", 8);
        denominator.onChange = [this] { applyMeter(std::clamp(numerator.getText().getIntValue(), 1, 16), denominator.getSelectedId()); };
        addAndMakeVisible(denominator);

        numerator.setInputRestrictions(2, "0123456789");
        numerator.onReturnKey = [this] { applyMeter(std::clamp(numerator.getText().getIntValue(), 1, 16), denominator.getSelectedId()); };
        numerator.onFocusLost = numerator.onReturnKey;
        addAndMakeVisible(numerator);

        groupEdit.onClick = [this] { openGroupEditor(); };
        groupEdit.setTooltip("Edit grouping in eighth-note units. Apply or Cancel explicitly.");
        addGroup.setTooltip("Open the group draft editor; no group is added automatically.");
        addAndMakeVisible(groupEdit);

        // 5 Real Vertical Sliders
        setupFader(devSlider, 0.0, 1.0, 0.45, [this](double v) {
            juce::ScopedLock lock(p.modelLock);
            p.pattern.development = float(v);
        });
        setupFader(fillSlider, 0.0, 1.0, 0.50, [this](double v) {
            juce::ScopedLock lock(p.modelLock);
            p.pattern.fill = float(v);
        });
        setupFader(spaceSlider, 0.0, 1.0, 0.35, [this](double v) {
            juce::ScopedLock lock(p.modelLock);
            p.pattern.space = float(v);
        });
        setupFader(densitySlider, 0.0, 1.0, 0.55, [this](double v) {
            juce::ScopedLock lock(p.modelLock);
            p.pattern.density = float(v);
        });
        setupFader(syncopSlider, 0.0, 2.0, 1.0, [this](double v) {
            juce::ScopedLock lock(p.modelLock);
            p.pattern.dispersion = int(std::round(v));
        });

        // Backward compatibility tier buttons
        setupTiers(developmentButtons, "development", &lattice::Pattern::development, developmentValues, 0);
        setupTiers(fillButtons, "fill", &lattice::Pattern::fill, fillValues, 1);
        setupTiers(spaceButtons, "space", &lattice::Pattern::space, spaceValues, 2);
        setupTiers(densityButtons, "density", &lattice::Pattern::density, densityValues, 3);
        setupTiers(syncopationButtons, "syncopation", &lattice::Pattern::dispersion, syncopationValues, 4);

        seedEdit.setInputRestrictions(9, "0123456789");
        seedEdit.onFocusLost = [this] {
            juce::ScopedLock lock(p.modelLock);
            p.pattern.seed = seedEdit.getText().getIntValue();
        };
        seedEdit.onReturnKey = seedEdit.onFocusLost;
        addAndMakeVisible(seedEdit);

        tempo.setRange(40, 240, 1);
        tempo.setSliderStyle(juce::Slider::LinearHorizontal);
        tempo.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 24);
        tempo.onValueChange = [this] {
            juce::ScopedLock lock(p.modelLock);
            p.pattern.bpm = tempo.getValue();
            commit();
        };
        addAndMakeVisible(tempo);

        backbeatToggle.setClickingTogglesState(true);
        backbeatToggle.onClick = [this] {
            juce::ScopedLock lock(p.modelLock);
            p.pattern.backbeat = backbeatToggle.getToggleState();
            status = backbeatToggle.getToggleState() ? "Backbeat on. Click GENERATE to apply." : "Backbeat off.";
            repaint();
        };
        addAndMakeVisible(backbeatToggle);

        ghost.onClick = [this] { juce::ScopedLock lock(p.modelLock); p.pattern.ghost = ghost.getToggleState(); };
        accent.onClick = [this] { juce::ScopedLock lock(p.modelLock); p.pattern.accents = accent.getToggleState(); };
        addAndMakeVisible(ghost);
        addAndMakeVisible(accent);

        barLock.setClickingTogglesState(true);
        barLock.onClick = [this] {
            juce::ScopedLock lock(p.modelLock);
            p.pattern.barLocked[bar] = barLock.getToggleState();
            commit();
        };

        grid16.setClickingTogglesState(true);
        grid16.onClick = [this] { juce::ScopedLock lock(p.modelLock); p.pattern.gridStep = 120; repaint(); };
        grid32.setClickingTogglesState(true);
        grid32.onClick = [this] { juce::ScopedLock lock(p.modelLock); p.pattern.gridStep = 60; repaint(); };

        play.onClick = [this] { p.preview.store(!p.preview.load()); };
        generate.onClick = [this] {
            if (!groupingReady()) return;
            juce::ScopedLock lock(p.modelLock);
            p.pattern.seed = seedEdit.getText().getIntValue();
            auto backup = p.pattern.bars;
            bool ok = p.pattern.generate();
            if (ok) {
                for (int b = 0; b < 16; ++b)
                    if (p.pattern.barLocked[b]) p.pattern.bars[b] = backup[b];
                status = "Generated 16 bars. Locked tracks and bars preserved.";
            } else {
                status = "Fix grouping before generating.";
            }
            clearSelection();
            commit();
        };

        reroll.onClick = [this] {
            juce::ScopedLock lock(p.modelLock);
            p.pattern.seed = juce::Random::getSystemRandom().nextInt(999999999);
            seedEdit.setText(juce::String(p.pattern.seed), false);
            status = "New seed ready (" + juce::String(p.pattern.seed) + "). Notes unchanged.";
            repaint();
        };

        developSource.onClick = [this] {
            if (!groupingReady()) return;
            juce::ScopedLock lock(p.modelLock);
            p.pattern.seed = seedEdit.getText().getIntValue();
            bool ok = p.pattern.developFromBar1();
            status = ok ? "Developed bars 2-16 from bar 1. Source and locks kept."
                        : "Cannot develop: bar 1 must contain notes and valid cells; check grouping.";
            clearSelection();
            commit();
        };

        addGroup.onClick = [this] { openGroupEditor(); };

        // Floating Action Buttons Styling
        divide3.setColour(juce::TextButton::buttonColourId, bgCard);
        divide3.setColour(juce::TextButton::textColourOffId, accentCyan);
        divide6.setColour(juce::TextButton::buttonColourId, bgCard);
        divide6.setColour(juce::TextButton::textColourOffId, accentCyan);
        restore.setColour(juce::TextButton::buttonColourId, bgCard);
        restore.setColour(juce::TextButton::textColourOffId, accentPurple);
        off.setColour(juce::TextButton::buttonColourId, bgCard);
        off.setColour(juce::TextButton::textColourOffId, accentPink);

        divide3.onClick = [this] { subdivision(3); };
        divide6.onClick = [this] { subdivision(6); };
        restore.onClick = [this] { subdivision(0); };
        off.onClick = [this] {
            if (selectedRow >= 0) {
                juce::ScopedLock lock(p.modelLock);
                auto& l = p.pattern.bars[bar][selectedRow];
                for (int i = std::min(anchor, endCell); i <= std::max(anchor, endCell); ++i)
                    l[i].velocity = 0;
                commit();
            }
        };

        variate.onClick = [this] {
            if (!groupingReady()) return;
            juce::ScopedLock lock(p.modelLock);
            int cur = bar;
            if (p.pattern.barLocked[cur]) {
                status = "Bar " + juce::String(cur + 1) + " is locked - cannot variate.";
                repaint();
                return;
            }
            auto backup = p.pattern.bars;
            p.pattern.seed = juce::Random::getSystemRandom().nextInt(999999999);
            seedEdit.setText(juce::String(p.pattern.seed), false);
            bool ok = p.pattern.generate();
            if (ok) {
                for (int b = 0; b < 16; ++b)
                    if (b != cur) p.pattern.bars[b] = backup[b];
                status = "Variated bar " + juce::String(cur + 1) + ". Other 15 bars restored.";
            } else {
                status = "Fix grouping before variating.";
            }
            clearSelection();
            commit();
        };

        copyBar.onClick = [this] {
            int cur = bar, nxt = bar + 1;
            if (nxt >= 16) {
                status = "No next bar to copy into (already bar 16).";
                repaint();
                return;
            }
            juce::ScopedLock lock(p.modelLock);
            if (p.pattern.barLocked[nxt]) {
                status = "Target bar " + juce::String(nxt + 1) + " is locked - copy blocked.";
                repaint();
                return;
            }
            for (int r = 0; r < 8; ++r) {
                if (p.pattern.locked[r]) continue;
                p.pattern.bars[nxt][r] = p.pattern.bars[cur][r];
            }
            status = "Copied bar " + juce::String(cur + 1) + " into bar " + juce::String(nxt + 1) + ". Locked tracks/bars kept.";
            commit();
        };

        exportButton.addMouseListener(&dragListener, false);
        exportButton.onClick = [this] {
            status = "Drag this button into DAW, or click SAVE .MID.";
            repaint();
        };

        identify(saveWavButton, "save-wav");
        saveWavButton.setTooltip("Export all 16 bars + 3.5 s tail, all 8 tracks (M/S are UI-only). 48 kHz stereo 24-bit WAV. Uses panel pattern.bpm at export start, NOT host tempo or automation.");
        saveWavButton.onClick = [this] {
            if (wavDialogPending || p.isWavExporting()) return;
            wavDialogPending = true;
            saveWavButton.setEnabled(false);
            wavChooser = std::make_unique<juce::FileChooser>("Save 16-bar WAV - panel BPM, not host tempo",
                juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("LATTICE.wav"), "*.wav");
            // 扩展名规范化后单独确认覆盖，避免保存对话框确认了不同的原始路径。
            wavChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                [safe = juce::Component::SafePointer<Editor>(this)](const juce::FileChooser& fc) {
                    if (safe == nullptr) return;
                    auto destination = fc.getResult();
                    if (destination == juce::File()) {
                        safe->wavDialogPending = false;
                        return;
                    }
                    destination = destination.withFileExtension(".wav");
                    if (destination.existsAsFile()) {
                        juce::AlertWindow::showAsync(juce::MessageBoxOptions{}
                            .withIconType(juce::MessageBoxIconType::WarningIcon)
                            .withTitle("Replace WAV?")
                            .withMessage("Replace " + destination.getFullPathName() + "? The original is kept until export succeeds.")
                            .withButton("Replace").withButton("Cancel").withAssociatedComponent(safe.getComponent()),
                            [safe, destination](int choice) {
                                if (safe == nullptr) return;
                                if (choice == 1) safe->beginWavExport(destination);
                                else safe->wavDialogPending = false;
                            });
                    } else {
                        safe->beginWavExport(destination);
                    }
                });
        };

        saveButton.onClick = [this] {
            auto f = p.exportMidi();
            if (!f.existsAsFile()) {
                status = "MIDI export failed.";
                return;
            }
            chooser = std::make_unique<juce::FileChooser>("Save 16-bar MIDI",
                juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(f.getFileName()), "*.mid");
            chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                [safe = juce::Component::SafePointer<Editor>(this), f](const juce::FileChooser& fc) {
                    if (safe != nullptr) {
                        auto dest = fc.getResult();
                        if (dest != juce::File()) {
                            safe->status = f.copyFileTo(dest) ? "Saved " + dest.getFileName() : "Save failed.";
                            safe->repaint();
                        }
                    }
                });
        };

        sync();
        updateSelection();
        setSize(1120, 760);
        startTimerHz(25);
    }

    ~Editor() override {
        stopTimer();
        wavChooser.reset();
        exportButton.removeMouseListener(&dragListener);
        setLookAndFeel(nullptr);
    }

    void resized() override {
        // Top Header Bar
        play.setBounds(410, 16, 100, 32);
        tempo.setBounds(520, 18, 170, 28);
        exportButton.setBounds(700, 16, 120, 32);
        saveButton.setBounds(830, 16, 120, 32);
        saveWavButton.setBounds(960, 16, 120, 32);

        // ================= TOP SECTION: SEQUENCER DECK (y: 56 - 440) =================
        const int bw = 30, gap = 2, groupGap = 6;
        for (int b = 0; b < 16; ++b) {
            int x = 40 + (b / 4) * (4 * bw + 3 * gap + groupGap) + (b % 4) * (bw + gap);
            bars16[b].setBounds(x, 60, bw, 24);
        }

        barLock.setBounds(610, 60, 85, 24);
        variate.setBounds(700, 60, 85, 24);
        copyBar.setBounds(790, 60, 85, 24);
        grid16.setBounds(880, 60, 50, 24);
        grid32.setBounds(935, 60, 50, 24);
        follow.setBounds(990, 60, 95, 24);

        groupStatus.setBounds(20, grouping.getY(), 96, grouping.getHeight());
        groupDraft.setBounds(grid.getX() + 12, grouping.getBottom() + 6, 440, 264);
        syncGroupingControls();

        // Track Headers: M / S / L
        for (int r = 0; r < 8; ++r) {
            int y = grid.getY() + r * 34;
            mutes[r].setBounds(62, y + 6, 17, 17);
            solos[r].setBounds(81, y + 6, 17, 17);
            locks[r].setBounds(100, y + 6, 17, 17);
        }

        // ================= BOTTOM SECTION: GENERATIVE RACK (y: 450 - 720) =================
        // Zone A: Meter & Grouping Rack
        meter.setBounds(24, 480, 85, 28);
        numerator.setBounds(24, 512, 40, 24);
        denominator.setBounds(68, 512, 41, 24);

        groupEdit.setBounds(116, 480, 95, 28);
        addGroup.setBounds(162, 512, 49, 24);

        preset7_1.setBounds(24, 544, 56, 22);
        preset7_2.setBounds(84, 544, 56, 22);
        preset7_3.setBounds(144, 544, 56, 22);
        preset5_1.setBounds(24, 544, 56, 22);
        preset5_2.setBounds(84, 544, 56, 22);

        ghost.setBounds(24, 594, 180, 22);
        accent.setBounds(24, 620, 180, 22);
        backbeatToggle.setBounds(24, 646, 180, 22);

        // Zone B: 5 Vertical Faders
        devSlider.setBounds(268, 504, 64, 136);
        fillSlider.setBounds(372, 504, 64, 136);
        spaceSlider.setBounds(476, 504, 64, 136);
        densitySlider.setBounds(580, 504, 64, 136);
        syncopSlider.setBounds(684, 504, 64, 136);

        // Zone C: Generation Hub
        seedEdit.setBounds(800, 480, 135, 28);
        reroll.setBounds(945, 480, 135, 28);

        generate.setBounds(800, 526, 280, 56);
        developSource.setBounds(800, 592, 280, 50);
    }

    void paint(juce::Graphics& g) override {
        // Background
        g.fillAll(bgMain);

        // Section Raised Cards
        g.setColour(bgCard);
        g.fillRoundedRectangle(15.f, 54.f, 1090.f, 386.f, 6.f); // Top Sequencer Card
        g.fillRoundedRectangle(15.f, 448.f, 1090.f, 264.f, 6.f); // Bottom Generative Rack Card

        // Card Borders
        g.setColour(borderSubtle);
        g.drawRoundedRectangle(15.f, 54.f, 1090.f, 386.f, 6.f, 0.8f);
        g.drawRoundedRectangle(15.f, 448.f, 1090.f, 264.f, 6.f, 0.8f);

        // Header Branding
        text(g, "ozo", {24, 14, 50, 32}, 28, accentCyan);
        text(g, "LATTICE", {76, 14, 160, 32}, 29, textPrimary);
        text(g, "MATH ROCK DRUM MACHINE", {24, 42, 210, 14}, 11.f, textDim);

        text(g, "BPM", {480, 18, 36, 28}, 12.f, textMuted);

        // Top Deck: Bar Strip & Tools Panel background
        g.setColour(bgPanel);
        g.fillRoundedRectangle(22.f, 58.f, 1076.f, 28.f, 4.f);
        g.setColour(borderSubtle);
        g.drawRoundedRectangle(22.f, 58.f, 1076.f, 28.f, 4.f, 0.8f);

        juce::ScopedLock lock(p.modelLock);
        auto& m = p.pattern;
        const float scale = float(grid.getWidth()) / editorBarTicks(m);

        // Ruler bar background
        g.setColour(bgPanel);
        g.fillRoundedRectangle(grouping.toFloat(), 4.f);
        g.setColour(borderSubtle);
        g.drawRoundedRectangle(grouping.toFloat(), 4.f, 0.8f);

        // 状态独占左侧标题区，分组条保持与网格对齐；非法数据不参与组边界计算。
        if (validGrouping(m.groups, groupingTarget(m))) {
            int start = 0;
            for (int i = 0; i < int(m.groups.size()); ++i) {
                const float x = grid.getX() + start * 240 * scale;
                g.setColour(colours[i % 8].withAlpha(0.35f));
                g.drawVerticalLine(int(x), float(grid.getY()), float(grid.getBottom()));
                start += m.groups[i];
            }
        }

        // Grid subdiv lines
        int gs = m.gridStep > 0 ? m.gridStep : 120;
        for (int t = 0; t <= editorBarTicks(m); t += gs) {
            float x = grid.getX() + t * scale;
            if (x > grid.getX() && x < grid.getRight()) {
                g.setColour(t % 480 == 0 ? borderStrong : t % 240 == 0 ? borderSubtle : bgPanel);
                g.drawVerticalLine(int(x), float(grid.getY()), float(grid.getBottom()));
            }
        }

        // Draw 8 Tracks (Wireframe layout: row height 34px, tracks x: 120 - 1100)
        for (int r = 0; r < 8; ++r) {
            int y = grid.getY() + r * 34;

            // Alternate row background
            if (r % 2 == 1) {
                g.setColour(bgMain.withAlpha(0.35f));
                g.fillRect(grid.getX(), y, grid.getWidth(), 34);
            }

            // Track Name
            g.setColour(colours[r]);
            g.setFont(juce::Font(juce::FontOptions(12.5f, juce::Font::bold)));
            // 轨名到 x=60 截止，M/S/L 从 x=62 开始；短名完整显示且不重叠。
            g.drawFittedText(trackDisplayName(r), juce::Rectangle<int>{20, y + 4, 40, 20}, juce::Justification::centredLeft, 1, 0.85f);

            // Track separator line
            g.setColour(borderSubtle);
            g.drawHorizontalLine(y + 33, float(grid.getX()), float(grid.getRight()));

            // Track Cells
            auto& l = m.bars[bar][r];
            for (int i = 0; i < int(l.size()); ++i) {
                auto& c = l[i];
                float x = grid.getX() + c.start * scale, w = c.length * scale;

                // Base slot bar
                g.setColour(bgPanel);
                g.fillRect(x + 1.f, float(y + 26), std::max(1.f, w - 2.f), 2.f);

                // Beat marker
                if (c.start % 240 == 0) {
                    g.setColour(borderSubtle);
                    g.drawVerticalLine(int(x), float(y + 3), float(y + 30));
                }

                // Active note hit - Neon Glow Blocks (3 tiers exact: 127/80/40)
                if (c.velocity > 0) {
                    float h = c.velocity >= 120 ? 22.f : c.velocity >= 70 ? 15.f : 8.f;
                    float nw = std::max(3.f, std::min(24.f, w - 4.f));
                    float noteX = x + (w - nw) * 0.5f;
                    float noteY = float(y + 27) - h;

                    float alpha = c.velocity >= 120 ? 1.0f : c.velocity >= 70 ? 0.75f : 0.4f;
                    g.setColour(colours[r].withAlpha(alpha));
                    g.fillRoundedRectangle(noteX, noteY, nw, h, 2.5f);

                    if (c.velocity >= 120) {
                        g.setColour(juce::Colours::white.withAlpha(0.8f));
                        g.fillRoundedRectangle(noteX + 0.5f, noteY + 0.5f, nw - 1.f, 2.f, 1.f);
                    }
                }

                // Selection highlight
                if (r == selectedRow && i >= std::min(anchor, endCell) && i <= std::max(anchor, endCell)) {
                    g.setColour(accentPurple.withAlpha(0.2f));
                    g.fillRect(x, float(y + 2), w, 30.f);
                    g.setColour(accentPurple);
                    g.drawRect(x, float(y + 2), w, 30.f, 1.2f);
                }
            }
        }

        // Playhead indicator
        int pt = p.playTick.load();
        if (pt >= 0 && pt / editorBarTicks(m) == bar) {
            float x = grid.getX() + (pt % editorBarTicks(m)) * scale;
            g.setColour(accentCyan);
            g.drawLine(x, float(grid.getY() - 3), x, float(grid.getBottom()), 2.f);
            juce::Path pth;
            pth.addTriangle(x - 4.f, float(grid.getY() - 5), x + 4.f, float(grid.getY() - 5), x, float(grid.getY()));
            g.fillPath(pth);
        }

        // Sequencer Bottom Velocity Helper
        text(g, "Drag Up/Down on notes to adjust Velocity (127 / 80 / 40 / 0)  -  Box-drag across notes opens Subdiv Toolbar", {grid.getX(), 404, 800, 18}, 11.5f, textDim);

        // ================= BOTTOM SECTION: GENERATIVE RACK =================
        // Zone A: METER & GROUPS
        text(g, "METER & GROUPS", {24, 458, 160, 18}, 13.5f, textPrimary);
        text(g, "QUICK PRESETS", {24, 528, 160, 14}, 11.f, textDim);
        text(g, "ARTICULATION", {24, 574, 160, 14}, 11.f, textDim);

        // Zone B: 5 Vertical Faders
        const char* const faderLabels[5] = {"DEVELOP", "FILL", "SPACE", "DENSITY", "SYNCOP"};
        const double faderVals[5] = {devSlider.getValue(), fillSlider.getValue(), spaceSlider.getValue(), densitySlider.getValue(), syncopSlider.getValue()};

        for (int c = 0; c < 5; ++c) {
            int colX = 268 + c * 104;
            // Title (13.5pt bold)
            text(g, faderLabels[c], {colX - 20, 458, 100, 18}, 13.5f, textPrimary, juce::Justification::centred);

            // Subtitle: MID (0.45)
            juce::String subStr = (c == 4) ? (faderVals[c] == 0 ? "LOW" : faderVals[c] == 1 ? "MID" : "HIGH")
                                           : (faderVals[c] < 0.2 ? "LOW (" : faderVals[c] > 0.65 ? "HIGH (" : "MID (") + juce::String(faderVals[c], 2) + ")";
            text(g, subStr, {colX - 20, 480, 100, 16}, 11.5f, textDim, juce::Justification::centred);

            // Center of slider = colX + 32. Thumb width = 34 (span: colX + 15 to colX + 49).
            // Left tick lines: from (colX + 7) to (colX + 13), gap of 2px to thumb.
            // Right tick lines: from (colX + 51) to (colX + 57), gap of 2px to thumb.
            int tickX1 = colX + 7,  tickX2 = colX + 13;
            int tickR1 = colX + 51, tickR2 = colX + 57;
            g.setColour(borderStrong);
            g.drawLine(float(tickX1), 512.f, float(tickX2), 512.f, 1.5f); // High
            g.drawLine(float(tickX1), 572.f, float(tickX2), 572.f, 1.5f); // Mid
            g.drawLine(float(tickX1), 632.f, float(tickX2), 632.f, 1.5f); // Low
            g.drawLine(float(tickR1), 512.f, float(tickR2), 512.f, 1.5f);
            g.drawLine(float(tickR1), 572.f, float(tickR2), 572.f, 1.5f);
            g.drawLine(float(tickR1), 632.f, float(tickR2), 632.f, 1.5f);

            // Left Labels H / M / L positioned snug to the left of left ticks
            text(g, "H", {colX - 6, 504, 11, 16}, 9.5f, textDim, juce::Justification::centredRight);
            text(g, "M", {colX - 6, 564, 11, 16}, 9.5f, textDim, juce::Justification::centredRight);
            text(g, "L", {colX - 6, 624, 11, 16}, 9.5f, textDim, juce::Justification::centredRight);

            // Bottom scale label
            text(g, "H - M - L", {colX - 20, 650, 100, 14}, 10.5f, textDim, juce::Justification::centred);
        }

        // Zone C: GENERATION HUB
        text(g, "GENERATION HUB", {800, 458, 200, 18}, 13.5f, textPrimary);

        // Subtitles under Big Action Buttons
        text(g, "16-BAR MOTIF", {800, 560, 280, 14}, 9.5f, juce::Colour(0xffe2dbe8), juce::Justification::centred);
        text(g, "ACROSS PHRASE", {800, 622, 280, 14}, 9.5f, textDim, juce::Justification::centred);

        // Global Status Toast at bottom
        text(g, "> " + status, {24, 722, 1070, 18}, 11.5f, accentCyan);
    }

    void paintOverChildren(juce::Graphics& g) override {
        juce::ScopedLock lock(p.modelLock);
        int pt = p.playTick.load();
        int playingBar = pt >= 0 ? juce::jlimit(0, 15, pt / editorBarTicks(p.pattern)) : -1;

        for (int b = 0; b < 16; ++b) {
            auto bounds = bars16[b].getBounds();
            if (b == playingBar) {
                g.setColour(accentCyan);
                g.drawRoundedRectangle(bounds.toFloat().reduced(1.f), 4.f, 2.f);
            }
            if (p.pattern.barLocked[b]) {
                float x = float(bounds.getRight() - 7), y = float(bounds.getY() + 2);
                g.setColour(accentPink);
                g.drawRoundedRectangle(x + 1.f, y, 4.f, 5.f, 2.f, 1.f);
                g.fillRoundedRectangle(x, y + 3.f, 6.f, 5.f, 1.f);
            }
        }
    }

    void mouseDown(const juce::MouseEvent& ev) override {
        if (grouping.contains(ev.getPosition())) {
            clearSelection();
            // 状态区域和按钮间隙都不会隐式修改分组。
            if (groupStripBounds().contains(ev.getPosition()) && !ev.mods.isRightButtonDown())
                openGroupEditor();
            return;
        }

        auto h = snapHit(ev.getPosition());
        if (h.first < 0) {
            clearSelection();
            return;
        }

        selectedRow = h.first;
        anchor = endCell = h.second;
        down = ev.getPosition();
        dragging = false;
        hasDraggedVelocity = false;

        {
            juce::ScopedLock lock(p.modelLock);
            auto& c = p.pattern.bars[bar][selectedRow][anchor];
            dragInitialVelocity = c.velocity;
        }

        updateSelection();
    }

    void mouseDrag(const juce::MouseEvent& ev) override {
        if (selectedRow < 0 || anchor < 0) return;

        int dx = ev.x - down.x;
        int dy = down.y - ev.y; // positive when dragging upwards

        // Vertical drag adjusts velocity of the single selected cell
        if (std::abs(dy) > 4 && std::abs(dx) < 18) {
            hasDraggedVelocity = true;
            dragging = true;
            juce::ScopedLock lock(p.modelLock);
            auto& c = p.pattern.bars[bar][selectedRow][anchor];

            int newV = 80;
            if (dy > 14) newV = 127;
            else if (dy >= -10) newV = 80;
            else if (dy >= -26) newV = 40;
            else newV = 0;

            if (c.velocity != newV) {
                c.velocity = newV;
                status = "Velocity: " + juce::String(c.velocity) + (c.velocity == 127 ? " (Accent)" : c.velocity == 80 ? " (Normal)" : c.velocity == 40 ? " (Ghost)" : " (Off)");
                commit();
            }
            return;
        }

        // Horizontal drag selects a range of cells for subdivision
        auto h = hit({std::clamp(ev.x, grid.getX(), grid.getRight() - 1), grid.getY() + selectedRow * 34 + 16});
        if (h.second >= 0) {
            endCell = h.second;
            dragging = ev.getDistanceFromDragStart() > 4;
            updateSelection();
        }
    }

    void mouseUp(const juce::MouseEvent& ev) override {
        if (selectedRow < 0 || anchor < 0) return;

        if (hasDraggedVelocity) {
            hasDraggedVelocity = false;
            return;
        }

        if (!dragging) {
            juce::ScopedLock lock(p.modelLock);
            auto& c = p.pattern.bars[bar][selectedRow][anchor];
            if (ev.mods.isRightButtonDown() || ev.mods.isShiftDown()) {
                c.velocity = c.velocity == 127 ? 80 : c.velocity == 80 ? 40 : 127;
            } else {
                c.velocity = c.velocity ? 0 : 80;
            }
            commit();
        }
        repaint();
    }
};
}

juce::AudioProcessorEditor* makeLatticeEditor(LatticeProcessor& p) {
    return new Editor(p);
}
