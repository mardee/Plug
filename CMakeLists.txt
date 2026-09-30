cmake_minimum_required(VERSION 3.22)
project(ozoPRISM VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(JUCE EXCLUDE_FROM_ALL)

juce_add_plugin(ozoPRISM
    COMPANY_NAME "ozo"
    PLUGIN_MANUFACTURER_CODE OzoA
    PLUGIN_CODE Prsm
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "ozo PRISM"
    BUNDLE_ID "cn.ozo.prism"
)

juce_generate_juce_header(ozoPRISM)

# 涂鸦字体（SIL OFL 1.1，见 assets/fonts/OFL.txt）
juce_add_binary_data(ozoPRISMFonts
    SOURCES
        assets/fonts/SedgwickAveDisplay-Regular.ttf
        assets/fonts/OFL.txt
)

target_sources(ozoPRISM PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/dsp/SignalChain.cpp
    src/presets/PresetFactory.cpp
    src/presets/UserPresets.cpp
)

target_include_directories(ozoPRISM PRIVATE src)

target_compile_definitions(ozoPRISM PRIVATE
    JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(ozoPRISM PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_basics
    ozoPRISMFonts
)

# -----------------------------------------------------------------------------
# 离线 DSP 验证：直接跑 SignalChain，不依赖插件宿主
# -----------------------------------------------------------------------------
juce_add_console_app(ozoPRISMTests
    PRODUCT_NAME "ozoPRISMTests"
)

target_sources(ozoPRISMTests PRIVATE
    tests/dsp_tests.cpp
    src/dsp/SignalChain.cpp
)

target_include_directories(ozoPRISMTests PRIVATE src)

target_link_libraries(ozoPRISMTests PRIVATE
    juce::juce_audio_basics
    juce::juce_core
    juce::juce_dsp
)

# -----------------------------------------------------------------------------
# VST3 宿主诊断：加载已安装的 .vst3，跑一段音频，确认插件真的在处理
# -----------------------------------------------------------------------------
juce_add_console_app(ozoPRISMVST3Host
    PRODUCT_NAME "ozoPRISMVST3Host"
)

target_sources(ozoPRISMVST3Host PRIVATE
    tests/vst3_host_test.cpp
)

target_link_libraries(ozoPRISMVST3Host PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_core
    juce::juce_audio_processors_headless
)

target_compile_definitions(ozoPRISMVST3Host PRIVATE
    JUCE_PLUGINHOST_VST3=1
)

# -----------------------------------------------------------------------------
# 离线 UI 渲染截图工具
# -----------------------------------------------------------------------------
juce_add_console_app(ozoPRISMSnapshot
    PRODUCT_NAME "ozoPRISMSnapshot"
)

target_sources(ozoPRISMSnapshot PRIVATE
    tests/render_snapshots.cpp
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/dsp/SignalChain.cpp
    src/presets/PresetFactory.cpp
    src/presets/UserPresets.cpp
)

target_include_directories(ozoPRISMSnapshot PRIVATE src)

target_compile_definitions(ozoPRISMSnapshot PRIVATE
    JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_link_libraries(ozoPRISMSnapshot PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_basics
    ozoPRISMFonts
)
