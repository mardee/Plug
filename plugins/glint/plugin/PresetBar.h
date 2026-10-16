#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <functional>
#include <cmath>

namespace ozo
{

//==============================================================================
// 一条预设。出厂的和用户存的都走这个结构，预设条不区分来源，只看这些字段。
struct PresetEntry
{
    juce::String name;
    bool factory = true;    // 出厂预设只读，不可删除
    bool wild    = false;   // 决定圆点颜色（如 wild/high-energy 特殊标示）
};

//==============================================================================
// NIGHTSHIFT 预设条组件。
//
// 适配 218x65 紧凑区域（推荐 bounds: x=39, y=808, width=218, height=65）：
// - 顶部预设名称行：左侧状态圆点、名称、右侧下拉指示箭头；点击向上弹出完整列表（Factory / Mine 分区）。
// - 底部工具行：[<] 上一个、[>] 下一个、预设计数（如 "3 / 12"）、[+] 新建保存、[×] 删除用户预设（出厂预设只读禁用）。
// - 若高度 < 48 则自动自适应为 PRISM 经典的单行胶囊模式。
// - 滚轮支持平滑增减预设。
// - 键盘导航：Left/Up 前移、Right/Down 后移、Return/Space 展开菜单。
// - 菜单使用 SafePointer 保护，避免异步回调悬空。
// - 提供 isPopupActive() 接口供主面板淡出保护。
// - 遵循 NIGHTSHIFT 暖色/暗调/琥珀配色，独立于 OzoLookAndFeel。
class PresetBar : public juce::Component, public juce::TooltipClient
{
public:
    std::function<void (int)> onSelect;    // 选中第 index 个
    std::function<void ()>    onSave;      // 想存一个新的
    std::function<void (int)> onDelete;    // 想删第 index 个

    PresetBar()
    {
        setComponentID ("presetBar");
        setWantsKeyboardFocus (true);
    }

    ~PresetBar() override = default;

    void setEntries (std::vector<PresetEntry> list, int selected)
    {
        entries = std::move (list);
        index   = selected;
        repaint();
    }

    int getIndex() const noexcept { return index; }
    int getNumEntries() const noexcept { return static_cast<int> (entries.size()); }
    bool isPopupActive() const noexcept { return popupActive; }

    void resized() override
    {
        updateLayout();
    }

