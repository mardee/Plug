# GLINT 0.3.0 精简回归

按精简验收约定，默认只跑最重要的回归测试：

- `glint.core`：默认 12 项 smoke（覆盖原始 MIDI 保持、On/Off 配对、踏板与停播安全、7 条声部独立首音延迟与重复间隔响应、波动衰减、调内音程音级计算及生命周期硬上限）。
- `glint.vst3-roundtrip`：真实 VST3 宿主加载、静音乐器接口、MIDI 透传和调内音程、状态保存复原。
- `glint.processor`：60 项参数与 v6 状态保存、旧版 v1–v5 状态安全迁移、本地剪贴板 Copy/Paste 验证与界面 60 个参数绑定（22 sliders, 31 combos, 7 toggles）。
- `glint.au-roundtrip`：macOS AU 实际 MIDI 回录。

```sh
ctest --preset glint
ctest --preset glint-au  # macOS AU 测试
```

核心单测保留于 `ozoGlintCoreTests`，默认 `--smoke` 执行 12 项 smoke。音乐性最终以真实音源试听为准。
