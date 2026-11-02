cmake_minimum_required(VERSION 3.22)

project(Afterglow VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
endif()

# ---------------------------------------------------------------------------
# JUCE: use a local copy if you have one (-DAFTERGLOW_JUCE_PATH=...),
# otherwise download JUCE 8 automatically.
# ---------------------------------------------------------------------------
set(AFTERGLOW_JUCE_PATH "" CACHE PATH "Path to a local JUCE checkout (optional)")
option(AFTERGLOW_BUILD_PLUGIN "Build the plugin (needs JUCE)" ON)
option(AFTERGLOW_BUILD_TESTS "Build the offline render test" ON)

if(AFTERGLOW_BUILD_PLUGIN)
    if(AFTERGLOW_JUCE_PATH)
        add_subdirectory(${AFTERGLOW_JUCE_PATH} JUCE)
    else()
        include(FetchContent)
        FetchContent_Declare(
            JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.4
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()

    set(AFTERGLOW_FORMATS VST3 Standalone)
    if(APPLE)
        list(APPEND AFTERGLOW_FORMATS AU)
    endif()

    juce_add_plugin(Afterglow
        COMPANY_NAME "Kevin Kaye"
        COMPANY_WEBSITE ""
        BUNDLE_ID "uk.kevinkaye.afterglow"
        PLUGIN_MANUFACTURER_CODE Kkay
        PLUGIN_CODE Aglw
        FORMATS ${AFTERGLOW_FORMATS}
        PRODUCT_NAME "Afterglow"
        DESCRIPTION "Piano-style spectral sustain and sostenuto, after the PLUS Pedal II"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Fx Reverb)

    juce_add_binary_data(AfterglowData SOURCES Resources/RobotoCondensed-Bold.ttf)

    target_sources(Afterglow PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)

    target_include_directories(Afterglow PRIVATE Source Source/engine)

    target_compile_definitions(Afterglow PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(Afterglow
        PRIVATE
            AfterglowData
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()

# ---------------------------------------------------------------------------
# Offline engine test (no JUCE needed): renders test audio through every mode
# ---------------------------------------------------------------------------
if(AFTERGLOW_BUILD_TESTS)
    add_executable(afterglow_render_test Tests/RenderTest.cpp)
    target_include_directories(afterglow_render_test PRIVATE Source/engine)
endif()
