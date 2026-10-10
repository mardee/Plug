add_library(ozoMidiCore STATIC
    core/ParticleEngine.cpp
)
target_include_directories(ozoMidiCore PUBLIC core)
target_compile_features(ozoMidiCore PUBLIC cxx_std_17)
set_target_properties(ozoMidiCore PROPERTIES POSITION_INDEPENDENT_CODE ON)

juce_add_plugin(ozoMidiDelay
    VERSION 0.1.0
    COMPANY_NAME "ozo"
    PLUGIN_MANUFACTURER_CODE OzoA
    PLUGIN_CODE Omid
    BUNDLE_ID "cn.ozo.mididelay"
    PRODUCT_NAME "ozo MIDI Delay"
    IS_SYNTH FALSE
    IS_MIDI_EFFECT TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    AU_MAIN_TYPE kAudioUnitType_MIDIProcessor
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    FORMATS AU VST3
)
juce_generate_juce_header(ozoMidiDelay)
target_sources(ozoMidiDelay PRIVATE
    plugin/PluginProcessor.cpp
    plugin/PluginEditor.cpp
)
target_include_directories(ozoMidiDelay PUBLIC plugin)
target_compile_definitions(ozoMidiDelay PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
)
target_link_libraries(ozoMidiDelay PRIVATE
    ozoMidiCore
    juce::juce_audio_processors
    juce::juce_gui_basics
    PUBLIC juce::juce_recommended_config_flags
)

if(APPLE)
    # VST3 manifest generation modifies the bundle after JUCE's initial signing.
    foreach(format IN ITEMS AU VST3)
        add_custom_command(TARGET ozoMidiDelay_${format} POST_BUILD
            COMMAND /usr/bin/codesign --force --sign - --timestamp=none
                "$<TARGET_BUNDLE_DIR:ozoMidiDelay_${format}>"
            VERBATIM
        )
    endforeach()
endif()

if(BUILD_TESTING)
    add_executable(ozoMidiCoreTests tests/core_tests.cpp)
    target_link_libraries(ozoMidiCoreTests PRIVATE ozoMidiCore)
    add_test(NAME midi.core COMMAND ozoMidiCoreTests)

    juce_add_console_app(ozoMidiHostTests PRODUCT_NAME "ozo MIDI Host Tests")
    target_sources(ozoMidiHostTests PRIVATE tests/host_tests.cpp)
    target_compile_definitions(ozoMidiHostTests PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )
    target_link_libraries(ozoMidiHostTests PRIVATE
        juce::juce_audio_processors_headless
    )
    add_dependencies(ozoMidiHostTests ozoMidiDelay_VST3)
    add_test(NAME midi.vst3-roundtrip COMMAND ozoMidiHostTests
        "$<TARGET_GENEX_EVAL:ozoMidiDelay_VST3,$<TARGET_PROPERTY:ozoMidiDelay_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>")

    add_executable(ozoMidiProcessorTests tests/processor_tests.cpp)
    target_link_libraries(ozoMidiProcessorTests PRIVATE
        ozoMidiDelay
        ozoMidiCore
        juce::juce_audio_processors
        juce::juce_gui_basics
    )
    add_test(NAME midi.processor COMMAND ozoMidiProcessorTests)
    set_tests_properties(midi.core midi.vst3-roundtrip midi.processor PROPERTIES TIMEOUT 120)

    if(APPLE)
        add_executable(ozoMidiAUHostTests tests/au_host_tests.mm)
        set_target_properties(ozoMidiAUHostTests PROPERTIES OBJCXX_STANDARD 17 OBJCXX_STANDARD_REQUIRED ON)
        target_link_libraries(ozoMidiAUHostTests PRIVATE
            "-framework AudioToolbox"
            "-framework AudioUnit"
            "-framework CoreFoundation"
            "-framework CoreMIDI"
        )
        add_dependencies(ozoMidiAUHostTests ozoMidiDelay_AU)
        add_test(NAME midi.au-roundtrip COMMAND ozoMidiAUHostTests
            "$<TARGET_GENEX_EVAL:ozoMidiDelay_AU,$<TARGET_PROPERTY:ozoMidiDelay_AU,JUCE_PLUGIN_ARTEFACT_FILE>>")
        set_tests_properties(midi.au-roundtrip PROPERTIES TIMEOUT 120)
    endif()
endif()
