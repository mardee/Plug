cmake_minimum_required(VERSION 3.22)
project(ozoLATTICE VERSION 0.3.1 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_subdirectory(../JUCE ${CMAKE_BINARY_DIR}/juce EXCLUDE_FROM_ALL)
juce_add_plugin(ozoLATTICE COMPANY_NAME ozo PLUGIN_MANUFACTURER_CODE OzoA PLUGIN_CODE Ltce IS_SYNTH TRUE NEEDS_MIDI_INPUT TRUE NEEDS_MIDI_OUTPUT FALSE IS_MIDI_EFFECT FALSE EDITOR_WANTS_KEYBOARD_FOCUS TRUE FORMATS VST3 Standalone PRODUCT_NAME "ozo LATTICE" BUNDLE_ID cn.ozo.lattice COPY_PLUGIN_AFTER_BUILD FALSE)
juce_add_binary_data(LatticeSamples HEADER_NAME LatticeSamples.h NAMESPACE LatticeSamples SOURCES
    src/samples/kick.wav src/samples/snare.wav src/samples/tom2.wav
    src/samples/hat.wav src/samples/open.wav src/samples/tom1.wav
    src/samples/ride.wav src/samples/crash.wav)
target_sources(ozoLATTICE PRIVATE src/Processor.cpp src/Editor.cpp)
target_include_directories(ozoLATTICE PRIVATE src)
target_compile_definitions(ozoLATTICE PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1)
target_link_libraries(ozoLATTICE PRIVATE juce::juce_audio_utils LatticeSamples)
juce_add_console_app(LatticeTests PRODUCT_NAME LatticeTests)
target_sources(LatticeTests PRIVATE tests/Tests.cpp src/Processor.cpp src/Editor.cpp)
target_include_directories(LatticeTests PRIVATE src)
target_compile_definitions(LatticeTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_PLUGINHOST_VST3=1)
target_link_libraries(LatticeTests PRIVATE juce::juce_audio_utils LatticeSamples)
