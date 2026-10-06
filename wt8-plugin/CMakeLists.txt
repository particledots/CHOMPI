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
# WT8 engine: unmodified CHOMPI WAVE headers + a few shims + our small wrapper
# ---------------------------------------------------------------------------
add_library(wt8_engine STATIC Source/WT8Engine.cpp)
# shim must come first so our stubs replace the hardware-bound headers
target_include_directories(wt8_engine PRIVATE Source/shim Source/engine)
target_include_directories(wt8_engine PUBLIC Source)
target_link_libraries(wt8_engine PUBLIC daisysp)
target_compile_options(wt8_engine PRIVATE -w)
set_target_properties(wt8_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(render_test tests/render_test.cpp)
target_link_libraries(render_test PRIVATE wt8_engine)

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
  PLUGIN_CODE Wt8a
  FORMATS ${WT8_FORMATS}
  PRODUCT_NAME "WT8"
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  COPY_PLUGIN_AFTER_BUILD ${WT8_COPY})

target_sources(WT8 PRIVATE Source/PluginProcessor.cpp Source/PluginProcessor.h)
target_compile_definitions(WT8 PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(WT8 PRIVATE WT8Data wt8_engine
  juce::juce_audio_utils juce::juce_audio_processors
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Headless test that drives the actual plugin object (processBlock, MIDI, state)
add_executable(plugin_test tests/plugin_test.cpp Source/PluginProcessor.cpp)
target_include_directories(plugin_test PRIVATE Source)
target_compile_definitions(plugin_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(plugin_test PRIVATE WT8Data wt8_engine juce::juce_audio_processors juce::juce_recommended_config_flags)
