# RoboDrummer JUCE Shell Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the verified plain-C++ RoboDrummer core into a playable JUCE VST3 and standalone application with host transport, MIDI intervention, internal sample playback, and a minimal live GUI.

**Architecture:** Keep the existing `robodrummer_core` independent of JUCE. Add a thin JUCE adapter layer that translates host transport/MIDI into core state and translates scheduled `DrumEvent`s into an internal sample player. The audio callback must remain allocation-free after `prepareToPlay`; disk I/O and sample loading occur off the audio thread.

**Tech Stack:** C++20, CMake 3.22+, JUCE 8 via CMake FetchContent, VST3, JUCE standalone target, CTest/GitHub Actions.

**Spec:** `SPEC.md`

## Global Constraints

- Single-guitar architecture only; multi-guitarist tracking remains out of scope.
- Build VST3 and standalone first; CLAP/AU remain later phases.
- Existing plain-C++ core tests must remain green.
- No disk I/O, heap allocation, mutex locking, network access, or blocking work in the realtime audio callback.
- Generated drum MIDI and internal audio must be driven from the same scheduled drum events.
- Adaptive guitar analysis is not part of this plan.

## Review Focus

- Host transport that lacks BPM/PPQ data must fall back safely rather than stopping/crashing.
- MIDI messages outside the command map must pass through/ignore cleanly without corrupting state.
- Missing/unloaded drum samples must produce silence for that instrument without breaking scheduling.
- Events on buffer boundaries must land in the correct buffer/sample offset.
- Plugin state restore must tolerate older or incomplete state blobs by applying defaults.

---

### Task 1: JUCE Build Targets

**Files:**
- Modify: `CMakeLists.txt`
- Create: `src/plugin/PluginProcessor.h`
- Create: `src/plugin/PluginProcessor.cpp`
- Create: `src/plugin/PluginEditor.h`
- Create: `src/plugin/PluginEditor.cpp`
- Test: existing CTest suite plus JUCE target compile

**Interfaces:**
- Produces `RoboDrummerAudioProcessor : juce::AudioProcessor` and `RoboDrummerAudioProcessorEditor : juce::AudioProcessorEditor`.

- [ ] Add JUCE through CMake `FetchContent` with a pinned tag.
- [ ] Add one JUCE plugin target producing VST3 and Standalone formats.
- [ ] Implement minimal processor/editor that compiles and clears output safely.
- [ ] Run core CTest and build both JUCE formats.

### Task 2: Host Transport Adapter

**Files:**
- Create: `src/plugin/HostTransportAdapter.h`
- Create: `tests/test_host_transport_adapter.cpp`
- Modify: `CMakeLists.txt`
- Modify: `src/plugin/PluginProcessor.cpp`

**Interfaces:**
- Produces `HostTransportSnapshot { bool playing; double bpm; double ppqPosition; int numerator; int denominator; bool validTempo; bool validPpq; }`.
- Produces pure helper mapping host optional data to safe defaults, testable without JUCE runtime.

- [ ] Write failing tests for missing tempo, normal 120 BPM transport, and non-4/4 meter.
- [ ] Implement minimal adapter helpers.
- [ ] Wire host transport into the processor without allowing missing host values to destabilize the core clock.
- [ ] Run full suite.

### Task 3: MIDI Command Mapping

**Files:**
- Create: `src/plugin/MidiCommandMapper.h`
- Create: `tests/test_midi_command_mapper.cpp`
- Modify: `src/plugin/PluginProcessor.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces `std::optional<jam::MidiCommand> mapNoteOn(int note, float velocity) noexcept` and CC mapping helper.

- [ ] Write failing tests for Fill, Next Section, Intensity Up/Down, Half-Time, Double-Time, Reset Listening, and unknown note.
- [ ] Implement mapping.
- [ ] Consume mapped MIDI commands in processor state while preserving unrelated MIDI.
- [ ] Run full suite.

### Task 4: Internal Drum Sample Player

**Files:**
- Create: `src/audio/DrumSamplePlayer.h`
- Create: `src/audio/DrumSamplePlayer.cpp`
- Create: `tests/test_drum_sample_player.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Supports preloaded mono/stereo floating-point sample buffers for each `DrumInstrument`.
- Produces `trigger(const DrumEvent&) noexcept` and `render(float** outputs, int channels, int samples) noexcept`.

- [ ] Write tests with synthetic impulse samples proving trigger timing, velocity scaling, overlap, and missing-sample silence.
- [ ] Implement fixed-voice playback with all voice/sample storage allocated before render.
- [ ] Run full suite.

### Task 5: Scheduled Groove-to-Audio Wiring

**Files:**
- Create: `src/plugin/JamEngine.h`
- Create: `tests/test_jam_engine.cpp`
- Modify: `src/plugin/PluginProcessor.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- `JamEngine` owns core clock, groove generator, scheduler, MIDI command state and exposes buffer-local drum events.

- [ ] Write failing test proving a 120 BPM basic-rock bar schedules kick/snare/hat events at deterministic sample offsets.
- [ ] Implement buffer scheduling across block boundaries.
- [ ] Wire events to `DrumSamplePlayer` and generated MIDI notes.
- [ ] Run full suite.

### Task 6: Minimal Live GUI and State Persistence

**Files:**
- Modify: `src/plugin/PluginProcessor.h/.cpp`
- Modify: `src/plugin/PluginEditor.h/.cpp`
- Create: `tests/test_state_model.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Parameters: Mode, BPM, Style, Leadership, Intensity, Complexity, Fill Level, Humanize.
- State serialization uses JUCE ValueTree/APVTS; malformed/missing values resolve to defaults.

- [ ] Write state-model tests for defaults and incomplete restore.
- [ ] Implement parameter/state model.
- [ ] Build minimal GUI showing BPM/mode/style/intensity plus transport and Fill/Reset controls.
- [ ] Run full suite and build VST3/Standalone.

### Task 7: CI and Documentation

**Files:**
- Modify: `.github/workflows/core-ci.yml`
- Create: `docs/JUCE_BUILD.md`
- Modify: `README.md`

**Interfaces:** none.

- [ ] Add JUCE configure/build jobs on Windows and Linux while retaining core-only tests.
- [ ] Document local prerequisites and artifact locations.
- [ ] Document that this stage is a non-adaptive drummer shell; guitar-follow analysis is Phase 3.
- [ ] Verify CI configuration and final repository state.
