# SSInger -- JUCE 9.0.3 build: VST3 + Standalone everywhere, AU on macOS,
# LV2 on Linux, CLAP everywhere (clap-juce-extensions). See BUILD.md.
cmake_minimum_required(VERSION 3.22)
project(SSInger VERSION 0.7.7 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

# Windows: the plug-ins carry their own C runtime, so a DAW loads them on a
# machine without the Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(SSINGER_BUILD_TESTS "Build offline C tests (no JUCE needed)" ON)
option(SSINGER_HAVE_M6502 "Enable real 6502 stepping (needs third_party/chips)" OFF)
option(SSINGER_CLAP "Build the CLAP plug-in too (fetches clap-juce-extensions, MIT)" ON)
option(SSINGER_COPY_AFTER_BUILD "Copy each plug-in to the user's plug-in folder after building" OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(_ssinger_lv2_default ON)
else()
  set(_ssinger_lv2_default OFF)
endif()
option(SSINGER_LV2 "Build the LV2 plug-in (default: on for Linux)" ${_ssinger_lv2_default})

# --- JUCE 9.0.3, fetched once at configure time ---------------------------
include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 9.0.3
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# --- clap-juce-extensions (MIT), pinned; brings CLAP + clap-helpers (MIT) --
if(SSINGER_CLAP)
  FetchContent_Declare(clap_juce_extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG 55525c9858d4b25687be7759a5e0f70eccef218e  # main, 2026-09-12, JUCE 9 support
    GIT_SUBMODULES_RECURSE TRUE)
  FetchContent_MakeAvailable(clap_juce_extensions)
endif()

# --- floooh chips (m6502.h) -------------------------------------------------
# Clone once: git clone --depth 1 https://github.com/floooh/chips.git third_party/chips
if(SSINGER_HAVE_M6502)
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/chips/chips/m6502.h")
    message(FATAL_ERROR "SSINGER_HAVE_M6502=ON but third_party/chips/chips/m6502.h missing. See third_party/README.md.")
  endif()
endif()

# --- The SSI-263 engine (this repo's C core, reused as-is) ------------------
set(SSI263_CSRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src/csrc" CACHE PATH "ssi263 C core")
add_library(ssi263 STATIC "${SSI263_CSRC_DIR}/ssi263.c" "${SSI263_CSRC_DIR}/ssi263dsp.c")
target_include_directories(ssi263 PUBLIC "${SSI263_CSRC_DIR}")
target_compile_options(ssi263 PRIVATE $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-ffp-contract=off>)
# Linked into a plug-in (a shared object on Linux): position-independent.
set_target_properties(ssi263 PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(UNIX)
  target_link_libraries(ssi263 PUBLIC m)
endif()

# --- The plugin --------------------------------------------------------------
set(SSINGER_FORMATS VST3 Standalone)
if(APPLE)
  list(APPEND SSINGER_FORMATS AU)
endif()
if(SSINGER_LV2)
  list(APPEND SSINGER_FORMATS LV2)
endif()

juce_add_plugin(SSInger
  COMPANY_NAME "ssi263-speech"
  BUNDLE_ID "org.ssi263-speech.SSInger"
  PLUGIN_MANUFACTURER_CODE S263
  PLUGIN_CODE Ssng
  FORMATS ${SSINGER_FORMATS}
  IS_SYNTH TRUE
  PRODUCT_NAME "SSInger"
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  # TRUE: on macOS a FALSE here makes the editor window ignore key presses
  # (no Tab between controls). Windows VST3 is unaffected either way.
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD ${SSINGER_COPY_AFTER_BUILD}
  VST3_CATEGORIES "Instrument" "Synth" "Vocal"
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  LV2URI "urn:ssi263-speech:ssinger"
  DESCRIPTION "SSInger: SC-02 singing voice after the Robovox patent (EP0396141A2)")

target_sources(SSInger PRIVATE
  plugin/SSIngerProcessor.cpp
  plugin/SSIngerEditor.cpp)

target_include_directories(SSInger PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${SSI263_CSRC_DIR}")

if(SSINGER_HAVE_M6502)
  target_include_directories(SSInger PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/chips")
  target_compile_definitions(SSInger PRIVATE SSINGER_HAVE_M6502=1)
endif()

target_compile_definitions(SSInger PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(SSInger PRIVATE
  ssi263
  juce::juce_audio_utils
  juce::juce_audio_processors)

if(SSINGER_CLAP)
  clap_juce_extensions_plugin(TARGET SSInger
    CLAP_ID "org.ssi263-speech.ssinger"
    CLAP_FEATURES instrument synthesizer mono
    CLAP_USE_JUCE_PARAMETER_RANGES DISCRETE)
endif()

# --- Offline tests (plain C, no JUCE) -----------------------------------------
if(SSINGER_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
