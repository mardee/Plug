# ozo GLINT 验证说明

本页说明 0.1.0 的验证方法；GLINT 更名后的重建、回归和旧版状态迁移结果见 [验证记录](../VALIDATION.md)。

测试分三层，避免把“核心逻辑通过”误报成“宿主能用”。

1. `core_tests.cpp` 直接驱动无 JUCE 引擎，验证生命周期、时序和生成规则。目标为 `ozoGlintCoreTests`，CTest 名为 `glint.core`。
2. `processor_tests.cpp` 直接实例化 Processor，验证参数、状态和最简界面的控件绑定。目标为 `ozoGlintProcessorTests`，CTest 名为 `glint.processor`。额外传一个不存在的 PNG 绝对路径，可以保存当前界面快照。
3. `host_tests.cpp` 加载真实 VST3 包（目标 `ozoGlintHostTests`，CTest `glint.vst3-roundtrip`）；`au_host_tests.mm` 在进程内加载真实 AU 包（目标 `ozoGlintAUHostTests`，CTest `glint.au-roundtrip`）。两者验证封装后的 MIDI 输出，不安装到用户插件目录。

构建和 CTest 命令见 [上级 README](../README.md#构建)，使用 `OZO_BUILD_GLINT=ON` 和 `build/glint`。GUI 快照测试只需运行一次输出路径，已有文件不覆盖。

## 真实 DAW 手工验收

自动化通过后仍需在目标 DAW 中验证以下项目，不能仅凭自建测试宿主推定通过：

- 更名兼容性：由用户先备份工程、预设和旧安装包，移除旧品牌文件，仅安装 ozo GLINT，不新旧同时安装；打开原名版本保存的工程，确认同一插件被正确识别，参数、自动化与状态恢复一致。此项尚未通过真实 DAW 验证，不能以身份稳定代替实测。
- 能扫描、插入插件，且 MIDI 输出能到达下游音源。
- Add 下 Amount=0 时原始演奏完整；Amount=30% 时可听见附加音符。
- Particles 分轨不重复输出原始击键；主体轨不受粒子模式影响。
- 参数自动化和工程重开保持设置，无需打开界面才能运行。
- 实时键盘、MIDI 片段、延音踏板、循环、停止、定位与旁路行为正常。
- 停止处理或卸载插件时，宿主/音源正确执行自身的音符清理。
- 离线渲染和实时回录的节奏一致，记录宿主/插件格式/版本和块大小。

实际音源试听还需要检查默认值是否过稀或过密，以及高 Amount 是否抢占主体。测试程序只能证明事件规则，不证明音色品质。
