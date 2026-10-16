# GLINT 0.1.1 — Studio One 两轨路由验证

## 本次变更

- VST3 从纯 Fx / 无音频总线改为 Instrument / Synth，0 输入、2 声道静音输出。
- 所有 float/double、正常/旁路回调清零音频，不会发出占位音或未初始化缓冲内容。
- MIDI 输入、输出和生成规则不变，Amount / Character / Output 和旧状态格式不变。
- AU 继续使用 aumi MIDI Processor，构造时保持零音频总线；不把 Logic MIDI FX 改成普通乐器。
- 产品版本为 0.1.1，保留原 VST3 class IDs / AU identity；宿主需重新扫描分类。

## 用户可验证的两轨接法

1. 在 Studio One 的 Instruments / 乐器列表寻找 ozo GLINT，拖入建立 A 乐器轨。
2. 在 A 写一小段 MIDI；GLINT 选择 Add，Amount 先设 0%。
3. 新建 B 乐器轨，加载钢琴。
4. 展开 B 的输入选择，寻找 GLINT 插件实例；如果存在，选中它并开启 B 的监听。
5. 播放 A 的 MIDI。先确认 Amount=0 时钢琴能演奏原始音符，再把 Amount 调到 30–60%，检查新增音符。
6. 停止播放、松开键盘和踏板，检查有没有挂音。实时弹奏时 A 同样需要监听。

A 自身的调音台通道保持静音是预期行为，不要使用音频 Send / Sidechain 代替 MIDI 路由。B 不要同时接键盘直接输入，以免重复击键。

若 GLINT 只出现在 Effects，先退出并重启 Studio One 或用其重新扫描功能；不要删除用户工程或自行清空整套插件数据库。

若 GLINT 已在 Instruments 但 B 输入仍没有 GLINT，记录这个结果：VST3 封装已识别为乐器，但宿主 MIDI 输出路由尚未成立。此时不要继续搭空中转轨。

## 验证边界

自建宿主能检查 Instrument 分类、音频总线、静音及 MIDI 事件，不能证明 Studio One 的输入菜单会暴露插件实例。本会话未提供桌面控制工具，因此没有点击 Studio One 完成路由实测。

## 本次自动化与安装结果

- 四组回归通过：38 项核心用例、Processor 静音/参数/状态、VST3 乐器分类及 MIDI 回录、AU MIDIProcessor 回录。
- VST3 manifest 为 Instrument / Synth，版本 0.1.1；component/controller IDs 与 0.1.0 相同。
- float/double 及旁路输出缓冲填入非零数据后均清零；真实 VST3 回录仍保持原 MIDI 时标。
- 真实旧版 MIDI Delay 的保存状态在新版恢复后，生成的 MIDI 逐事件一致。
- 已安装到 `~/Library/Audio/Plug-Ins/VST3/ozo GLINT.vst3`；安装后实际加载测试通过。
- 实际旧安装完整备份于 `archive/installed-plugins/glint-before-midi-instrument-20261010-172904/ozo GLINT.vst3`，不在插件扫描目录内。
- 旧 0.1.0 release 不覆盖；此次为本地路由试验，尚未作为完成 Studio One 验收的正式 release 发布。AU 已构建和测试，但未安装。

**用户后续已确认：**0.1.1 在其 Studio One 中的两轨路由跑通。截图排查时两条轨曾同时把 GLINT 选为乐器目标；钢琴轨应加载真正音源，并在检查器的 MIDI 输入中选择 GLINT、开启监听。这是用户当前环境的验收，不扩展为所有宿主兼容性承诺。助手未操控 DAW、未清理宿主缓存或修改用户工程。本文记录 0.1.1 历史结果，后续音乐算法更新见对应版本说明。
