cmake_minimum_required(VERSION 3.22)
# These two must be set BEFORE project(), or CMake ignores them (the first CI build was arm64-only because of this).
if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal: Apple silicon + Intel")
endif()

project(WT8 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(WT8_ENGINE_ONLY "Build only the engine + headless test (no JUCE, no plugin)" OFF)

# ---------------------------------------------------------------------------
# DaisySP (MIT, (c) Electrosmith) -- DSP library used by the CHOMPI WAVE engine
# ---------------------------------------------------------------------------
set(DSP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/third_party/DaisySP/Source)
file(GLOB DAISYSP_SRC
  ${DSP_ROOT}/Control/*.cpp ${DSP_ROOT}/Dynamics/*.cpp ${DSP_ROOT}/Effects/*.cpp
  ${DSP_ROOT}/Filters/*.cpp ${DSP_ROOT}/Synthesis/*.cpp ${DSP_ROOT}/Utility/*.cpp)
add_library(daisysp STATIC ${DAISYSP_SRC})
target_include_directories(daisysp PUBLIC ${DSP_ROOT} ${DSP_ROOT}/Control ${DSP_ROOT}/Dynamics
  ${DSP_ROOT}/Effects ${DSP_ROOT}/Filters ${DSP_ROOT}/Synthesis ${DSP_ROOT}/Utility)
target_compile_options(daisysp PRIVATE -w)
set_target_properties(daisysp PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# WT8 engine: CHOMPI WAVE headers (one small sample-rate change to the delay) + a few shims + our small wrapper
# ---------------------------------------------------------------------------
add_library(wt8_engine STATIC Source/WT8Engine.cpp Source/StepSequencer.cpp Source/WavetableImport.cpp)
# shim must come first so our stubs replace the hardware-bound headers
target_include_directories(wt8_engine PRIVATE Source/shim Source/engine)
target_include_directories(wt8_engine PUBLIC Source)
target_link_libraries(wt8_engine PUBLIC daisysp)
target_compile_options(wt8_engine PRIVATE -w)
set_target_properties(wt8_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(render_test tests/render_test.cpp)
target_link_libraries(render_test PRIVATE wt8_engine)

add_executable(rate_test tests/rate_test.cpp)
target_link_libraries(rate_test PRIVATE wt8_engine)

add_executable(seq_test tests/seq_test.cpp)
target_link_libraries(seq_test PRIVATE wt8_engine)

if(WT8_ENGINE_ONLY)
  return()
endif()

# ---------------------------------------------------------------------------
# JUCE plugin (AU + VST3 + Standalone)
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.8 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_binary_data(WT8Data SOURCES
  wavetables/wavetable01.wav wavetables/wavetable02.wav wavetables/wavetable03.wav
  wavetables/wavetable04.wav wavetables/wavetable05.wav wavetables/wavetable06.wav
  wavetables/wavetable07.wav)

if(APPLE)
  set(WT8_FORMATS AU VST3 Standalone)
  set(WT8_COPY TRUE)
else()
  set(WT8_FORMATS VST3 Standalone)
  set(WT8_COPY FALSE)
endif()

juce_add_plugin(WT8
  COMPANY_NAME "particledots"
  PLUGIN_MANUFACTURER_CODE Ptdt
  PLUGIN_CODE Ipmo
  FORMATS ${WT8_FORMATS}
  PRODUCT_NAME "ipmohc"
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  COPY_PLUGIN_AFTER_BUILD ${WT8_COPY})

target_sources(WT8 PRIVATE Source/PluginProcessor.cpp Source/PluginProcessor.h Source/PluginEditor.cpp Source/PluginEditor.h)
target_compile_definitions(WT8 PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(WT8 PRIVATE WT8Data wt8_engine
  juce::juce_audio_utils juce::juce_audio_processors
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# v0.22 draft: "dotriaconta-tone", the sequencer as a MIDI effect (AU type aumi). Same sources, compiled with WT8_MIDI_FX=1:
# no sound engine, no audio buses, the sequencer's notes leave as MIDI.
# ---------------------------------------------------------------------------
if(APPLE)
  set(WT8SEQ_FORMATS AU VST3)
else()
  set(WT8SEQ_FORMATS VST3)
endif()
juce_add_plugin(WT8Seq
  COMPANY_NAME "particledots"
  PLUGIN_MANUFACTURER_CODE Ptdt
  PLUGIN_CODE Ipsq
  FORMATS ${WT8SEQ_FORMATS}
  PRODUCT_NAME "dotriaconta-tone"
  IS_SYNTH FALSE
  IS_MIDI_EFFECT TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT TRUE
  AU_MAIN_TYPE kAudioUnitType_MIDIProcessor
  COPY_PLUGIN_AFTER_BUILD ${WT8_COPY})
target_sources(WT8Seq PRIVATE Source/PluginProcessor.cpp Source/PluginProcessor.h Source/PluginEditor.cpp Source/PluginEditor.h)
target_compile_definitions(WT8Seq PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 WT8_MIDI_FX=1)
target_link_libraries(WT8Seq PRIVATE WT8Data wt8_engine
  juce::juce_audio_utils juce::juce_audio_processors
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Headless test of the MIDI effect build (drives the real processor: MIDI in, MIDI out)
add_executable(fx_test tests/fx_test.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_include_directories(fx_test PRIVATE Source)
target_compile_definitions(fx_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 WT8_MIDI_FX=1)
target_link_libraries(fx_test PRIVATE WT8Data wt8_engine juce::juce_audio_processors juce::juce_recommended_config_flags)

# Headless test that drives the actual plugin object (processBlock, MIDI, state)
add_executable(plugin_test tests/plugin_test.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_include_directories(plugin_test PRIVATE Source)
target_compile_definitions(plugin_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(plugin_test PRIVATE WT8Data wt8_engine juce::juce_audio_processors juce::juce_recommended_config_flags)

# Optional: render the editor offscreen to a PNG (used on Linux to check layout without a Mac)
option(WT8_EDITOR_SNAPSHOT "Build editor_snapshot (renders the editor to a PNG)" OFF)
if(WT8_EDITOR_SNAPSHOT)
  add_executable(editor_snapshot tests/editor_snapshot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
  target_include_directories(editor_snapshot PRIVATE Source)
  target_compile_definitions(editor_snapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
  target_link_libraries(editor_snapshot PRIVATE WT8Data wt8_engine juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
  add_executable(editor_snapshot_fx tests/editor_snapshot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
  target_include_directories(editor_snapshot_fx PRIVATE Source)
  target_compile_definitions(editor_snapshot_fx PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 WT8_MIDI_FX=1)
  target_link_libraries(editor_snapshot_fx PRIVATE WT8Data wt8_engine juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
endif()
