add_library(ozoGlintCore STATIC
    core/ParticleEngine.cpp
)
target_include_directories(ozoGlintCore PUBLIC core)
target_compile_features(ozoGlintCore PUBLIC cxx_std_17)
set_target_properties(ozoGlintCore PROPERTIES POSITION_INDEPENDENT_CODE ON)

juce_add_plugin(ozoGLINT
    VERSION 0.1.0
    COMPANY_NAME "ozo"
    # Keep the pre-GLINT identities so saved host projects can resolve the same plugin.
    PLUGIN_MANUFACTURER_CODE OzoA
    PLUGIN_CODE Omid
    BUNDLE_ID "cn.ozo.mididelay"
    PRODUCT_NAME "ozo GLINT"
    IS_SYNTH FALSE
    IS_MIDI_EFFECT TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    AU_MAIN_TYPE kAudioUnitType_MIDIProcessor
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    FORMATS AU VST3
)
juce_generate_juce_header(ozoGLINT)
target_sources(ozoGLINT PRIVATE
    plugin/PluginProcessor.cpp
    plugin/PluginEditor.cpp
)
target_include_directories(ozoGLINT PUBLIC plugin)
target_compile_definitions(ozoGLINT PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
)
target_link_libraries(ozoGLINT PRIVATE
    ozoGlintCore
    juce::juce_audio_processors
    juce::juce_gui_basics
    PUBLIC juce::juce_recommended_config_flags
)

if(APPLE)
    # VST3 manifest generation modifies the bundle after JUCE's initial signing.
    foreach(format IN ITEMS AU VST3)
        add_custom_command(TARGET ozoGLINT_${format} POST_BUILD
            COMMAND /usr/bin/codesign --force --sign - --timestamp=none
                "$<TARGET_BUNDLE_DIR:ozoGLINT_${format}>"
            VERBATIM
        )
    endforeach()
endif()

if(BUILD_TESTING)
    add_executable(ozoGlintCoreTests tests/core_tests.cpp)
    target_link_libraries(ozoGlintCoreTests PRIVATE ozoGlintCore)
    add_test(NAME glint.core COMMAND ozoGlintCoreTests)

    juce_add_console_app(ozoGlintHostTests PRODUCT_NAME "ozo GLINT Host Tests")
    target_sources(ozoGlintHostTests PRIVATE tests/host_tests.cpp)
    target_compile_definitions(ozoGlintHostTests PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )
    target_link_libraries(ozoGlintHostTests PRIVATE
        juce::juce_audio_processors_headless
    )
    add_dependencies(ozoGlintHostTests ozoGLINT_VST3)
    add_test(NAME glint.vst3-roundtrip COMMAND ozoGlintHostTests
        "$<TARGET_GENEX_EVAL:ozoGLINT_VST3,$<TARGET_PROPERTY:ozoGLINT_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>")

    add_executable(ozoGlintProcessorTests tests/processor_tests.cpp)
    target_link_libraries(ozoGlintProcessorTests PRIVATE
        ozoGLINT
        ozoGlintCore
        juce::juce_audio_processors
        juce::juce_gui_basics
    )
    add_test(NAME glint.processor COMMAND ozoGlintProcessorTests)
    set_tests_properties(glint.core glint.vst3-roundtrip glint.processor PROPERTIES TIMEOUT 120)

    if(APPLE)
        add_executable(ozoGlintAUHostTests tests/au_host_tests.mm)
        set_target_properties(ozoGlintAUHostTests PROPERTIES OBJCXX_STANDARD 17 OBJCXX_STANDARD_REQUIRED ON)
        target_link_libraries(ozoGlintAUHostTests PRIVATE
            "-framework AudioToolbox"
            "-framework AudioUnit"
            "-framework CoreFoundation"
            "-framework CoreMIDI"
        )
        add_dependencies(ozoGlintAUHostTests ozoGLINT_AU)
        add_test(NAME glint.au-roundtrip COMMAND ozoGlintAUHostTests
            "$<TARGET_GENEX_EVAL:ozoGLINT_AU,$<TARGET_PROPERTY:ozoGLINT_AU,JUCE_PLUGIN_ARTEFACT_FILE>>")
        set_tests_properties(glint.au-roundtrip PROPERTIES TIMEOUT 120)
    endif()
endif()
