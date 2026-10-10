# ozo MIDI Delay

演奏驱动的 MIDI 粒子效果器，0.1.0 原型。它生成音符，不生成音频；必须放在能接收其 MIDI 输出的音源之前。

## 控制

- **Amount**：粒子的整体活跃程度。默认 30%；0 不产生新粒子。
- **Character**：ECHO 为较规则的回应，BLOOM 为非等距的音符云，PULSE 为网格脉冲。默认 BLOOM。
- **Output**：Add 保留原始演奏并加入粒子；Particles 只输出粒子，适合分轨。

不需要设置调式、音阶、步进或反馈。粒子音高来自演奏音符和它们的上方八度。MIDI 力度并不等于外部音源的音量。

## 接线

最简单的方式：

```text
MIDI 键盘 / MIDI 片段 → ozo MIDI Delay [Add] → 钢琴 / 合成器
```

分轨方式：

```text
原始 MIDI ──┬────────────────────────────→ 主体音源
            └→ ozo MIDI Delay [Particles] → 粒子音源 → 混响等音频效果
```

AU 类型为 MIDI Processor，应使用宿主的 MIDI FX 位置，不是普通音频 insert。VST3 需要宿主支持插件 MIDI 输入/输出；有的宿主可直接串联，有的需要额外轨道路由。仅支持 VST3 并不代表支持该接法。

这不是独立发声乐器。没有下游音源时听不到声音是正常的。音源材质、混响及音频电平由下游决定。

### 输出模式切换

持音或踏板尚未结束时，输出模式更改会等待 MIDI 空闲，以免截断原声或交叉发送 Note Off。界面显示等待状态；释放按键和踏板、等待粒子结束后生效。

### 延音踏板和结束音符

Add 模式原声与粒子共用踏板；Amount=0 只停止生成并释放插件按住的生成音符，不保证踏板中的声音立即静音。长时间踩住踏板会占用粒子预算，预算用完后暂缓生成，不无限堆积。

Particles 模式把踏板用于理解输入，但不把它发给粒子音源。其他普通 CC、程序切换、弯音及压力也不转发到这条输出。

外部音源的 release、复音抢占和宿主硬旁路均不由本插件控制。宿主若暂停处理，插件无法发出最后的清理事件；必要时使用宿主或音源的停止发声功能。

## 构建

从仓库根目录运行，需要 CMake 3.22+、C++17 和仓库内的 JUCE 9.0.2。

```sh
cmake -S . -B build/midi-delay -DCMAKE_BUILD_TYPE=Release \
  -DOZO_BUILD_MIDI_DELAY=ON -DPRISM_BUILD_SNAPSHOT=OFF -DBUILD_TESTING=ON
cmake --build build/midi-delay --config Release \
  --target ozoMidiDelay_VST3 ozoMidiCoreTests ozoMidiHostTests ozoMidiProcessorTests --parallel
ctest --test-dir build/midi-delay -C Release -R '^midi\.(core|vst3-roundtrip|processor)$' --output-on-failure
```

macOS 额外构建 AU 与原生 AU 回录测试：

```sh
cmake --build build/midi-delay --config Release --target ozoMidiDelay_AU ozoMidiAUHostTests --parallel
ctest --test-dir build/midi-delay -C Release -R '^midi\.au-roundtrip$' --output-on-failure
```

Windows 使用已配置 MSVC 的开发命令行；`--config Release` 适用于 Visual Studio 多配置生成器。AU 目标仅在 macOS 存在。本目录尚不包含独立应用或第三方插件托管。

`OZO_BUILD_MIDI_DELAY` 默认关闭，不改变原有 PRISM 构建和发布目标。不会自动拷贝插件到系统目录。macOS 构建会在 AU/VST3 完整 bundle 生成后进行 ad-hoc 签名；不是 Developer ID 签名或公证。

本次交付为 Apple Silicon arm64，本地按 `-DCMAKE_OSX_DEPLOYMENT_TARGET=12.0` 构建；这只是二进制最低版本设置，未在 macOS 12 实机验证。没有生成 Windows 或 Intel Mac 包。

## 本地发布归档

发布产物统一放在仓库根目录的 `release/ozo-midi-delay/<版本>/<平台架构>/`，二进制不提交到 Git。0.1.0 本地归档布局为：

```text
release/ozo-midi-delay/0.1.0/
  macos-arm64/
    VST3/ozo MIDI Delay.vst3/
    AU/ozo MIDI Delay.component/
    docs/
    INSTALL.txt
    BUILD-INFO.txt
    SHA256SUMS.txt
  ozo-midi-delay-0.1.0-macos-arm64.zip
  ozo-midi-delay-0.1.0-macos-arm64.zip.sha256
```

`BUILD-INFO.txt` 记录源码提交号、架构、最低系统版本和验证边界；`SHA256SUMS.txt` 校验解包文件，ZIP 的 `.sha256` 校验下载包。此目录是本地归档，不代表已发布到 GitHub Release。

## 自动化测试

- `midi.core`：独立核心的音符、踏板、时序、模式、上限及确定性检查。
- `midi.processor`：JUCE 参数、状态验证、最简界面与控件绑定。
- `midi.vst3-roundtrip`：加载本次构建 VST3，回录真实 MIDI 输出并检查时标和 Note On/Off。
- `midi.au-roundtrip`：进程内加载 AU，直接接收 MIDI callback，避免中间宿主抹掉样本偏移。

这些测试不等于已经在 Logic、REAPER、Studio One 等 DAW 完成兼容性验收，也不替代真实音源试听。

完整产品边界见 [架构设计](../../docs/MIDI_PARTICLE_ARCHITECTURE.md)。
