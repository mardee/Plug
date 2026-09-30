#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

#include "OzoLookAndFeel.h"

namespace ozo
{

//==============================================================================
// 一条预设。出厂的和用户存的都走这个结构，预设条不区分来源，只看这些字段。
struct PresetEntry
{
    juce::String name;
    bool factory = true;    // 出厂预设删不掉
    bool wild    = false;   // 决定圆点的颜色
};

//==============================================================================
// 预设条。
//
// 一条胶囊：左箭头、当前预设名、右箭头，最右侧是存和删两个小圆按钮。
// 点名字向上弹出完整列表。滚轮也能切换。
//
// 组件本身不碰参数。切换、保存、删除都通过回调交出去。
class PresetBar : public juce::Component
{
public:
    std::function<void (int)> onSelect;    // 选中第 index 个
    std::function<void ()>    onSave;      // 想存一个新的
    std::function<void (int)> onDelete;    // 想删第 index 个

    void setEntries (std::vector<PresetEntry> list, int selected)
    {
        entries = std::move (list);
        index   = selected;
        repaint();
    }

    int getIndex() const noexcept { return index; }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (0.5f);

        g.setColour (OzoCol::panelInner);
        g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
        g.setColour (OzoCol::panelEdge);
        g.drawRoundedRectangle (b, b.getHeight() * 0.5f, 1.0f);

        const float h = b.getHeight();

        // ---- 右侧动作按钮区（+ 存 / × 删），不画外圈圆圈 ----
        const float actionBtnW = 24.0f;
        deleteArea = { b.getRight() - actionBtnW - 6.0f, b.getY(), actionBtnW, h };
        saveArea   = { deleteArea.getX() - actionBtnW - 2.0f, b.getY(), actionBtnW, h };

        // 分隔线：在右箭头和新建按钮之间拉开清晰间距，并画一条极淡的竖分隔线
        const float dividerX = saveArea.getX() - 6.0f;
        g.setColour (OzoCol::panelEdge.withAlpha (0.50f));
        g.drawLine (dividerX, b.getY() + 7.0f, dividerX, b.getBottom() - 7.0f, 1.0f);

        // ---- 翻页与名称区 ----
        const float arrowW = 26.0f;
        prevArea = { b.getX() + 2.0f, b.getY(), arrowW, h };
        nextArea = { dividerX - 10.0f - arrowW, b.getY(), arrowW, h };
        nameArea = { prevArea.getRight(), b.getY(),
                     nextArea.getX() - prevArea.getRight(), h };

        // dir < 0 画向左的箭头，dir > 0 画向右的
        drawArrow (g, prevArea, -1.0f, index > 0, hover == Hover::prev);
        drawArrow (g, nextArea, +1.0f,
                   index < (int) entries.size() - 1, hover == Hover::next);

        // + 存 / × 删：纯图标绘制，不画圆圈底座
        drawIconButton (g, saveArea, true, true, hover == Hover::save);

        const bool canDelete = index >= 0 && index < (int) entries.size()
                            && ! entries[(size_t) index].factory;
        drawIconButton (g, deleteArea, false, canDelete, hover == Hover::del);

        drawName (g);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto p = e.position;

        if (prevArea.contains (p)) { step (-1); return; }
        if (nextArea.contains (p)) { step (+1); return; }

        if (saveArea.contains (p))
        {
            if (onSave != nullptr) onSave();
            return;
        }

        if (deleteArea.contains (p))
        {
            const bool canDelete = index >= 0 && index < (int) entries.size()
                                && ! entries[(size_t) index].factory;

            if (canDelete && onDelete != nullptr)
                onDelete (index);

            return;
        }

        // 点名字：列表向上展开
        if (nameArea.contains (p))
            showMenu();
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
        const auto next = prevArea.contains (e.position)   ? Hover::prev
                        : nextArea.contains (e.position)   ? Hover::next
                        : saveArea.contains (e.position)   ? Hover::save
                        : deleteArea.contains (e.position) ? Hover::del
                        : Hover::none;

