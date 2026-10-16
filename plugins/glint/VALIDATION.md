# ozo GLINT 验证记录

## 当前版本 0.1.4

当前完整记录见 [模式、八度与混合节奏验证](OCTAVES_MIXED_014.md#014-验证结果)。69项核心及UBSan通过，四组插件回归通过，v3状态兼容读取v1/v2。VST3为带静音双声道输出的MIDI乐器，AU保持MIDIProcessor。以下保留0.1.0更名阶段的历史记录，其UI尺寸、状态版本和逐事件兼容结论不代表当前算法。

# ozo GLINT 0.1.0 历史验证记录

日期：2026-10-10。平台：本地 Apple Silicon macOS，AppleClang 17，JUCE 9.0.2，C++17。正式品牌为 ozo GLINT，原工作名为 ozo MIDI Delay。

## GLINT 更名后已完成

- 从 `plugins/glint` 独立构建新的 `ozo GLINT.vst3` 和 `ozo GLINT.component`，不是仅修改旧 bundle 文件名。
- `glint.core`：38 个命名测试重新通过。核心源文件与更名前提交逐字节一致，未修改生成算法或默认值。
- `glint.processor`：默认参数、float/double 处理、零长度块、旁路、状态往返、非法状态拒绝、最简界面及三个控件绑定通过；新增正式产品名、界面标题及固定旧版 v1 状态兼容断言。
- `glint.vst3-roundtrip`：扫描名称为 ozo GLINT；音符/踏板样本偏移透传、三种 Character 生成及 On/Off 平衡、无 Editor 状态恢复通过。
- `glint.au-roundtrip`：进程内加载 GLINT AU，直接接收 MIDI callback，Note On/Off 和 CC64 样本偏移、Particles 生成和最终清理通过。
- 额外加载旧版真实 VST3，保存参数/随机状态后交给 GLINT 恢复；同一 MIDI 输入产生逐字节、逐样本相同的输出。
- VST3 component/controller FUID 与旧版逐项相同；新 manifest 中工厂产品名称为 ozo GLINT。
- AU 的 bundle ID、type/subtype/manufacturer/version 与旧版相同；新品牌工厂符号 `ozo_GLINTAUFactory` 与 Info.plist 匹配且实际导出。
- AU/VST3 完整 bundle 的 ad-hoc 签名通过 `codesign --verify --deep --strict`。
- 最简界面（430×300）已重新生成 2× GLINT 预览并检查，无控件越界。

## 保持的兼容身份

- VST3 component CID：`ABCDEF019182FAEB4F7A6F414F6D6964`。
- VST3 controller CID：`ABCDEF011234ABCD4F7A6F414F6D6964`。
- AU：`aumi / Omid / OzoA`。
- bundle ID：`cn.ozo.mididelay`。
- 状态根：`OZO_MIDI_DELAY_STATE`，version 1，参数 `amount` / `character` / `outputMode`。

更名是同一插件的替换，不是可同时安装的第二个产品。旧工程在真实 DAW 中是否正常重新定位还需实测，详见 [安装与兼容](README.md#更名与兼容)。

## 原名阶段已验证并保持的核心行为

- 核心 UBSan：38/38 通过，无未定义行为诊断；更名未更改核心文件。
- 核心不同块长（32/64/128/512/1024）、三种 Character、两种输出模式，完整有序 MIDI 轨迹完全一致；GLINT 核心测试再次覆盖这些用例。
- 固定输入/输出探针检查 process 无 C++ new/delete，且消息数量有界；不覆盖直接 malloc/realloc 或其他线程。
- 原名阶段现有 PRISM 的 `prism.dsp` 和 `prism.vst3-load` 两项基线测试通过；本次未修改 PRISM 音频实现或产品身份。

## 未完成 / 持续限制

- 未在 Logic、REAPER、Studio One 或其他真实 DAW 中操作、回录或试听，不把自建测试宿主结论当作完整兼容性保证。
- 未跨实际钢琴、拨弦和合成器音源调音；默认宏曲线仍为原型。
- 当前仅 arm64 包，部署目标 macOS 12.0；没有 Windows、Intel Mac 构建或真实 macOS 12 系统验证。
- 没有 Developer ID 签名或 Apple 公证，没有安装到用户/系统插件目录。
- AddressSanitizer 在原名阶段未完成：本机运行时进入 main 前初始化阻塞；不能计为通过。
- JUCE 适配层首次 buffer 交换、宿主更换小缓冲或超大 MIDI/SysEx 时仍可能分配；严格无动态分配边界是核心及已测试固定容量回调。
- Add 模式共享踏板及 MIDI 1.0 同键语义，不能保证单独强制静音某个粒子；外部音源 release 与复音抢占也不受控制。
- 宿主停止回调或硬旁路时无法保证发出最终 Off；`prepareToPlay` 后为新引擎状态，宿主需妥善结束旧音符。

## 重跑

```sh
ctest --test-dir build/glint -C Release -R '^glint\.' --output-on-failure
```

VST3 回录程序可额外接收旧版插件路径，验证真实旧状态迁移：

```sh
ozoGlintHostTests /path/to/ozo\ GLINT.vst3 /path/to/ozo\ MIDI\ Delay.vst3
```

当前记录只适用于本次源码和 GLINT 产物；后续修改核心、封装或参数时需重新运行。
