# RoboDrummer Architecture

RoboDrummer is split into a portable musical core and host-specific adapters. The portable core deliberately has no JUCE dependency. This keeps timing, groove generation, scheduling and MIDI command behavior directly testable and keeps the VST3, CLAP, AU and standalone host adapters focused.

## Portable musical modules

`MusicalClock` is a portable, independently tested clock from Phase 1. The live renderer uses `plugin/JamEngine.h`'s sample cursor and musical bar timeline; `PluginProcessor` applies timing authority and phase corrections to that engine.

`DrumEvent` is the realtime event value type. `EventScheduler` is a fixed-capacity scheduler that keeps events ordered by sample offset without allocating during `push()`.

`Style` stores behavior probabilities. `GrooveGenerator` creates a deterministic bar from a style, musical context and seed. The style library, fills, phrase context and virtual-limb constraints build on that event contract.

`MidiCommand` and `JamState` define optional performance intervention. Intervention notes request fills, section transitions, intensity changes and transport-like behavior. The selectable MIDI Clock input separately supplies tempo through clock/start/continue/stop messages.

## Realtime boundary

The plugin audio callback reads snapshots and processes fixed-capacity events. It must perform no allocation or deallocation, blocking, locks, file/network I/O, logging or GUI operations. Sample loading, preset serialization, analysis model setup and GUI operations belong on loader or message threads. Deterministic bounded analysis remains inline under ADR 10.

## Live pipeline

1. JUCE VST3/CLAP/AU and standalone adapters.
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


## Bounded host adapters and ownership contracts

| Module | Responsibility and invariants | Dependencies / failure behavior |
|---|---|---|
| `core/MidiBlockPolicy.h` | Non-owning message classification; at most 4,096 input events and 8,192 accounted output bytes per callback. Independent ordinary, generated and critical quotas protect note-offs/Stop. | No JUCE dependency. Ordinary excess is dropped. Scan, command-queue, generated/critical or clock/transport exhaustion stops playback and emits MIDI Stop plus All Sound Off/All Notes Off on all channels. Zero-length callbacks defer this output. Explicit Resume, later Start/Continue, or host Stop then Play recovers. |
| `plugin/PluginProcessor.*` MIDI adapter | Reserves its scratch buffer in preparation; reads raw host metadata and copies final output without swapping host storage. | Allocation/free checks include long/cumulative SysEx and replacement host buffers. Host-owned output storage must also be prebuffered to 8,192 bytes; the plugin cannot control a host's supplied capacity. |
| `audio/RealtimeOwnerHandoff.h` | Loader owns publication; audio borrows current sampler and retires old owners only with capacity available; message thread reclaims. | A full retirement queue retains current/pending ownership and postpones adoption. Callback never deletes or blocks. Preparation/shutdown quiesce loader jobs before reset. |
| `audio/KitLoaderLifecycle.h` | Cancellation plus checked loader joining off the audio thread. | A 15-second grace expiry is followed by a real join, never destruction of live job state. Uncancellable filesystem I/O can extend teardown. |
| `audio/KitImportPolicy.h`, `KitBuilder.*` | Select deterministic supported slots before decoding; account at most 256 MiB peak decoded/resampling memory per import. Sanitize decoded values before interpolation/effects. | Skipped/duplicate/oversized files are reported, missing instruments use built-in fallbacks. Ordinary samples retain their levels; invalid values become zero and extreme values clamp to ±16. |
| `audio/DrumSamplePlayer.h` | Hit velocity remains voice-owned; current instrument mix affects ringing voices through 5 ms ramps. | Unchanged cached mix retains exact default output. No callback allocation. |
| `core/SectionSequencer.h` | Direct forward seeking is bounded by arrangement capacity, including loops/manual/final sections. | One-bar transitions retain ordinary cues. Multi-bar seeks select the final section without replaying skipped fills/crashes; backwards seeks rebase. Riff catch-up also has a fixed bound. |
| `analysis/AutoSetup.h`, processor state gate | Generation tokens reject superseded listening, queued, review and retained Apply results. | A recursive off-audio gate serializes preset/state/Apply/Undo. Checks around host notifications stop stale same-thread reentrant writes; callbacks never acquire this gate. Invalid presets preserve current state/proposal/Undo. |
| `plugin/PresetManager.*` | Validate supported legacy XML and a per-type allowlist before committing. | Unknown/out-of-slice fields are ignored; malformed known fields reject the whole input; finite values clamp and nonfinite values retain their historical ignore policy. Presets never start practice. |
| `plugin/ReducedMotionPreference.*` | Injectable platform preference reader and portable explicit-setting precedence. | macOS accessibility preference / Windows client-area animation; unavailable Linux preference defaults off. Explicit saved on/off wins. Reduced motion freezes onset/grid pixels until accessible Update while FIFO draining continues on hidden tabs. |
| `tests/support/WavReader.h`, `tools/check_fixtures.py` | Bounded offline PCM ingestion, manifest/license/hash/schema validation, production analysis traces and separate raw/folded tempo/lock/beat metrics. | Missing optional corpus reports a skip; required missing/corrupt input fails. Synthetic characterization cannot establish real-guitar qualification. No recordings are downloaded implicitly. |

Parameter IDs/order, plugin identity and supported state/preset formats remain unchanged. Analysis stays on the audio thread under ADR 10; transient enhancement (ADR 16) and learned models (spec 14) retain their real-data gates.


## Tempo, transport and meter authority

Internal BPM controls base tempo and ignores host play/stop and PPQ, while retaining the supplied host time signature. Manual meter overrides that signature; without either, the default is 4/4 and the existing automatic-meter gate can supply a confident suggestion. MIDI Clock can own tempo without replacing host/manual meter priority. Dedicated actual-callback regressions characterize this existing separation without new controls or changed automation.

The JamEngine sample timeline is clamped before conversion to 0..2^52, with saturating advances/nudges and headroom for block/bar arithmetic. Giant finite PPQ, backwards recovery and ordinary exact-event traces are covered under UBSan. Offline full-corpus qualification requires at least 20 distinct licensed clips, 48 kHz primaries and 44.1 kHz variants for at least three IDs. Every supplied rate variant runs at 64/512/2048 samples; variants cannot inflate acceptance. Invalid metadata replaces stale output with an ERROR artifact. Synthetic characterization cannot establish real-guitar qualification.


Oversized adaptive callbacks cannot rely on landing inside a narrow beat-phase window. The processor latches one future beat from the analyzer's accepted PLL anchor and period, consumes that countdown on later callbacks and translates the shared event array at entry. Rejected offbeat onsets do not move that anchor. State/listening/mode/preparation changes invalidate pending entry. Audio uses repeated fixed-scratch chunks, preserving the single-chunk operation order for ordinary buffers and aligning MIDI, capture and practice clicks throughout the full host buffer.
