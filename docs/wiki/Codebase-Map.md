# Codebase Map: where to find what

| I want to... | Look in |
|---|---|
| Portable clock component, groove/fill generation, scheduling, styles, arrangement, MIDI commands and practice state | [src/core/](../../src/core/) |
| Live rendering tempo, sample cursor and per-bar event generation | [plugin/JamEngine.h](../../src/plugin/JamEngine.h) |
| Conditioning, onset/tempo/downbeat/performance analysis and Auto Setup | [src/analysis/](../../src/analysis/) |
| MIDI take capture and file encoding | [core/MidiCapture.h](../../src/core/MidiCapture.h), [core/MidiFileWriter.h](../../src/core/MidiFileWriter.h) |
| Sample importing, built-in kit construction and fixed-voice playback | [audio/KitBuilder.cpp](../../src/audio/KitBuilder.cpp), [audio/DrumSamplePlayer.h](../../src/audio/DrumSamplePlayer.h) |
| DSP processing | [src/dsp/](../../src/dsp/) |
| JUCE host audio/MIDI/bus callbacks, state and engine integration | [plugin/PluginProcessor.cpp](../../src/plugin/PluginProcessor.cpp), [PluginProcessor.h](../../src/plugin/PluginProcessor.h) |
| Editor widgets/panels and preset-file adapters | [src/plugin/](../../src/plugin/) |
| Portable regression tests (automatically discovered `test_*.cpp`) | [tests/](../../tests/) and [CMakeLists.txt](../../CMakeLists.txt) |
| Real processor/editor integration tests | [tests/harness/](../../tests/harness/) |
| Build | [CMakeLists.txt](../../CMakeLists.txt), [BUILDING.md](../BUILDING.md), [JUCE_BUILD.md](../JUCE_BUILD.md) |
| Windows installer definition | [installer/RoboDrummer.iss](../../installer/RoboDrummer.iss) |
| CI and packaging | [.github/workflows/](../../.github/workflows/), [scripts/ci/](../../scripts/ci/) |
| Specs and contributor rules | [SPEC.md](../../SPEC.md), [docs/specs/](../specs/), [AGENTS.md](../../AGENTS.md) |
| Research | [research/](../../research/) |
| Published feature traceability | [0.4.0 feature audit](../feature-audit/FEATURE_AUDIT_0.4.0.md), [0.3.0 feature audit](../feature-audit/FEATURE_AUDIT_2026-10-14.md) |
