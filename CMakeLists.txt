cmake_minimum_required(VERSION 3.22)
if(NOT DEFINED GN_VERSION)
    set(GN_VERSION 0.1.0)   # CI passes 0.1.<build number>
endif()
project(GlitchNasty VERSION ${GN_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)  # universal binary
endif()

option(GN_BUILD_TESTS "Build the offline engine tests" ON)

# JUCE: use a local checkout if you pass -DJUCE_DIR=/path/to/JUCE, otherwise download 8.0.15.
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.15 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

if(APPLE)
    set(GN_FORMATS AU VST3 Standalone)
    set(GN_COPY_AFTER_BUILD TRUE)   # installs into ~/Library/Audio/Plug-Ins/
else()
    set(GN_FORMATS VST3 Standalone)
    set(GN_COPY_AFTER_BUILD FALSE)
endif()

juce_add_plugin(GlitchNasty
    COMPANY_NAME "Norka DSP"
    BUNDLE_ID com.norkadsp.glitchnasty
    PLUGIN_MANUFACTURER_CODE Nrka
    PLUGIN_CODE Gnst              # keep this forever once it's been used in sessions
    FORMATS ${GN_FORMATS}
    PRODUCT_NAME "Glitch Nasty"
    IS_SYNTH FALSE
    # MIDI in is ON now (unused until MIDI triggering) because switching it later
    # changes the AU type from aufx to aumf and breaks existing Logic sessions.
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicEffect
    VST3_CATEGORIES Fx
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    # Standalone on macOS gets silence from the interface without this.
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Glitch Nasty processes the live audio from your audio interface."
    COPY_PLUGIN_AFTER_BUILD ${GN_COPY_AFTER_BUILD})

target_sources(GlitchNasty PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/StepGrid.cpp)

target_compile_definitions(GlitchNasty PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(GlitchNasty
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- Offline tests (plain C++, no JUCE) ---------------------------------------
if(GN_BUILD_TESTS)
    enable_testing()
    add_executable(GlitchNastyTests tests/test_engine.cpp)
    target_include_directories(GlitchNastyTests PRIVATE Source)
    add_test(NAME engine COMMAND GlitchNastyTests)
endif()
