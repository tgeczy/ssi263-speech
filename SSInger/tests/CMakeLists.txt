# Offline C tests: no JUCE, no network. Usable standalone:
#   gcc -std=c99 -I <SSInger> -I <csrc> test_6850_midi.c <csrc>/ssi263.c -lm
cmake_minimum_required(VERSION 3.22)
project(SSIngerTests LANGUAGES C CXX)
enable_testing()

if(NOT DEFINED SSI263_CSRC_DIR)
  set(SSI263_CSRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../src/csrc")
endif()

add_executable(test_6850_midi test_6850_midi.c
  "${SSI263_CSRC_DIR}/ssi263.c" "${SSI263_CSRC_DIR}/ssi263dsp.c")
target_include_directories(test_6850_midi PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/.."
  "${SSI263_CSRC_DIR}")
target_compile_options(test_6850_midi PRIVATE
  $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-std=c99 -Wall -Wextra -ffp-contract=off>)
if(UNIX)
  target_link_libraries(test_6850_midi PRIVATE m)
endif()

# Runs in the SSInger folder: one test reads note_map.txt there.
add_test(NAME acia6850_translator_bus_engine COMMAND test_6850_midi
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/..")

# The must-fail controls: each puts one bug back and passes only when its own check fails.
#   #82 a held phoneme let go at a routing change and at All Notes Off
#   #83 CC3 and the Patent mod wheel kept past the next block
#   #83 again: Glide moved alone keeps CC3's rate (one change test for both put the rate knob back)
add_test(NAME control_routing_release COMMAND test_6850_midi
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/..")
set_tests_properties(control_routing_release PROPERTIES ENVIRONMENT "SSINGER_ROUTING_BREAK=1"
  PASS_REGULAR_EXPRESSION "FAIL [^\n]*a routing change releases the held phoneme")
add_test(NAME control_params_keep_controllers COMMAND test_6850_midi
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/..")
set_tests_properties(control_params_keep_controllers PROPERTIES ENVIRONMENT "SSINGER_PARAMS_BREAK=1"
  PASS_REGULAR_EXPRESSION "FAIL [^\n]*the next block keeps the controllers")
add_test(NAME control_params_glide_alone COMMAND test_6850_midi
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/..")
set_tests_properties(control_params_glide_alone PROPERTIES ENVIRONMENT "SSINGER_PARAMS_BREAK=glide"
  PASS_REGULAR_EXPRESSION "FAIL [^\n]*Glide moved alone keeps the controller's rate")

# The emulator headers ship to C++ (JUCE plugin). Compile-only check.
add_library(ssinger_cxxcheck STATIC cxxcheck.cpp)
target_include_directories(ssinger_cxxcheck PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/.."
  "${SSI263_CSRC_DIR}")
target_compile_features(ssinger_cxxcheck PRIVATE cxx_std_20)