    void paint (juce::Graphics& g) override
    {
        updateLayout();

        const bool isTwoRow = getLocalBounds().getHeight() >= 48;

        if (isTwoRow)
        {
            drawName (g);

            const bool hasEntries = ! entries.empty();
            const bool canPrev = index > 0;
            const bool canNext = hasEntries && index < static_cast<int> (entries.size()) - 1;
            const bool canDelete = index >= 0 && index < static_cast<int> (entries.size())
                                && ! entries[static_cast<size_t> (index)].factory;

            // 绘制底部按钮底座
            drawButtonFrame (g, prevArea, canPrev, hover == Hover::prev);
            drawButtonFrame (g, nextArea, canNext, hover == Hover::next);
            drawButtonFrame (g, saveArea, true, hover == Hover::save);
            drawButtonFrame (g, deleteArea, canDelete, hover == Hover::del);

            // 绘制按钮图标
            drawArrow (g, prevArea, -1.0f, canPrev, hover == Hover::prev);
            drawArrow (g, nextArea, +1.0f, canNext, hover == Hover::next);
            drawIconButton (g, saveArea, true, true, hover == Hover::save);
            drawIconButton (g, deleteArea, false, canDelete, hover == Hover::del);

            // 中间计数器（如 "3 / 12"），清晰大字号 >= 12
            if (! counterArea.isEmpty())
            {
                g.setFont (juce::FontOptions (12.0f));
                g.setColour (Colours::textDim);
                juce::String counterText;
                if (index >= 0 && hasEntries)
                    counterText = juce::String (index + 1) + " / " + juce::String (entries.size());
                else if (hasEntries)
                    counterText = "- / " + juce::String (entries.size());
                else
                    counterText = "- / -";

                g.drawText (counterText, counterArea, juce::Justification::centred, false);
            }
        }
        else
        {
            // 单行胶囊布局（PRISM 经典回退）
            auto b = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (Colours::bg);
            g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
            g.setColour (Colours::edge);
            g.drawRoundedRectangle (b, b.getHeight() * 0.5f, 1.0f);

            const float dividerX = saveArea.getX() - 6.0f;
            g.setColour (Colours::edge.withAlpha (0.50f));
            g.drawLine (dividerX, b.getY() + 7.0f, dividerX, b.getBottom() - 7.0f, 1.0f);

            drawArrow (g, prevArea, -1.0f, index > 0, hover == Hover::prev);
            drawArrow (g, nextArea, +1.0f, index < static_cast<int> (entries.size()) - 1, hover == Hover::next);

            drawIconButton (g, saveArea, true, true, hover == Hover::save);

            const bool canDelete = index >= 0 && index < static_cast<int> (entries.size())
                                && ! entries[static_cast<size_t> (index)].factory;
            drawIconButton (g, deleteArea, false, canDelete, hover == Hover::del);

            drawName (g);
        }

        // 键盘焦点指示光圈
        if (hasKeyboardFocus (true))
        {
            g.setColour (Colours::warm.withAlpha (0.45f));
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 5.0f, 1.2f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto p = e.position;

        if (prevArea.contains (p))
        {
            step (-1);
            return;
        }

        if (nextArea.contains (p))
        {
            step (+1);
            return;
        }

        if (saveArea.contains (p))
        {
            if (onSave != nullptr)
                onSave();
            return;
        }

        if (deleteArea.contains (p))
        {
            const bool canDelete = index >= 0 && index < static_cast<int> (entries.size())
                                && ! entries[static_cast<size_t> (index)].factory;

            if (canDelete && onDelete != nullptr)
                onDelete (index);

            return;
        }

        // 点名称栏或计数栏弹出预设列表
        if (nameArea.contains (p) || counterArea.contains (p))
        {
            showMenu();
            return;
        }
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        wheelAccum += wheel.deltaY;

        if (std::abs (wheelAccum) < 0.4f)
            return;

        step (wheelAccum > 0.0f ? -1 : +1);
        wheelAccum = 0.0f;
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        lastMousePos = e.position;
        const auto next = prevArea.contains (e.position)    ? Hover::prev
                        : nextArea.contains (e.position)    ? Hover::next
                        : saveArea.contains (e.position)    ? Hover::save
                        : deleteArea.contains (e.position)  ? Hover::del
                        : nameArea.contains (e.position)    ? Hover::name
                        : counterArea.contains (e.position) ? Hover::counter
                        : Hover::none;

        if (next != hover)
        {
            hover = next;
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        lastMousePos = { -1.0f, -1.0f };
        if (hover != Hover::none)
        {
            hover = Hover::none;
            repaint();
        }
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::leftKey || key == juce::KeyPress::upKey)
        {
            step (-1);
            return true;
        }
        if (key == juce::KeyPress::rightKey || key == juce::KeyPress::downKey)
        {
            step (+1);
            return true;
        }
        if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey)
        {
            showMenu();
            return true;
        }
        return false;
    }

    juce::String getTooltip() override
    {
        if (prevArea.contains (lastMousePos))   return "Previous preset";
        if (nextArea.contains (lastMousePos))   return "Next preset";
        if (saveArea.contains (lastMousePos))   return "Save current settings as user preset";
        if (deleteArea.contains (lastMousePos)) return "Delete current user preset (factory presets are read-only)";
        if (nameArea.contains (lastMousePos))   return "Click to open preset list";
        return {};
    }

private:
    struct Colours
    {
        inline static const juce::Colour bg        { 0xcc141815 }; // panel background
        inline static const juce::Colour bgHover   { 0xdd222622 }; // hovered background
        inline static const juce::Colour edge      { 0xff494d44 }; // subtle panel border
        inline static const juce::Colour text      { 0xffe9e3ce }; // bright bone/cream
        inline static const juce::Colour textDim   { 0xffafa995 }; // muted warm gray
        inline static const juce::Colour textFaint { 0xff72786a }; // inactive text
        inline static const juce::Colour warm      { 0xfff5d79e }; // warm golden amber
        inline static const juce::Colour dotWild   { 0xffff4b45 }; // wild preset red
        inline static const juce::Colour dotCyan   { 0xff7ce6e8 }; // factory preset cyan
    };

