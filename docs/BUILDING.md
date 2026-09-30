# ozo PRISM 构建与打包

## Mac 开发，Windows 出包

使用仓库的 **Actions → Build PRISM → Run workflow**，选择代码所在分支：

- `platform = windows`、`package = vst3`：最快路径，仅生成 Windows x64 VST3。
- `platform = windows`、`package = full`：VST3 + Standalone，并运行音频回归和 VST3 加载测试。
- `platform = macos`：生成 arm64 + x86_64 的 macOS Universal 包。
- `platform = both`、`package = full`：完整双平台验证和打包。

手动触发需要此工作流先存在于仓库默认分支。若尚未合并，推送到 `feat/**` 分支也会自动构建 Windows VST3，无需手动按钮。

### 自动构建规则

| 触发方式 | 构建内容 |
| --- | --- |
| `feat/**` 的相关源码、资源、构建文件变更 | Windows x64 VST3 快速包 |
| `main` / `master` 的相关变更 | Windows + macOS 完整包及音频测试 |
| 推送 `v*` 标签 | 双平台完整包，全部成功后上传 GitHub Release |
| Run workflow | 使用选择的平台和包类型，不自动发布 Release |

文档、记忆文件或 LATTICE 的单独变更不会重编 PRISM。相同分支、触发方式和手动选项的新任务会取消旧任务；发布标签不会被自动取消。两个平台并行构建，某个平台的 artifact 上传完成即可下载，不必等另一个平台。

在运行页面的 **Artifacts** 下载，例如：

- `ozoPRISM-Windows-x64-vst3`
- `ozoPRISM-Windows-x64-full`
- `ozoPRISM-macOS-Universal-full`

GitHub 下载的外层 artifact 内含安装 ZIP 和 `.zip.sha256` 校验文件。解开安装 ZIP 后，Windows 必须复制整个 `ozo PRISM.vst3` 文件夹到 `C:\Program Files\Common Files\VST3\`，不能只拿其中的 DLL/二进制。每个包附带 `INSTALL.txt` 和包含源码 commit 的 `BUILD-INFO.txt`，方便确认用的是哪一版。

## 加速与失败检查

- Windows 固定使用 MSVC x64、Ninja、Release 和并行编译。
- `sccache` 缓存 Windows C/C++ 编译结果，使用 GitHub 缓存后端；JUCE 的配置期辅助工具也继承编译启动器。首次构建仍是冷缓存，后续收益取决于缓存命中及工具链是否变化。
- 缓存统计写入每次运行摘要。JUCE 源码另有缓存；不缓存或跨平台复用整个 `build` 目录。
- 快速包只构建 `ozoPRISM_VST3`；不编译 DSP 测试、宿主诊断、Standalone 或截图工具。完整包启用测试，但仍不编译截图工具。
- macOS Universal 暂不使用编译缓存，避免多架构编译的缓存兼容性问题。
- 打包只读取明确的产物路径，检查真实二进制和架构，文件缺失立即失败，不会上传只有说明书的空包。
- macOS 打包在临时副本上执行 ad-hoc 签名及校验，保留 bundle 结构；这不是 Developer ID 签名或公证，Gatekeeper 仍可能提示。
- ZIP 上传关闭二次压缩；产物保留 14 天，正式标签发布的文件另存于 Release。

不能把 Mac 编译出的 VST3 改扩展名当作 Windows 版本；Windows 包必须由 Windows 工具链生成。实际构建耗时和热缓存收益应以 Actions 的步骤耗时、缓存统计为准。

## 本地构建

需要 CMake 3.22+、C++17 工具链、JUCE 9.0.2，Windows 另需 Visual Studio 2022 C++ 工作负载、Windows SDK 和 Ninja。

若根目录还没有 JUCE：

```bash
git clone --branch 9.0.2 --depth 1 https://github.com/juce-framework/JUCE.git JUCE
```

### Windows 快速包

在 x64 Native Tools Command Prompt / 配好 MSVC 的终端运行：

```powershell
cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DPRISM_BUILD_SNAPSHOT=OFF
cmake --build build-win --config Release --target ozoPRISM_VST3 --parallel
python scripts/package_plugins.py --platform windows --package vst3 --build-dir build-win --output-dir dist
```

不要只构建 `ozoPRISM`：它是 JUCE 共享代码静态库，不是可安装插件。

### macOS 完整包

使用独立构建目录，避免和本机单架构开发产物混用：

```bash
cmake -S . -B build-universal -DCMAKE_BUILD_TYPE=Release \
  '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64' -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  -DBUILD_TESTING=ON -DPRISM_BUILD_SNAPSHOT=OFF
cmake --build build-universal --config Release \
  --target ozoPRISM_VST3 ozoPRISM_AU ozoPRISM_Standalone ozoPRISMTests ozoPRISMVST3Host --parallel
ctest --test-dir build-universal -C Release --output-on-failure --no-tests=error
python3 scripts/package_plugins.py --platform macos --package full --build-dir build-universal --output-dir dist
```

`BUILD_TESTING` 和 `PRISM_BUILD_SNAPSHOT` 默认保持开启，方便本地开发；CI 会显式选择。配置了测试不代表 `cmake --build --target ozoPRISM_VST3` 会自动构建测试；运行 CTest 前需构建上述两个测试目标及 VST3。

### 构建脚本自测

Python 3.9+，无第三方依赖：

```bash
python3 -m unittest discover -s tests -p 'test_*.py' -v
```

这些测试验证构建目标选择和打包失败检查，不代替 Windows 编译或 DAW 实机试听。
