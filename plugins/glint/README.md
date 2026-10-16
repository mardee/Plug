# ozo NIGHTSHIFT

**0.3.0：七路 MIDI 粒子延迟与夜航机场交互界面。** 产品沿用原 GLINT 的插件身份与参数协议。

安装与独立跨平台构建见 [INSTALL.txt](INSTALL.txt) 和 [RELEASE_NOTES.md](RELEASE_NOTES.md)。该版本仍是测试构建，完整引擎历史失败及平台验证边界在发布说明中列出。

七条固定音程关系声部：原音、上三度、上五度、上八度、下三度、下五度、下八度。取消随机黑盒，每条声部提供完全独立的手动控制，用于精调出理想的声音组合。详见 [0.3.0 七 lane 调音工作台](MANUAL_LANES_030.md)。历史算法详见 [0.2.3 衰减与生命周期](DECAY_LIFETIME_023.md) 及 [并行声部](LANES_022.md)。

这是前置 MIDI 生成乐器，不处理音频，也不自带钢琴。统一随宿主速度换算节奏，生成音符以原音力度与音域为基准。

## 控制

- **Amount**：全局强度，默认 30% 为基准倍率；置 0% 停止生成。
- **Root / Scale**：调性设置。Major 为自然大调，Minor 为自然小调。未设调性（Not set）时不猜三五度，仅同度和八度可用。
- **Output**：Add 保留原旋律并加入附加音符；Particles 只输出附加音符（用于分轨路由）。
- **七行独立 Lane 控制**：
  - **On**：声部开关，不再受 Amount 门限随机干涉。
  - **First**：原音触发到首颗回声的时值（1/32 到 1/2 含附点共 10 档）。
  - **Repeat**：后续重复间隔（10 档音符时值）。
  - **Feedback**：周期保留力度比例（0..98%），决定回声延续寿命。
  - **Level**：相对原音 velocity 的首次发声力度比例（0..100%）。
  - **Length**：生成音符 gate 按键长度（10 档音符时值）。
  - **Motion**：衰减包络内的力度波动深度（0..100%）。
  - **Wave cycle**：波动力度周期（1 / 2 / 4 / 8 拍）。

## 预设与详情

- 左下角沿用 PRISM 的预设条架构，提供 Factory / Mine 列表、前后切换、保存及删除。
- 用户预设保存完整 60 项参数到本机 XML；工厂预设只读，同名覆盖和删除前确认。
- 主面板的 First / Repeat / Feedback / Level 均为旋钮，数值仅在拖动时显示。
- 点击每路面板空白处展开全部八项设置；详情旋钮常驻显示当前配置并与主面板同步。
- 关闭轨道后标签与读数变暗，仍可编辑；品牌标题不参与控制面板的闲置淡出。

## Studio One

```text
轨道A：MIDI片段/键盘 → ozo NIGHTSHIFT
                          ↓ MIDI
轨道B：输入选NIGHTSHIFT → 钢琴音源 → 音频输出
```

NIGHTSHIFT VST3 在 Instruments 中，输出音频静音是预期行为。钢琴轨的检查器输入选择 NIGHTSHIFT、监听开启。AU 仍是 MIDI Processor。

建议先旁路后端 GLINT FX，用裸钢琴比较。声音质感取决于音源，不把规则通过测试等同于已完成听感验收。

## 兼容边界与状态迁移

- 当前使用 **v6 状态**，保存 60 个参数（4 全局 + 7×8 lane 参数）。
- 旧版 **v1–v5 状态** 严格校验后自动迁移全局参数与 seed，并填入默认 lane 推荐表。
- 删除的旧模式自动化不承诺无缝还原，升级前建议保留工程副本。

## 工程与构建

```sh
# JUCE 9.0.2 位于仓库 JUCE/ 目录
cmake -S cmake/nightshift -B build/nightshift -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build/nightshift --config Release --target ozoGLINT_VST3 --parallel 4
# macOS AU
cmake --build build/nightshift --config Release --target ozoGLINT_AU --parallel 4
```

源码位于 `plugins/glint/`。独立入口 `cmake/nightshift` 不构建 PRISM 或 LATTICE。Windows 在 MSVC x64 环境使用 `-G Ninja`。

如需有限检查，将 `BUILD_TESTING` 设为 `ON`，只构建并运行需要的测试目标，不默认启动真实窗口测试。

[0.3.0 七 lane 调音工作台](MANUAL_LANES_030.md) · [0.2.3 衰减与生命周期](DECAY_LIFETIME_023.md) · [并行声部设计](LANES_022.md) · [连贯版调整](FLOW_021.md) · [单一闪光设计与验证](SPARKLE_020.md) · [历史验证记录](VALIDATION.md)