    void updateLayout()
    {
        const auto b = getLocalBounds().toFloat().reduced (0.5f);
        const float w = b.getWidth();
        const float h = b.getHeight();

        if (h >= 48.0f)
        {
            // 218x65 场景：清晰双行布局
            const float gap = 4.0f;
            const float row1H = juce::jlimit (26.0f, 32.0f, (h - gap) * 0.52f);
            const float row2H = h - row1H - gap;

            nameArea = { b.getX(), b.getY(), w, row1H };

            const float y2 = nameArea.getBottom() + gap;
            const float btnW = juce::jlimit (28.0f, 36.0f, w * 0.16f);

            prevArea   = { b.getX(), y2, btnW, row2H };
            nextArea   = { prevArea.getRight() + 4.0f, y2, btnW, row2H };

            deleteArea = { b.getRight() - btnW, y2, btnW, row2H };
            saveArea   = { deleteArea.getX() - 4.0f - btnW, y2, btnW, row2H };

            counterArea = { nextArea.getRight(), y2,
                            juce::jmax (0.0f, saveArea.getX() - nextArea.getRight()), row2H };
        }
        else
        {
            // 单行宽条胶囊模式（PRISM 经典）
            const float actionBtnW = juce::jlimit (20.0f, 26.0f, w * 0.11f);
            deleteArea = { b.getRight() - actionBtnW - 4.0f, b.getY(), actionBtnW, h };
            saveArea   = { deleteArea.getX() - actionBtnW - 2.0f, b.getY(), actionBtnW, h };

            const float dividerX = saveArea.getX() - 6.0f;
            const float arrowW = juce::jlimit (20.0f, 26.0f, w * 0.12f);
            prevArea = { b.getX() + 2.0f, b.getY(), arrowW, h };
            nextArea = { dividerX - 8.0f - arrowW, b.getY(), arrowW, h };
            nameArea = { prevArea.getRight(), b.getY(),
                         juce::jmax (0.0f, nextArea.getX() - prevArea.getRight()), h };
            counterArea = {};
        }
    }

    void drawButtonFrame (juce::Graphics& g, juce::Rectangle<float> area, bool enabled, bool hot)
    {
        auto r = area.reduced (0.5f);
        const float radius = 4.0f;

        auto bg = Colours::bg;
        auto border = Colours::edge;

        if (! enabled)
        {
            bg = bg.withAlpha (0.35f);
            border = border.withAlpha (0.22f);
        }
        else if (hot)
        {
            bg = Colours::bgHover;
            border = Colours::warm;
        }

        g.setColour (bg);
        g.fillRoundedRectangle (r, radius);
        g.setColour (border);
        g.drawRoundedRectangle (r, radius, 1.0f);
    }

    void drawArrow (juce::Graphics& g, juce::Rectangle<float> area, float dir,
                    bool enabled, bool hot)
    {
        const float len = 5.0f;
        const auto c = area.getCentre();

        // dir = -1 时向左，+1 时向右
        juce::Path p;
        p.startNewSubPath (c.x - dir * len * 0.35f, c.y - len);
        p.lineTo          (c.x + dir * len * 0.65f, c.y);
        p.lineTo          (c.x - dir * len * 0.35f, c.y + len);

        g.setColour (! enabled ? Colours::textFaint.withAlpha (0.30f)
                               : hot ? Colours::text : Colours::textDim);
        g.strokePath (p, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }

    void drawIconButton (juce::Graphics& g, juce::Rectangle<float> area, bool plus, bool enabled, bool hot)
    {
        const float s = 4.0f;
        const auto c = area.getCentre();

        if (! enabled)
            g.setColour (Colours::textFaint.withAlpha (0.25f));
        else if (hot)
            g.setColour (plus ? Colours::warm : Colours::text);
        else
            g.setColour (Colours::textDim);

        if (plus)
        {
            g.drawLine (c.x - s, c.y, c.x + s, c.y, 1.6f);
            g.drawLine (c.x, c.y - s, c.x, c.y + s, 1.6f);
        }
        else
        {
            g.drawLine (c.x - s, c.y - s, c.x + s, c.y + s, 1.6f);
            g.drawLine (c.x - s, c.y + s, c.x + s, c.y - s, 1.6f);
        }
    }

    void drawName (juce::Graphics& g)
    {
        const bool isTwoRow = getLocalBounds().getHeight() >= 48;
        const auto r = nameArea.reduced (0.5f);
        const float radius = isTwoRow ? 5.0f : r.getHeight() * 0.5f;

        const bool hot = (hover == Hover::name) || popupActive;
        g.setColour (hot ? Colours::bgHover : Colours::bg);
        g.fillRoundedRectangle (r, radius);
        g.setColour (hot ? Colours::warm : Colours::edge);
        g.drawRoundedRectangle (r, radius, 1.0f);

        const bool selected = index >= 0 && index < static_cast<int> (entries.size());

        if (selected)
        {
            const auto& e = entries[static_cast<size_t> (index)];
            const float dotR = 3.5f;
            const float cx = nameArea.getX() + 14.0f;
            const float cy = nameArea.getCentreY();

            const auto dotColour = e.wild ? Colours::dotWild
                                          : (e.factory ? Colours::warm : Colours::dotCyan);
            g.setColour (dotColour);
            if (e.factory)
                g.drawEllipse (cx - dotR, cy - dotR, dotR * 2.0f, dotR * 2.0f, 1.4f);
            else
                g.fillEllipse (cx - dotR, cy - dotR, dotR * 2.0f, dotR * 2.0f);
        }

        g.setColour (selected ? Colours::text : Colours::textFaint);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));

