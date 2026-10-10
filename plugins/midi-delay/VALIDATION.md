# ozo MIDI Delay 0.1.0 验证记录

日期：2026-10-10。平台：本地 Apple Silicon macOS，AppleClang 17，JUCE 9.0.2，C++17。

## 已完成

- Release 构建：arm64 VST3 和 AU MIDI Processor。
- 最低 macOS 链接版本设为 12.0；通过 `vtool -show-build` 检查。
- AU/VST3 完整 bundle 的 ad-hoc 签名通过 `codesign --verify --deep --strict`。
- `midi.core`：38 个命名测试通过。
- 核心 UBSan：38/38 通过，无未定义行为诊断。
- `midi.processor`：默认参数、空音频总线、float/double 处理、零长度块、旁路、状态往返、非法状态拒绝、界面创建和三个控件绑定通过。
- `midi.vst3-roundtrip`：加载本次构建包，MIDI 音符/踏板/样本偏移透传通过；三种 Character 生成音高及最终 On/Off 平衡通过；无 Editor 状态恢复通过。
- `midi.au-roundtrip`：进程内加载本次构建包，直接接收原生 AU MIDI callback，Note On/Off 和 CC64 样本偏移通过；Particles 生成及最终清理通过。
- 核心不同块长（32/64/128/512/1024）、三种 Character、两种输出模式，完整有序 MIDI 轨迹完全一致。
- 核心固定输入/输出探针检查 process 无 C++ new/delete，且输出消息数量有界。此探针不覆盖直接 malloc/realloc 或其他线程。
- 最简原生界面（430×300）已生成 2× 预览并视觉检查，无控件越界；Amount / Character / Output 绑定通过测试。
- 现有 `build` 中 PRISM 的 `prism.dsp` 和 `prism.vst3-load` 两项基线测试通过；未重建或修改 PRISM 的音频实现。

## 修复的集成问题

- Objective-C++ 在子目录启用导致 CMake 编译规则不可见：改为仅启用新插件时，在根目录初始化语言。
- VST3 manifest 在初次签名后写入导致封包签名失效：在完整 bundle 生成后重新 ad-hoc 签名，AU 同样最终签名。
- Processor 测试复用插件 target 时 standalone 宏冲突：使用普通可执行测试目标，避免重复定义。
- 高强度音区补齐 +2 八度，并增加专门的 Amount/Character 音区测试。
- 无分配测试对齐内存分配增加 MSVC 对应接口，避免依赖 MSVC 未提供的 std::aligned_alloc；尚未在 Windows 编译验证。

## 未完成 / 限制

- 未在 Logic、REAPER、Studio One 或其他真实 DAW 中操作、回录或试听。不把自建测试宿主结果当作这些宿主的兼容性保证。
- 未跨实际钢琴、拨弦和合成器音源调音；默认宏曲线仍为原型。
- 无 Windows、Intel Mac 构建或真实 macOS 12 系统验证。
- 没有 Developer ID 签名或 Apple 公证；没有安装到用户/系统插件目录。
- AddressSanitizer 未完成：本机运行时进入 main 前初始化阻塞，已停止；不能计为通过。UBSan 已独立通过。
- JUCE 适配层首次 buffer 交换、宿主更换小缓冲或超大 MIDI/SysEx 时仍可能分配；严格无动态分配的边界是核心处理及已测试的固定容量回调，不是无限制输入的完整宿主适配层。
- Add 模式共享踏板和 MIDI 1.0 同键语义，不能保证单独强制静音某个粒子；外部音源 release 和复音抢占也不受控制。
- 宿主停止回调或硬旁路时无法保证发出最终 Off；`prepareToPlay` 后是新引擎状态，宿主需妥善结束旧音符。

## 运行

```sh
ctest --test-dir build/midi-delay -R '^midi\.' --output-on-failure
```

此记录仅适用于当前源码和本次产物；修改核心、封装或参数后需要重新运行。
