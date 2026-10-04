cmake_minimum_required(VERSION 3.22)
project(RoboDrummer VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ROBODRUMMER_BUILD_JUCE "Build VST3 and Standalone targets" ON)

add_library(robodrummer_core INTERFACE)
target_include_directories(robodrummer_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

enable_testing()
foreach(test_name IN ITEMS
    musical_clock
    event_scheduler
    groove_generator
    midi_commands
    host_transport_adapter
    midi_command_mapper
    drum_sample_player
    jam_engine
    onset_detector
    tempo_tracker
    rhythm_analyzer
    timing_authority
    phase_follower
    performance_analyzer
    dynamics_follower
    downbeat_tracker)
  add_executable(test_${test_name} tests/test_${test_name}.cpp)
  target_link_libraries(test_${test_name} PRIVATE robodrummer_core)
  add_test(NAME ${test_name} COMMAND test_${test_name})
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

  juce_add_plugin(RoboDrummer
    COMPANY_NAME "Circuit Drift Labs"
    BUNDLE_ID "com.circuitdriftlabs.robodrummer"
    PLUGIN_MANUFACTURER_CODE CDrf
    PLUGIN_CODE RDrm
    FORMATS VST3 Standalone
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
  )
  target_include_directories(RoboDrummer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(RoboDrummer PRIVATE
    robodrummer_core
    juce::juce_audio_utils
    juce::juce_dsp
  )
  target_compile_definitions(RoboDrummer PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
  )
endif()
