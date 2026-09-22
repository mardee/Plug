cmake_minimum_required(VERSION 3.22)
project(ozoEZamp VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(JUCE EXCLUDE_FROM_ALL)

juce_add_plugin(ozoEZamp
    COMPANY_NAME "ozo"
    PLUGIN_MANUFACTURER_CODE OzoA
    PLUGIN_CODE EZmp
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "ozo EZamp"
    BUNDLE_ID "cn.ozo.ezamp"
)

juce_generate_juce_header(ozoEZamp)

# 涂鸦字体（SIL OFL 1.1，见 assets/fonts/OFL.txt）。
# 嵌进二进制而不是依赖系统字体 —— 用户机器上没装这个字体时不能退化成宋体。
juce_add_binary_data(ozoEZampFonts
    SOURCES
        assets/fonts/SedgwickAveDisplay-Regular.ttf
        assets/fonts/OFL.txt
)

target_sources(ozoEZamp PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/dsp/SignalChain.cpp
    src/presets/PresetFactory.cpp
)

target_include_directories(ozoEZamp PRIVATE src)

target_compile_definitions(ozoEZamp PRIVATE
    JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(ozoEZamp PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_basics
    ozoEZampFonts
)

# -----------------------------------------------------------------------------
# 离线 DSP 验证：直接跑 SignalChain，不依赖插件宿主
# -----------------------------------------------------------------------------
juce_add_console_app(ozoEZampTests
    PRODUCT_NAME "ozoEZampTests"
)

target_sources(ozoEZampTests PRIVATE
    tests/dsp_tests.cpp
    src/dsp/SignalChain.cpp
)

target_include_directories(ozoEZampTests PRIVATE src)

target_link_libraries(ozoEZampTests PRIVATE
    juce::juce_audio_basics
    juce::juce_core
    juce::juce_dsp
)

# -----------------------------------------------------------------------------
# VST3 宿主诊断：加载已安装的 .vst3，跑一段音频，确认插件真的在处理
# -----------------------------------------------------------------------------
juce_add_console_app(ozoEZampVST3Host
    PRODUCT_NAME "ozoEZampVST3Host"
)

target_sources(ozoEZampVST3Host PRIVATE
    tests/vst3_host_test.cpp
)

target_link_libraries(ozoEZampVST3Host PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_core
    juce::juce_audio_processors_headless
)

target_compile_definitions(ozoEZampVST3Host PRIVATE
    JUCE_PLUGINHOST_VST3=1
)