        if (next != hover) { hover = next; repaint(); }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (hover != Hover::none) { hover = Hover::none; repaint(); }
    }

private:
    void drawArrow (juce::Graphics& g, juce::Rectangle<float> area, float dir,
                    bool enabled, bool hot)
    {
        const float len = 5.0f;
        const auto c = area.getCentre();

        // dir = -1 时箭头朝左，+1 时朝右
        juce::Path p;
        p.startNewSubPath (c.x - dir * len * 0.3f, c.y - len);
        p.lineTo          (c.x + dir * len * 0.7f, c.y);
        p.lineTo          (c.x - dir * len * 0.3f, c.y + len);

        g.setColour (! enabled ? OzoCol::textFaint.withAlpha (0.35f)
                               : hot ? OzoCol::text : OzoCol::textDim);
        g.strokePath (p, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }

    // plus = true 画加号，false 画叉（不画外圈圆圈）
    void drawIconButton (juce::Graphics& g, juce::Rectangle<float> area, bool plus, bool enabled, bool hot)
    {
        const float s = 4.0f;
        const auto c = area.getCentre();

        if (! enabled)
            g.setColour (OzoCol::textFaint.withAlpha (0.30f));
        else if (hot)
            g.setColour (OzoCol::text);
        else
            g.setColour (OzoCol::textDim);

        if (plus)
        {
            g.drawLine (c.x - s, c.y, c.x + s, c.y, 1.5f);
            g.drawLine (c.x, c.y - s, c.x, c.y + s, 1.5f);
        }
        else
        {
            g.drawLine (c.x - s, c.y - s, c.x + s, c.y + s, 1.5f);
            g.drawLine (c.x - s, c.y + s, c.x + s, c.y - s, 1.5f);
        }
    }

    void drawName (juce::Graphics& g)
    {
        const bool selected = index >= 0 && index < (int) entries.size();

        if (selected)
        {
            const auto& e = entries[(size_t) index];
            const float r  = 3.5f;
            const float cx = nameArea.getX() + 14.0f;
            const float cy = nameArea.getCentreY();

            g.setColour (e.wild ? OzoCol::meterRed : OzoCol::mint);
            if (e.factory)
                g.drawEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f, 1.4f);
            else
                g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);
        }

        g.setColour (selected ? OzoCol::text : OzoCol::textFaint);
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));

        const auto text = nameArea.withTrimmedLeft (selected ? 24.0f : 12.0f).withTrimmedRight (6.0f);
        g.drawText (selected ? entries[(size_t) index].name : juce::String ("No preset"),
                    text, juce::Justification::centredLeft, true);
    }

    void step (int delta)
    {
        if (entries.empty())
            return;

        // 没选中时，按右箭头从第一个开始，按左箭头从最后一个开始
        int base = index;
        if (base < 0)
            base = delta > 0 ? -1 : (int) entries.size();

        const int next = juce::jlimit (0, (int) entries.size() - 1, base + delta);

        if (next != index && onSelect != nullptr)
            onSelect (next);
    }

    void showMenu()
    {
        juce::PopupMenu menu;
        bool mineHeader = false;

        for (int i = 0; i < (int) entries.size(); ++i)
        {
            if (i == 0)
                menu.addSectionHeader ("Factory");

            if (! entries[(size_t) i].factory && ! mineHeader)
            {
                menu.addSeparator();
                menu.addSectionHeader ("Mine");
                mineHeader = true;
            }

            menu.addItem (i + 1, entries[(size_t) i].name, true, i == index);
        }

        // 向上展开：锚在组件上沿，方向优先往上
        menu.showMenuAsync (juce::PopupMenu::Options()
                                .withTargetComponent (this)
                                .withPreferredPopupDirection (
                                    juce::PopupMenu::Options::PopupDirection::upwards),
                            [this] (int result)
                            {
                                if (result > 0 && onSelect != nullptr)
                                    onSelect (result - 1);
                            });
    }

    std::vector<PresetEntry> entries;
    int index = -1;

    juce::Rectangle<float> prevArea, nextArea, nameArea, saveArea, deleteArea;

    float wheelAccum = 0.0f;

    enum class Hover { none, prev, next, save, del };
    Hover hover = Hover::none;
};

} // namespace ozo
