cmake_minimum_required(VERSION 3.22)

project(CloPlayer VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# By default JUCE is fetched automatically. For an offline/local JUCE checkout:
# cmake -S . -B build -DCLOPLAYER_JUCE_PATH=/path/to/JUCE
set(CLOPLAYER_JUCE_PATH "" CACHE PATH "Optional path to a local JUCE checkout")

if(CLOPLAYER_JUCE_PATH)
    add_subdirectory("${CLOPLAYER_JUCE_PATH}" JUCE)
else()
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(CloPlayer
    COMPANY_NAME "CloPlayer"
    BUNDLE_ID "com.cloplayer.plugin"
    PLUGIN_MANUFACTURER_CODE CloP
    PLUGIN_CODE ClP1
    FORMATS VST3
    PRODUCT_NAME "CloPlayer"
    DESCRIPTION "GP-200 CLO/SnapTone model player"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

juce_generate_juce_header(CloPlayer)

target_sources(CloPlayer PRIVATE
    source/PluginProcessor.cpp
    source/PluginProcessor.h
    source/PluginEditor.cpp
    source/PluginEditor.h
    source/GP200CloPlayer.cpp
    source/GP200CloPlayer.h
)

target_compile_definitions(CloPlayer PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(CloPlayer PRIVATE
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_gui_extra
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

if(MSVC)
    target_compile_options(CloPlayer PRIVATE /utf-8 /permissive- /Zc:__cplusplus)
endif()
