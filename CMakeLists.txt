cmake_minimum_required(VERSION 3.22)
project(Vectron VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(VECTRON_BUILD_TESTS "Build Vectron unit tests" ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.13
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Vectron
    PRODUCT_NAME              "Vectron"
    COMPANY_NAME              "Vectron Audio"
    BUNDLE_ID                 com.vectronaudio.Vectron
    PLUGIN_MANUFACTURER_CODE  Vctn
    PLUGIN_CODE               Vec1
    FORMATS                   VST3 Standalone
    IS_SYNTH                  TRUE
    NEEDS_MIDI_INPUT          TRUE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   TRUE)

juce_generate_juce_header(Vectron)

target_sources(Vectron PRIVATE
    source/PluginProcessor.cpp
    source/params/ParameterLayout.cpp
    source/dsp/VectronVoice.cpp
    source/dsp/osc/VectorEngine.cpp
    source/dsp/noise/NoiseGenerator.cpp)

target_include_directories(Vectron PRIVATE source)

target_compile_definitions(Vectron PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Vectron
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(VECTRON_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
