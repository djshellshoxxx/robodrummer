cmake_minimum_required(VERSION 3.22)
project(RoboDrummer VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ROBODRUMMER_BUILD_JUCE "Build VST3 and Standalone targets" ON)
option(ROBODRUMMER_BUILD_CLAP "Also build the CLAP target (requires ROBODRUMMER_BUILD_JUCE)" ON)

add_library(robodrummer_core INTERFACE)
target_include_directories(robodrummer_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

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

  target_sources(RoboDrummer PRIVATE
    src/plugin/PluginProcessor.cpp
    src/plugin/PluginProcessor.h
    src/plugin/PluginEditor.cpp
    src/plugin/PluginEditor.h
    src/plugin/EditorPanels.h
    src/plugin/EditorWidgets.h
    src/plugin/PresetManager.cpp
    src/plugin/PresetManager.h
    src/audio/KitBuilder.cpp
    src/audio/KitBuilder.h
  )
  target_include_directories(RoboDrummer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
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

  # Headless end-to-end checks against the real AudioProcessor (no host or audio device needed).
  juce_add_console_app(RoboDrummerHarness PRODUCT_NAME "RoboDrummerHarness")
  juce_generate_juce_header(RoboDrummerHarness)
  target_sources(RoboDrummerHarness PRIVATE tests/harness/PluginHarness.cpp)
  target_include_directories(RoboDrummerHarness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_compile_definitions(RoboDrummerHarness PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
  target_link_libraries(RoboDrummerHarness PRIVATE
    RoboDrummer
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)
  if(MSVC)
    target_compile_options(RoboDrummerHarness PRIVATE /UNDEBUG)
  else()
    target_compile_options(RoboDrummerHarness PRIVATE -UNDEBUG)
  endif()
  add_test(NAME plugin_harness COMMAND RoboDrummerHarness)

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
endif()