        const float leftPad  = selected ? 26.0f : 14.0f;
        const float rightPad = isTwoRow ? 24.0f : 6.0f;
        const auto textBounds = nameArea.withTrimmedLeft (leftPad).withTrimmedRight (rightPad);

        g.drawText (selected ? entries[static_cast<size_t> (index)].name : juce::String ("No preset"),
                    textBounds, juce::Justification::centredLeft, true);

        // 两行模式下绘制小三角下拉指示
        if (isTwoRow)
        {
            juce::Path ch;
            const float chX = nameArea.getRight() - 13.0f;
            const float chY = nameArea.getCentreY();
            ch.startNewSubPath (chX - 4.0f, chY - 2.0f);
            ch.lineTo          (chX,        chY + 2.5f);
            ch.lineTo          (chX + 4.0f, chY - 2.0f);

            g.setColour (hot ? Colours::text : Colours::textDim);
            g.strokePath (ch, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
        }
    }

    void step (int delta)
    {
        if (entries.empty())
            return;

        int base = index;
        if (base < 0)
            base = delta > 0 ? -1 : static_cast<int> (entries.size());

        const int next = juce::jlimit (0, static_cast<int> (entries.size()) - 1, base + delta);

        if (next != index && onSelect != nullptr)
            onSelect (next);
    }

    void showMenu()
    {
        juce::PopupMenu menu;
        bool mineHeader = false;

        for (int i = 0; i < static_cast<int> (entries.size()); ++i)
        {
            if (i == 0)
                menu.addSectionHeader ("Factory");

            if (! entries[static_cast<size_t> (i)].factory && ! mineHeader)
            {
                menu.addSeparator();
                menu.addSectionHeader ("Mine");
                mineHeader = true;
            }

            menu.addItem (i + 1, entries[static_cast<size_t> (i)].name, true, i == index);
        }

        if (entries.empty())
            menu.addItem (1, "No presets available", false);

        popupActive = true;
        repaint();

        juce::Component::SafePointer<PresetBar> safeThis (this);

        menu.showMenuAsync (juce::PopupMenu::Options()
                                .withTargetComponent (this)
                                .withPreferredPopupDirection (
                                    juce::PopupMenu::Options::PopupDirection::upwards),
                            [safeThis] (int result)
                            {
                                if (safeThis == nullptr)
                                    return;

                                safeThis->popupActive = false;
                                safeThis->repaint();

                                if (result > 0 && safeThis->onSelect != nullptr)
                                    safeThis->onSelect (result - 1);
                            });
    }

    std::vector<PresetEntry> entries;
    int index = -1;
    bool popupActive = false;

    juce::Rectangle<float> prevArea, nextArea, nameArea, saveArea, deleteArea, counterArea;

    float wheelAccum = 0.0f;
    juce::Point<float> lastMousePos { -1.0f, -1.0f };

    enum class Hover { none, name, prev, next, save, del, counter };
    Hover hover = Hover::none;
};

} // namespace ozo

namespace ozo::nightshift
{
    using PresetEntry = ozo::PresetEntry;
    using PresetBar   = ozo::PresetBar;
}
