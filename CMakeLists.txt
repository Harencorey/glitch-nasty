cmake_minimum_required(VERSION 3.22)

# Build number from CI (0.1.<build>), same scheme as Glow.
set(GN_BUILD_NUMBER "0" CACHE STRING "Build number for the version string")
project(GlitchNasty VERSION 0.1.${GN_BUILD_NUMBER} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(GN_BUILD_TESTS "Build the offline engine tests" ON)

# ---- JUCE ---------------------------------------------------------------------
# build_mac.sh passes -DJUCE_DIR=<path to a JUCE 8.x checkout>.
if(NOT DEFINED JUCE_DIR)
    message(FATAL_ERROR "Set JUCE_DIR to a JUCE 8 checkout (build_mac.sh does this for you).")
endif()
add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)

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
