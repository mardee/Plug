#pragma once

#include "Processor.h"
#include <functional>
#include <memory>
#include <numeric>
#include <vector>

// 在 Tests.cpp 的 CHECK、section 定义后引入；由已初始化 JUCE 的消息线程调用。
inline void runGroupEditorTests()
{
    section("分组编辑器：公开组件与真实回调");
    CHECK(juce::MessageManager::getInstance()->isThisTheMessageThread(),
          "分组编辑器测试运行于 JUCE 消息线程");
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread()) return;

    LatticeProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::function<juce::Component*(juce::Component*, const juce::String&)> findByID;
    findByID = [&](juce::Component* root, const juce::String& id) -> juce::Component*
    {
        if (root == nullptr) return nullptr;
        if (root->getComponentID() == id) return root;
        for (int i = 0; i < root->getNumChildComponents(); ++i)
            if (auto* found = findByID(root->getChildComponent(i), id)) return found;
        return nullptr;
    };
    auto button = [&](const juce::String& id)
    {
        return dynamic_cast<juce::Button*>(findByID(editor.get(), id));
    };
    auto click = [&](const juce::String& id)
    {
        auto* control = button(id);
        const auto message = id + " 存在且绑定真实 onClick";
        const bool available = control != nullptr && bool(control->onClick);
        CHECK(available, message.toRawUTF8());
        // 故意不根据 enabled 跳过：禁用状态之外，回调自身也必须拒绝非法草稿。
        if (available) control->onClick();
        return available;
    };
    auto tick = []
    {
        juce::Thread::sleep(80); // 编辑器定时器为 25 Hz，仅同步和生命周期测试需要等待。
        juce::Timer::callPendingTimersSynchronously();
    };
    auto snapshot = [&]
    {
        juce::ScopedLock lock(processor.modelLock);
        return processor.pattern;
    };
    auto sameBars = [](const lattice::Pattern& a, const lattice::Pattern& b)
    {
        for (int bar = 0; bar < lattice::phraseBars; ++bar)
            for (int row = 0; row < lattice::tracks; ++row)
            {
                const auto& x = a.bars[bar][row];
                const auto& y = b.bars[bar][row];
                if (x.size() != y.size()) return false;
                for (size_t cell = 0; cell < x.size(); ++cell)
                    if (x[cell].start != y[cell].start || x[cell].length != y[cell].length
                        || x[cell].velocity != y[cell].velocity) return false;
            }
        return true;
    };
    auto sameModel = [&](const lattice::Pattern& a, const lattice::Pattern& b)
    {
        return sameBars(a, b) && a.groups == b.groups
            && a.numerator == b.numerator && a.denominator == b.denominator
            && a.bpm == b.bpm && a.seed == b.seed && a.density == b.density
            && a.ghost == b.ghost && a.accents == b.accents && a.backbeat == b.backbeat
            && a.development == b.development && a.fill == b.fill && a.space == b.space
            && a.dispersion == b.dispersion && a.gridStep == b.gridStep
            && a.locked == b.locked && a.barLocked == b.barLocked;
    };

    juce::Component* panel = nullptr;
    juce::TextEditor* input = nullptr;
    juce::ComboBox* selection = nullptr;
    juce::Label* total = nullptr;
    juce::Label* message = nullptr;
    juce::Button* generate = nullptr;
    auto initialise = [&](int numerator, int denominator, const std::vector<int>& groups)
    {
        editor.reset();
        {
            juce::ScopedLock lock(processor.modelLock);
            auto& model = processor.pattern;
            model.meter(numerator, denominator);
            model.groups = groups;
            model.seed = 24681357;
            model.bpm = 137;
            model.gridStep = 120;
            model.locked[2] = true;
            model.barLocked[6] = true;
            // 每轨均有手工三连音、非标准力度，避免重新生成被误判为 bars 未变。
            for (int bar = 0; bar < lattice::phraseBars; ++bar)
                for (int row = 0; row < lattice::tracks; ++row)
                {
                    auto& lane = model.bars[bar][row];
                    lane.clear();
                    for (int start = 0; start < model.barTicks();)
                    {
                        const int length = start < 120 ? 40 : 60;
                        lane.push_back({start, length, 21 + (bar * 7 + row * 11 + start / 20) % 105});
                        start += length;
                    }
                }
        }
        processor.changed();
        editor.reset(processor.createEditor());
        CHECK(editor != nullptr, "通过 Processor 创建公开 AudioProcessorEditor");
        if (editor == nullptr) return false;
        editor->setVisible(true);
        panel = findByID(editor.get(), "group-editor");
        input = dynamic_cast<juce::TextEditor*>(findByID(editor.get(), "group-draft"));
        selection = dynamic_cast<juce::ComboBox*>(findByID(editor.get(), "group-selection"));
        total = dynamic_cast<juce::Label*>(findByID(editor.get(), "group-total"));
        message = dynamic_cast<juce::Label*>(findByID(editor.get(), "group-message"));
        const bool fields = panel != nullptr && input != nullptr && selection != nullptr
            && total != nullptr && message != nullptr;
        CHECK(fields, "递归 componentID 找到草稿浮层、文本框、分组 ComboBox 和提示");
        if (!fields) return false;
        CHECK(bool(input->onTextChange) && bool(selection->onChange), "草稿和 ComboBox 绑定真实回调");
        if (!input->onTextChange || !selection->onChange) return false;
        for (const auto* id : {"group-segment-0", "group-open", "group-summary", "group-apply",
                               "group-cancel", "group-split", "group-merge-next",
                               "group-preset-1", "group-preset-2", "group-preset-3"})
        {
            auto* control = button(id);
            const bool available = control != nullptr && bool(control->onClick);
            const auto description = juce::String(id) + " 可转换为 juce::Button 并调用";
            CHECK(available, description.toRawUTF8());
            if (!available) return false;
        }
        // 当前 GENERATE 没有 componentID；只对这个旧控件按公开文案递归定位。
        generate = nullptr;
        int generateCount = 0;
        std::function<void(juce::Component*)> findGenerate = [&](juce::Component* root)
        {
            if (auto* control = dynamic_cast<juce::Button*>(root))
                if (control->getButtonText() == "GENERATE")
                {
                    generate = control;
                    ++generateCount;
                }
            for (int i = 0; i < root->getNumChildComponents(); ++i)
                findGenerate(root->getChildComponent(i));
        };
        findGenerate(editor.get());
        const bool canGenerate = generateCount == 1 && generate != nullptr && bool(generate->onClick);
        CHECK(canGenerate, "递归找到唯一 GENERATE 按钮及真实 onClick");
        CHECK(!panel->isVisible(), "新编辑器的草稿浮层初始隐藏");
        return canGenerate;
    };
    auto editDraft = [&](const juce::String& value)
    {
        input->setText(value, false);
        input->onTextChange(); // 同步调用真实输入回调，不等待 TextEditor 的异步通知。
    };
    auto selectGroup = [&](int id)
    {
        selection->setSelectedId(id, juce::dontSendNotification);
        selection->onChange();
        CHECK(selection->getSelectedId() == id, "ComboBox 真实 onChange 保留选中分组");
    };
    auto checkDraft = [&](const juce::String& expected, int target)
    {
        CHECK(input->getText() == expected, "草稿与预期分组一致");
        int sum = 0;
        const auto parts = juce::StringArray::fromTokens(input->getText(), "+", "");
        for (const auto& part : parts) sum += part.getIntValue();
        CHECK(sum == target, "草稿八分音符总和保持拍号目标");
        CHECK(total->getText().contains(juce::String(target) + " / " + juce::String(target)),
              "总和提示同步显示正确目标");
        CHECK(button("group-apply")->isEnabled(), "合法草稿可应用");
    };

    if (!initialise(4, 4, {3, 3, 2})) return;
    const auto original = snapshot();
    if (!click("group-segment-0")) return;
    CHECK(panel->isVisible(), "顶部 group-segment-0 点击打开草稿");
    CHECK(sameModel(snapshot(), original), "顶部点击不隐式拆组、不修改任何模型字段");
    CHECK(selection->getSelectedId() == 1, "顶部第一段选中草稿第一组");
    {
        auto image = editor->createComponentSnapshot(editor->getLocalBounds());
        juce::File file("/Users/wangxuele/Documents/AI/Plug/LATTICE/build/group-editor-preview.png");
        juce::FileOutputStream output(file);output.setPosition(0);output.truncate();
        CHECK(output.openedOk() && juce::PNGImageFormat().writeImageToStream(image, output), "分组草稿真实编辑器截图可渲染");
    }

    section("分组草稿：非法输入、错误总和、空值与超大整数");
    for (const auto* invalid : {"3+x+2", "3++3+2", "3+ +3+2", "3+3+2+", "+3+3+2",
                                "0+8", "-1+9", "3.5+4.5", "3+2", "4+5", "", "   ",
                                "2147483647+1", "999999999999999999999999999999999999+2"})
    {
        const auto caseName = juce::String("拒绝草稿 [") + invalid + "]";
        section(caseName.toRawUTF8());
        editDraft(invalid);
        CHECK(!button("group-apply")->isEnabled(), "非法或总和错误草稿禁用 APPLY");
        CHECK(!button("group-split")->isEnabled() && !button("group-merge-next")->isEnabled(),
              "非法草稿禁用拆分和合并");
        CHECK(sameModel(snapshot(), original), "输入非法草稿不修改模型");
        click("group-apply");
        CHECK(panel->isVisible() && sameModel(snapshot(), original), "直接调用 APPLY 回调也拒绝非法草稿");
        generate->onClick();
        CHECK(panel->isVisible() && sameModel(snapshot(), original), "草稿未处理时 GENERATE 回调不生成且不修改模型");
    }

    click("group-cancel");
    CHECK(!panel->isVisible() && sameModel(snapshot(), original), "取消非法草稿保留完整模型");

    section("分组草稿：取消不改模型，应用只改 groups");
    click("group-open");
    checkDraft("3+3+2", 8);
    editDraft("4+4");
    checkDraft("4+4", 8);
    generate->onClick();
    CHECK(sameModel(snapshot(), original), "合法但未应用的草稿同样阻止生成");
    click("group-cancel");
    CHECK(!panel->isVisible() && sameModel(snapshot(), original), "取消合法草稿保留完整模型");
    click("group-apply");
    CHECK(sameModel(snapshot(), original), "浮层隐藏后 APPLY 回调不会提交已取消草稿");
    click("group-summary");
    checkDraft("3+3+2", 8);
    editDraft("4+4");
    click("group-apply");
    auto applied = original;
    applied.groups = {4, 4};
    CHECK(!panel->isVisible(), "成功应用后隐藏浮层");
    CHECK(sameBars(snapshot(), original), "应用保留全部 16 小节八轨 cell 起点、时值与力度");
    CHECK(sameModel(snapshot(), applied), "应用仅改 groups，拍号、seed、参数和两类锁均不变");
    tick();
    CHECK(sameModel(snapshot(), applied), "定时同步不会撤销已应用分组或改写音符");

    section("分组草稿：ComboBox 选择、拆分及合并保持总和");
    click("group-open");
    editDraft("3+3+2");
    selectGroup(2);
    click("group-split");
    checkDraft("3+2+1+2", 8);
    CHECK(sameModel(snapshot(), applied), "拆分仅修改草稿");
    click("group-merge-next");
    checkDraft("3+3+2", 8);
    CHECK(sameModel(snapshot(), applied), "合并仅修改草稿");
    selectGroup(3);
    CHECK(!button("group-merge-next")->isEnabled(), "最后一组不能向右合并");
    click("group-merge-next");
    checkDraft("3+3+2", 8);
    click("group-split");
    checkDraft("3+3+1+1", 8);
    selectGroup(3);
    CHECK(!button("group-split")->isEnabled(), "长度为一的分组不能继续拆分");
    click("group-split");
    checkDraft("3+3+1+1", 8);
    click("group-cancel");
    CHECK(sameModel(snapshot(), applied), "拆合后取消仍保留原完整模型");

    section("分组预设：5/4 与 7/8 上下入口均先暂存再应用");
    struct PresetCase { int numerator, denominator, index; const char* id; const char* text; std::vector<int> groups; };
    const PresetCase presets[] = {
        {5, 4, 1, "group-54-64", "6+4", {6, 4}},
        {5, 4, 2, "group-54-46", "4+6", {4, 6}},
        {7, 8, 1, "group-78-322", "3+2+2", {3, 2, 2}},
        {7, 8, 2, "group-78-223", "2+2+3", {2, 2, 3}},
        {7, 8, 3, "group-78-232", "2+3+2", {2, 3, 2}}
    };
    for (const auto& preset : presets)
    {
        const int target = preset.numerator * 8 / preset.denominator;
        if (!initialise(preset.numerator, preset.denominator, {target})) return;
        const auto before = snapshot();
        auto* quick = button(preset.id);
        CHECK(quick != nullptr && quick->isVisible() && quick->getButtonText() == preset.text,
              "对应拍号的快捷预设可见且文案正确");
        if (!click(preset.id)) return;
        CHECK(panel->isVisible(), "快捷预设打开草稿而不是直接应用");
        checkDraft(preset.text, target);
        CHECK(sameModel(snapshot(), before), "快捷预设点击不修改模型");
        click("group-cancel");
        CHECK(sameModel(snapshot(), before), "取消快捷预设不修改模型");
        click("group-open");
        auto* floatingPreset = button("group-preset-" + juce::String(preset.index));
        CHECK(floatingPreset->isVisible() && floatingPreset->getButtonText() == preset.text,
              "浮层预设使用正确八分音符单位及顺序");
        CHECK(button("group-preset-3")->isVisible() == (preset.numerator == 7),
              "5/4 仅显示两个预设，7/8 显示三个预设");
        click("group-preset-" + juce::String(preset.index));
        checkDraft(preset.text, target);
        CHECK(sameModel(snapshot(), before), "浮层预设也只改变草稿");
        click("group-apply");
        auto expected = before;
        expected.groups = preset.groups;
        CHECK(!panel->isVisible() && sameModel(snapshot(), expected), "预设应用结果精确且仅改 groups");
        const auto actual = snapshot();
        CHECK(std::accumulate(actual.groups.begin(), actual.groups.end(), 0) == target,
              "应用后的模型分组总和与拍号一致");
    }

    section("分组草稿：外部拍号或分组变化拒绝旧草稿覆盖");
    for (int conflict = 0; conflict < 4; ++conflict)
    {
        if (!initialise(4, 4, {3, 3, 2})) return;
        click("group-open");
        editDraft("4+4");
        {
            juce::ScopedLock lock(processor.modelLock);
            if (conflict == 0) processor.pattern.numerator = 8;
            else if (conflict == 1) processor.pattern.denominator = 8;
            else if (conflict == 2)
            {
                // 4/4 → 8/8 总和不变，仍必须依据拍号快照拒绝旧草稿。
                processor.pattern.numerator = 8;
                processor.pattern.denominator = 8;
            }
            else processor.pattern.groups = {2, 2, 2, 2};
        }
        processor.changed();
        const auto external = snapshot();
        if (conflict != 0) tick(); // 同时覆盖 timer 同步之前与之后的冲突检测。
        CHECK(input->getText() == "4+4", "外部同步不会静默改写正在编辑的草稿");
        click("group-apply");
        CHECK(panel->isVisible() && sameModel(snapshot(), external), "外部状态变更后拒绝旧草稿覆盖");
        CHECK(message->getText().containsIgnoreCase("changed externally"), "冲突提示要求取消并重新打开");
        generate->onClick();
        CHECK(sameModel(snapshot(), external), "冲突未处理时仍不能生成");
        click("group-cancel");
        CHECK(!panel->isVisible() && sameModel(snapshot(), external), "取消冲突草稿保留外部状态");
        click("group-open");
        const int newTarget = external.numerator * 8 / external.denominator;
        editDraft(juce::String(newTarget));
        click("group-apply");
        auto expected = external;
        expected.groups = {newTarget};
        CHECK(!panel->isVisible() && sameModel(snapshot(), expected), "重新打开后基于新快照可合法应用");
    }

    section("分组草稿：重复开关、销毁与定时器安全");
    if (!initialise(4, 4, {3, 3, 2})) return;
    const auto lifecycleModel = snapshot();
    for (int cycle = 0; cycle < 8; ++cycle)
    {
        click("group-segment-0");
        checkDraft("3+3+2", 8);
        editDraft("4+4");
        click("group-summary");
        click("group-open");
        CHECK(panel->isVisible() && input->getText() == "4+4", "重复打开复用浮层且不重置未提交草稿");
        click("group-cancel");
        click("group-cancel");
        CHECK(!panel->isVisible() && sameModel(snapshot(), lifecycleModel), "重复取消安全且不修改模型");
    }
    for (bool destroyWhileOpen : {false, true})
    {
        if (!initialise(4, 4, {3, 3, 2})) return;
        const auto before = snapshot();
        click("group-open");
        editDraft("4+4");
        if (!destroyWhileOpen) click("group-cancel");
        tick();
        CHECK(sameModel(snapshot(), before), "开关浮层后的定时器不提交草稿");
        juce::Component::SafePointer<juce::Component> safeEditor(editor.get());
        juce::Component::SafePointer<juce::Component> safePanel(panel);
        juce::Component::SafePointer<juce::TextEditor> safeInput(input);
        editor.reset();
        CHECK(safeEditor == nullptr && safePanel == nullptr && safeInput == nullptr,
              "公开 SafePointer 确认编辑器及草稿子组件全部销毁");
        tick();
        CHECK(sameModel(snapshot(), before), "浮层打开或关闭时销毁，后续 timer 安全且模型不变");
    }
}
