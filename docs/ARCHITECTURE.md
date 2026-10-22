# RoboDrummer Architecture

RoboDrummer is split into a portable musical core and host-specific adapters. The portable core deliberately has no JUCE dependency. This keeps timing, groove generation, scheduling and MIDI command behavior directly testable and allows the VST3, standalone and later CLAP layers to remain thin.

## Phase 1 modules

`MusicalClock` owns tempo, meter, beat position and bar position. It accepts sample advances and exposes value snapshots. Future guitarist-following code will correct this clock gradually rather than replacing it.

`DrumEvent` is the realtime event value type. `EventScheduler` is a fixed-capacity scheduler that keeps events ordered by sample offset without allocating during `push()`.

`Style` stores behavior probabilities. `GrooveGenerator` creates a deterministic bar from a style, musical context and seed. The generator is intentionally simple in Phase 1; future style models can add ghost notes, cymbal state, fills, phrase context and limb constraints without changing the event contract.

`MidiCommand` and `JamState` define optional performance intervention. MIDI is not the main timing source. It requests musical actions such as fills, section transitions, intensity changes and transport-like behavior.

## Realtime boundary

The future plugin audio callback may read immutable snapshots and push/read fixed-capacity events. It must not perform disk access, network access, blocking waits or unbounded allocation. Sample loading, preset serialization, analysis model setup and GUI operations belong on worker or message threads.

## Planned next layers

1. JUCE VST3 and standalone wrapper.
2. Internal drum sampler and host MIDI output.
3. Groove/fill/arrangement expansion.
4. Guitar input feature extraction.
5. Online tempo and beat-phase tracker with confidence.
6. Hybrid leadership and graceful resynchronization.
7. Phrase-level jam coordination and Jam Memory.

The fast synchronization layer and the slower musical-coordination layer must remain separate. Beat/phase corrections happen continuously; fills, section interpretation and intensity arcs operate over beats and bars.

## Modules added in 0.3.0

| Area | Files |
|---|---|
| Styles / grooves | `core/Style.h`, `core/StyleLibrary.h`, `core/GrooveGenerator.h` (7 groove families, humanization), `core/JamStyleProfile.h` |
| Fills / drummer | `core/FillGenerator.h`, `core/DrummerPersonality.h`, `plugin/JamEngine.h` (beat-indexed fill planning) |
| Arrangement | `core/SectionSequencer.h` (per-section feel, transition, personality, overrides) |
| Input / analysis | `analysis/InputConditioner.h`, `analysis/CalibrationAnalyzer.h`, `analysis/OnsetDetector.h` (spectral flux), `analysis/PerformanceAnalyzer.h`, `analysis/RhythmAnalyzer.h` (phase-locked loop, tempo-change detection), `analysis/TempoMovement.h`, `analysis/TempoTracker.h` (rich hypotheses), `core/Lookahead.h` |
| Timing | `analysis/TimingAuthorityController.h`, `analysis/PhaseFollower.h` (follow strength, inertia, correction speed, sensitivity), `core/TapTempo.h`, `core/MidiClockTracker.h` |
| MIDI | `core/MidiLearn.h` (lock-free CC and note bindings) |
| Sound | `audio/DrumSamplePlayer.h` (aux buses, send), `audio/KitBuilder.*` (built-in + user kits), `core/Mixer.h`, `core/KitFileName.h` |
| Plug-in | `plugin/PluginProcessor.*`, `plugin/PresetManager.*`, `plugin/EditorPanels.h`, `plugin/EditorWidgets.h`, `plugin/PluginEditor.*` |
| Tests | `tests/*.cpp` (JUCE-free), `tests/harness/PluginHarness.cpp` (headless plug-in) |

Threading: the audio thread owns all analysis and generation state; the UI reads atomics and a lock-free onset FIFO; kits are built on a loader thread and handed over through an atomic pointer swap (old samplers are deleted on the message thread); MIDI Learn writes land in per-target atomics that the message-thread timer applies to host parameters.

## MIDI capture boundary (0.4.0)

`core/MidiCapture.h` owns an allocation-free audio producer and a message-thread consumer. The producer records segment, tempo, meter and note records into a fixed SPSC ring; only the consumer owns the take vectors and calls `core/MidiFileWriter.h` to export. Clear discards queued history and requests producer resynchronization. Each segment carries the producer's 24-bit take sequence, matching the packed latest-position sequence even if clear discarded undrained segments. Overflow drops the newest record and begins another take; history retention and export allocate only on the message thread. See spec 05 and `feature-audit/CAPTURE_QA_2026-10-22.md` for the clear/velocity regressions.
