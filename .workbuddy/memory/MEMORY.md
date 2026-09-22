# ozo EZamp 项目约定

## 构建（必须遵守）

- **插件本体目标是 `ozoEZamp_VST3` / `ozoEZamp_AU` / `ozoEZamp_Standalone`**，
  不是 `ozoEZamp`（那只是 SharedCode 静态库）。只构建 `ozoEZamp` 会造成
  "编译成功"假象，装机版还是旧代码（2026-09-22 踩过，WILD 开关消失即此因）。
- cmake 路径：`/opt/homebrew/bin/cmake`（不在默认 PATH）。
- 安装前验证新代码真的进了二进制：`LC_ALL=C grep -c "特征字符串" <Mach-O>`。
  时间戳不可信——codesign 会刷新它。
- 签名：`codesign --force --deep -s -`；安装用 `ditto` 合并覆盖，不删不挪用户文件。
- 回归测试：`--target ozoEZampTests`，跑 `ozoEZampTests_artefacts/Release/ozoEZampTests`。

## 项目事实

- 插件：ozo EZamp，JUCE C++，染色单块（Drive/Air/Weight/Glue + 频谱可视化）。
- 狂野模式（WILD）：FoldStage 波形折叠 + 次八度 f/2 + 各级加倍 + 深色换相主题。
- 测试 23 项（含 testExciterPurity、testWildMode）。
- 交付路径：`~/Library/Audio/Plug-Ins/VST3|Components/`。
