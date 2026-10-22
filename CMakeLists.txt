cmake_minimum_required(VERSION 3.22)
project(RoboDrummer VERSION 0.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ROBODRUMMER_BUILD_JUCE "Build VST3 and Standalone targets" ON)
option(ROBODRUMMER_BUILD_CLAP "Also build the CLAP target (requires ROBODRUMMER_BUILD_JUCE)" ON)
# Shown in Help/About next to the version (spec 17). Release CI passes the part of the version after the first '-' (e.g. beta.1).
set(ROBODRUMMER_PRERELEASE_TAG "" CACHE STRING "Pre-release tag appended to the version in About, e.g. beta.1 (empty for a final release)")
set(ROBODRUMMER_GIT_HASH "" CACHE STRING "Short git hash shown in About (detected from the checkout when empty)")
if(ROBODRUMMER_GIT_HASH STREQUAL "")
  find_package(Git QUIET)
  if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short=8 HEAD WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                    OUTPUT_VARIABLE ROBODRUMMER_GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE _rd_git_result)
    if(NOT _rd_git_result EQUAL 0)
      set(ROBODRUMMER_GIT_HASH "")
    endif()
  endif()
endif()

add_library(robodrummer_core INTERFACE)
target_include_directories(robodrummer_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# Bit-reproducible floating point: no fused multiply-add contraction, so identical inputs give identical
# generated bars/analysis regardless of compiler version, inlining or host CPU (GCC 15 diverged otherwise).
if(NOT MSVC)
  target_compile_options(robodrummer_core INTERFACE -ffp-contract=off)
else()
  target_compile_options(robodrummer_core INTERFACE /fp:precise)
endif()

enable_testing()
# Every tests/test_*.cpp is a core test (auto-discovered so features add tests without editing this file).
file(GLOB ROBODRUMMER_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.cpp)
foreach(test_src IN LISTS ROBODRUMMER_TEST_SOURCES)
  get_filename_component(test_target ${test_src} NAME_WE)
  string(REGEX REPLACE "^test_" "" test_name ${test_target})
  add_executable(${test_target} ${test_src})
  target_link_libraries(${test_target} PRIVATE robodrummer_core)
  if(MSVC)
    target_compile_options(${test_target} PRIVATE /UNDEBUG)
  else()
    target_compile_options(${test_target} PRIVATE -UNDEBUG)
  endif()
  add_test(NAME ${test_name} COMMAND ${test_target})
endforeach()

if(ROBODRUMMER_BUILD_JUCE)
  include(FetchContent)
  FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.3
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(JUCE)

  # AU is only available on macOS (it is an Apple format); everything else builds on every platform.
  set(ROBODRUMMER_FORMATS VST3 Standalone)
  if(APPLE)
    list(APPEND ROBODRUMMER_FORMATS AU)
  endif()

  juce_add_plugin(RoboDrummer
    COMPANY_NAME "Circuit Drift Labs"
    BUNDLE_ID "com.circuitdriftlabs.robodrummer"
    PLUGIN_MANUFACTURER_CODE CDrf
    PLUGIN_CODE RDrm
    FORMATS ${ROBODRUMMER_FORMATS}
    AU_MAIN_TYPE kAudioUnitType_MusicEffect
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "RoboDrummer listens to your guitar input to follow your playing."
    PLUGIN_DESCRIPTION "Adaptive virtual drummer for a live guitarist"

    VST3_CATEGORIES Instrument Drum
    PRODUCT_NAME "RoboDrummer"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
  )
  juce_generate_juce_header(RoboDrummer)

  # docs/HELP.md is the single source of the in-plug-in Help text (spec 17), so the repository document and the UI cannot drift.
  juce_add_binary_data(RoboDrummerHelpData SOURCES docs/HELP.md NAMESPACE rdhelp HEADER_NAME RoboDrummerHelpData.h)
  set_target_properties(RoboDrummerHelpData PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

  target_sources(RoboDrummer PRIVATE
    src/plugin/PluginProcessor.cpp
    src/plugin/PluginProcessor.h
    src/plugin/PluginEditor.cpp
    src/plugin/PluginEditor.h
    src/plugin/EditorPanels.h
    src/plugin/EditorPanelsExtra.h
    src/plugin/EditorWidgets.h
    src/plugin/PresetManager.cpp
    src/plugin/PresetManager.h
    src/audio/KitBuilder.cpp
    src/audio/KitBuilder.h
  )
  target_include_directories(RoboDrummer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(RoboDrummer PUBLIC RoboDrummerHelpData)
  target_link_libraries(RoboDrummer PRIVATE
    robodrummer_core
    juce::juce_audio_utils
    juce::juce_dsp
  )
  target_link_libraries(RoboDrummer PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
  )
  target_compile_definitions(RoboDrummer PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
  )
  # Build info for Help/About (spec 17) goes through a generated header so a new commit only recompiles its includers.
  file(WRITE ${CMAKE_BINARY_DIR}/generated/RoboDrummerBuildInfo.h.tmp
"#pragma once\n#define ROBODRUMMER_BUILD_INFO 1\n")
  if(NOT ROBODRUMMER_PRERELEASE_TAG STREQUAL "")
    file(APPEND ${CMAKE_BINARY_DIR}/generated/RoboDrummerBuildInfo.h.tmp "#define ROBODRUMMER_PRERELEASE_TAG \"${ROBODRUMMER_PRERELEASE_TAG}\"\n")
  endif()
  if(NOT ROBODRUMMER_GIT_HASH STREQUAL "")
    file(APPEND ${CMAKE_BINARY_DIR}/generated/RoboDrummerBuildInfo.h.tmp "#define ROBODRUMMER_GIT_HASH \"${ROBODRUMMER_GIT_HASH}\"\n")
  endif()
  configure_file(${CMAKE_BINARY_DIR}/generated/RoboDrummerBuildInfo.h.tmp ${CMAKE_BINARY_DIR}/generated/RoboDrummerBuildInfo.h COPYONLY)
  target_include_directories(RoboDrummer PUBLIC ${CMAKE_BINARY_DIR}/generated)

  # Headless end-to-end checks against the real AudioProcessor (no host or audio device needed).
  juce_add_console_app(RoboDrummerHarness PRODUCT_NAME "RoboDrummerHarness")
  juce_generate_juce_header(RoboDrummerHarness)
  target_sources(RoboDrummerHarness PRIVATE tests/harness/PluginHarness.cpp)
  target_include_directories(RoboDrummerHarness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_compile_definitions(RoboDrummerHarness PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
  target_link_libraries(RoboDrummerHarness PRIVATE
    robodrummer_core
    RoboDrummer
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)
  if(MSVC)
    target_compile_options(RoboDrummerHarness PRIVATE /UNDEBUG)
  else()
    target_compile_options(RoboDrummerHarness PRIVATE -UNDEBUG)
  endif()
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_test(NAME default_output_reference COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/build_compat_reference.py" "${CMAKE_BINARY_DIR}" --jobs 2 --config "$<CONFIG>")
  set_tests_properties(default_output_reference PROPERTIES FIXTURES_SETUP default_output_reference TIMEOUT 2400)
  add_test(NAME plugin_harness COMMAND RoboDrummerHarness
    --baseline-reference "${CMAKE_BINARY_DIR}/compat-reference/reference.bin")
  set_tests_properties(plugin_harness PROPERTIES FIXTURES_REQUIRED default_output_reference)

  if(ROBODRUMMER_BUILD_CLAP)
    FetchContent_Declare(
      clap_juce_extensions
      GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
      GIT_TAG 7adee3a1bd4684d4caa5601100e364abccff4b4d
      GIT_SUBMODULES_RECURSE TRUE
    )
    FetchContent_MakeAvailable(clap_juce_extensions)
    clap_juce_extensions_plugin(
      TARGET RoboDrummer
      CLAP_ID "com.circuitdriftlabs.robodrummer"
      CLAP_FEATURES instrument drum-machine stereo
    )
  endif()

  # Stage notices from the exact dependency sources used by this build, including
  # configurations that place FetchContent outside the build directory.
  set(ROBODRUMMER_NOTICES_DIR "${CMAKE_BINARY_DIR}/third-party-notices")
  file(MAKE_DIRECTORY "${ROBODRUMMER_NOTICES_DIR}")
  configure_file("${juce_SOURCE_DIR}/LICENSE.md" "${ROBODRUMMER_NOTICES_DIR}/JUCE-LICENSE.md" COPYONLY)
  configure_file("${juce_SOURCE_DIR}/JUCE.spdx.json" "${ROBODRUMMER_NOTICES_DIR}/JUCE.spdx.json" COPYONLY)
  if(ROBODRUMMER_BUILD_CLAP)
    configure_file("${clap_juce_extensions_SOURCE_DIR}/LICENSE.md"
      "${ROBODRUMMER_NOTICES_DIR}/clap-juce-extensions-LICENSE.md" COPYONLY)
    configure_file("${clap_juce_extensions_SOURCE_DIR}/clap-libs/clap/LICENSE"
      "${ROBODRUMMER_NOTICES_DIR}/CLAP-LICENSE" COPYONLY)
    configure_file("${clap_juce_extensions_SOURCE_DIR}/clap-libs/clap-helpers/LICENSE"
      "${ROBODRUMMER_NOTICES_DIR}/clap-helpers-LICENSE" COPYONLY)
  endif()
endif()
