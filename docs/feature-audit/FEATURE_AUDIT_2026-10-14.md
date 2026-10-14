# RoboDrummer feature audit — 2026-10-14

| | |
|---|---|
| Repository | `djshellshoxxx/robodrummer` |
| Base | `main` @ `2fb24fb` ("Vendor Superpowers and audio/JUCE skills") |
| Audit + completion branch | `claude/elegant-lamport-j4zy12` (PR [#30](https://github.com/djshellshoxxx/robodrummer/pull/30)) |
| Specification | `SPEC.md` v0.1 + `docs/standards/CDL_PLUGIN_BASELINE.md` |
| Method | Read the whole code base and SPEC, ran the existing suites, traced every SPEC section from UI/MIDI/host entry point to audio output, implemented the gaps, then re-ran everything (core suite, new tracking fixtures, headless plug-in harness, fuzzing, soak, CI on Windows/Linux/macOS). |

Classification used below: **Verified** = implemented and covered by a test that actually executes in CI; **Coded** = implemented, no automated evidence; **Partial**; **Stub**; **Missing**; **Out of scope** (SPEC says so); **Proposed** (idea only).

## 1. Findings on `main` before this work

### Defects found (all fixed on this branch, each with a regression test)

| # | Finding | Evidence on `main` | Fix | Regression test |
|---|---|---|---|---|
| D1 | **`Style::humanizeMs` was never used.** Every style declared a looseness but the generator emitted machine-exact hits; SPEC 22 humanization did not exist. | `src/core/Style.h:13` set, never read (`grep humanizeMs` → only definitions). | Style-aware triangular micro-timing, limb bias, push/lay-back feel, velocity variation (`GrooveGenerator::humanize`). | `tests/test_humanize.cpp` |
| D2 | **Host/MIDI tempo changes were slew-limited like guitar following.** In *Drummer Leads* a host tempo jump of 20 BPM took ~3 s to reach the drummer (6 BPM/s "Balanced" limit applied to the base tempo). | `TimingAuthorityController::update` slewed `currentBpm_` toward `desired` including the base. | Only the guitar contribution is slewed; the base tempo passes straight through. | `tests/harness` "tempo sources", `tests/test_timing_authority.cpp` |
| D3 | **The tempo tracker could not follow a gradual accelerando.** A 108→128 BPM ramp left the estimate at ~108 for the whole ramp; confidence then collapsed and the drummer lost the guitar. | Inter-onset histogram decays 0.965/onset; best bin only moves with a 1.18× score margin. Reproduced in `/tmp` fixture and in the plug-in harness. | Phase-locked loop on the beat period (re-anchored per onset) layered over the histogram; confidence from loop consistency. | `tests/test_tracking_fixtures.cpp` (ramp), harness "guitarist leads" |
| D4 | **A sudden tempo change (100→140) was never followed** (still 100 after 16 s). | Same histogram inertia; PLL window rejected every onset. | Run of consistent inter-onset intervals contradicting the loop (and not a musical subdivision of it) re-seeds the loop and forgets old histogram evidence. Settles in ~2 s. | `tests/test_tracking_fixtures.cpp` (sudden change) |
| D5 | Tracker lock had no hysteresis (SPEC 13). | `RhythmAnalyzer.h` lock was a pure threshold. | Enter at 0.38/0.30, leave at 0.28/0.20. | `test_rhythm_analyzer`, fixtures |
| D6 | Tempo range was 45–220 BPM; SPEC 8 says 40–240. | `TempoTracker.h` constants. | 40–240. | fixtures |
| D7 | Initial lock could stick to a wrong octave after the range change (acquiring state smoothed from a stale value). | Found while changing D6. | Acquisition snaps to a clearly better estimate; locked state still filters. | `test_rhythm_analyzer` |
| D9 | **End Jam (MIDI 49) only changed a status label.** `JamCoordinator` reported `EndingLikely`, but nothing ever ended the jam; SPEC 21 lists Outro among the bar states. | `PluginProcessor.cpp` `handleCommand` set a pending flag and returned; `JamCoordinatorDecision::executeTransition` is produced but never consumed anywhere (no-op wiring). | `JamEngine::requestOutro`: groove for the rest of the bar and the next, two-beat fill, crash + kick on the final downbeat, then stop (Resume restarts). `executeTransition` is redundant with the section sequencer and is documented as informational. | `tests/test_fill_generator.cpp` (outro), harness "end jam / outro" |
| D10 | **NaN/inf parameter values in saved state reached the audio thread** (found by the new state fuzzer: `kitTune`/`pan` = NaN → NaN sample position in the sampler). | `setStateInformation` trusted `replaceState` values; setters used `std::clamp`, which passes NaN through. | State is sanitised right after restore (non-finite → default, out-of-range → clamped); sampler and engine setters are NaN-safe. | harness "fuzzing" (400 mutated states, 100 garbage blobs, 40 randomised rounds) |
| D11 | The guitar was always copied to the output (and a mono guitar only to the left channel), doubling it in a DAW where the guitar has its own track. | `processBlock` left the input channels in place. | New **Guitar Monitor** level (0 % in hosts, 100 % in the standalone), mono guitar centred. | harness "guitar monitor" |
| D8 | Non-finite input samples passed straight into the monitor path and the analysis. | `processBlock` copied input unchanged. | NaN/Inf replaced by silence at the top of `processBlock`. | harness "robustness" |

### Gaps vs SPEC (before → after)

| SPEC § | Requirement | `main` | This branch | Evidence |
|---|---|---|---|---|
| 5 | Mono / stereo-sum / left / right input, DC removal, high-pass, gain/normalisation, gate | **Missing** (channel 0 only) | **Verified** | `src/analysis/InputConditioner.h:16`, `tests/test_input_conditioner.cpp`, `PluginProcessor.cpp` input block |
| 5 | Optional calibration (RMS/peak range, noise floor, transients, event density, dynamic range) | **Missing** | **Verified** | `src/analysis/CalibrationAnalyzer.h:29`, harness "calibration" |
| 6 | Feature outputs: onset probability, flux, band energy, density, regularity, silence/activity, accent | Partial (rms/peak/activity) | **Verified** | `src/analysis/PerformanceAnalyzer.h:26`, `OnsetDetector.h:16`, `tests/test_performance_features.cpp` |
| 7 | Weighted multi-feature onset detector | Partial (single energy feature) | **Verified** (broadband + band spectral flux, probability) | `tests/test_performance_features.cpp` |
| 8 | Hypothesis fields (supporting events, consistency, age, phase, predicted beat, half/double relation), 40–240 BPM | Partial | **Verified** | `TempoTracker.h` `richHypotheses`, `tests/test_tracking_fixtures.cpp`, ANALYSIS tab |
| 9/10 | Phase-locked clock, lookahead exposed | Partial (no loop; no lookahead) | **Verified** (PLL; `core/Lookahead.h`, shown in ANALYSIS) | `tests/test_lookahead.cpp` |
| 11 | Jitter / drift / accel / abrupt / half-double classification; tempo velocity; response presets | Partial (presets only) | **Verified** | `src/analysis/TempoMovement.h:27`, fixtures |
| 12 | Follow Strength, Tempo Inertia, Follow Range, Beat Correction Speed, Confidence Sensitivity; Tap tempo; MIDI clock; host | Partial (range/leadership only; no tap, no clock) | **Verified** | `TimingAuthorityController.h`, `PhaseFollower.h`, `core/TapTempo.h`, `core/MidiClockTracker.h`, `tests/test_follow_controls.cpp`, `test_midi_tools.cpp`, harness "tempo sources" |
| 13 | Confidence hysteresis | Missing | **Verified** | D5 |
| 16 | Meter 3/4, 6/8, 5/4, 7/8 with manual override | Verified on main | **Verified** | `test_meter_profiles.cpp`, `test_style_library.cpp` (every style × odd meters) |
| 18 | Intensity from loudness + density + transient strength + spectrum + activity | Partial (loudness + activity) | **Verified** | `test_performance_features.cpp` |
| 20 | 21 styles (Rock … Jazz/Swing), soft tempo regions, distinct behaviour | **Partial** (6 styles) | **Verified** (22 styles, 7 groove families) | `src/core/Style.h`, `GrooveGenerator.h`, `StyleLibrary.h`, `tests/test_style_library.cpp` |
| 22 | Style- and tempo-aware humanization | **Missing** (D1) | **Verified** | `tests/test_humanize.cpp` |
| 23 | Fill frequency/complexity/length(1 beat…2 bars)/intensity/surprise; queued vs immediate fill | **Partial** (single 1-beat fill) | **Verified** | `src/core/FillGenerator.h:30`, `JamEngine.h`, `tests/test_fill_generator.cpp`, harness "MIDI commands" |
| 21 | Bar states Normal/Build/Release/Fill/Transition/Break/Intro/Outro/Crash entry/Half-time/Double-time | Partial (no outro) | **Verified** (outro added; intro = the "establishing groove" bars with fills suppressed) | `JamEngine.h`, `test_fill_generator.cpp` |
| 24 | Jam coordination | Verified on main | Verified | `test_jam_coordinator.cpp` |
| 25 | Programmed song: name, bars, style, intensity, personality, fill frequency, leadership, follow strength, half/double, transition | **Partial** (style/bars/intensity/auto) | **Verified** | `core/SectionSequencer.h`, `SongPanel`, harness "programmed arrangement" |
| 26 | MIDI commands; expression-pedal defaults; MIDI Learn for live parameters | **Partial** (fixed note map) | **Verified** | `core/MidiLearn.h:15`, `MidiPanel`, harness "MIDI commands, learn" |
| 27 | Internal / MIDI-only / both; GM **and custom** maps | Partial (GM only, ch 10 fixed) | **Verified** (editable map, output channel) | harness "extended controls" |
| 28 | WAV/AIFF, velocity layers, round robin, choke, tuning, gain/pan, envelopes, per-instrument routing | Partial (sampler core only; synthetic kit only) | **Verified** | `audio/KitBuilder.cpp`, `core/KitFileName.h`, harness "user kit loading" |
| 29 | Mixer: Kick, Snare, Toms, Hi-Hat, Ride, Cymbals, Room, Master; gain/pan/mute/solo; multi-output | **Missing** | **Verified** | `core/Mixer.h`, `KitPanel`, harness "mixer, room and aux outputs" |
| 30 | Tabs LIVE/KIT/DRUMMER/STYLE/SONG/MIDI/ANALYSIS/SETTINGS; analysis view | **Missing** (single page) | **Verified** (screenshots rendered in the harness) | `src/plugin/EditorPanels.h`, `PluginEditor.cpp` |
| 31 | Presets: Jam/Drummer/Style/Kit/Song/Analysis; DAW state | Partial (state only) | **Verified** | `plugin/PresetManager.cpp`, harness "state round trip and presets" |
| 32 | Jam memory | Verified on main | Verified | `test_session_memory*.cpp` |
| 33 | Real-time safety | Coded (no evidence) | **Verified** (0 allocations over 1500 blocks incl. MIDI clock, aux buses, reverb, calibration) | `tests/harness` "real-time allocation audit", `test_realtime_safety.cpp` |
| 36 | Test layers incl. synthetic fixtures, CPU benchmark, host checks | Partial | **Verified** (fixtures, CPU, fuzz, soak, pluginval step) — real guitar fixtures **Missing** | `docs/QA.md` |
| 37 | Beta acceptance scenario | Not exercised | **Verified** end-to-end in the harness (acquire → join → accelerate → fill by MIDI → silence → resume) | harness |
| 38 Phase 5 | Learned models | Out of scope until deterministic layers validated | **Out of scope** | SPEC 38 |

### Still not implemented (explicit)

| Item | Status | Reason / next step |
|---|---|---|
| Automatic meter detection (SPEC 16 "later") | Missing | Manual meter always available (SPEC requirement). Needs real-guitar fixtures to tune. |
| Optional virtual-limb model (SPEC 22 "future") | Proposed | Marked "future" in the SPEC. |
| Transient-enhancement stage (SPEC 5 "may") | Missing | Optional; flux feature already covers the use case. |
| Analysis worker thread (SPEC 33) | Deviation | Analysis runs on the audio thread (0.7 % of a core in the harness, allocation-free). A worker adds latency; revisit if heavier models arrive. |
| Real guitar fixtures (SPEC 36) | Missing | No licensed recordings in the repository; synthetic fixtures only. |
| Learned beat/downbeat models (Phase 5) | Out of scope | SPEC gate not met. |
| AU target | Implemented in CMake (macOS only); **untested** here | Built by the macOS CI job; no AU host test performed. |
| Notarised/signed macOS & Windows binaries | Missing | Needs certificates; unsigned builds are documented in `INSTALL.md`. |

## 2. Wiring audit (placeholders, no-ops, dead code)

* `grep -rn "TODO\|FIXME\|XXX" src tests CMakeLists.txt .github` → none.
* Parameters: every host parameter (`docs/PARAMETERS.md`, generated from the code) is read on the audio thread (`rawXxx_` pointers or atomics) **and** has a GUI control; the harness exercises each group. `meterNumerator/meterDenominator/manualMeterEnabled` verified on main and unchanged.
* Orphans removed: the old editor's `setArrangementSection` API and the hard-coded 6-entry style tables in the processor/editor (now `kJamStyleNames`/`styleFor`).
* `MidiCommandMapper.h` is retained for the default map and its test; the processor now routes through `MidiLearnMap` (defaults identical).
* `Style::tempoMinBpm/MaxBpm` are *informational* (SPEC 20: soft preference); shown on the STYLE tab and never used to limit the clock.

## 3. Open PRs and issues (no open issues)

| PR | Title | Relationship |
|---|---|---|
| [#7](https://github.com/djshellshoxxx/robodrummer/pull/7) | Full initial style set | **Overlaps** (styles/groove primitives). Stacked on an unmerged base. |
| [#8](https://github.com/djshellshoxxx/robodrummer/pull/8) | Rich tempo hypotheses | **Overlaps** (`TempoTracker`). |
| [#9](https://github.com/djshellshoxxx/robodrummer/pull/9) | Selectable variable-length fills | **Overlaps** (`JamEngine`, fills). |
| [#11](https://github.com/djshellshoxxx/robodrummer/pull/11) | Deterministic style-aware humanization | **Overlaps** (`GrooveGenerator`). |
| [#12](https://github.com/djshellshoxxx/robodrummer/pull/12) | Real-time performance feature analysis | **Overlaps** (`PerformanceAnalyzer`). |
| [#13](https://github.com/djshellshoxxx/robodrummer/pull/13) | Guitar input modes + conditioning | **Overlaps** (input conditioning). |
| [#14](https://github.com/djshellshoxxx/robodrummer/pull/14) | Tempo movement classification | **Overlaps** (`RhythmState`). |
| [#15](https://github.com/djshellshoxxx/robodrummer/pull/15) | Multi-feature onset detection | **Overlaps** (`OnsetDetector`). |
| [#16](https://github.com/djshellshoxxx/robodrummer/pull/16) | Live timing follow controls | **Overlaps** (`TimingAuthorityController`). |
| [#29](https://github.com/djshellshoxxx/robodrummer/pull/29) | AGENTS.md modularity policy (docs only) | Compatible: this branch keeps parameter IDs stable, isolates DSP from UI, and adds tests with every change. |

Those draft PRs are stacked on unmerged bases (`codex/qa-assertions-enabled`, `codex/full-style-set`) and were **not** merged or reused. This branch is based on `main` and independent; the owner should decide whether to close them or port individual ideas. Nothing was pulled from them.

## 4. Compatibility notes for the owner

* **`jamStyle` grew from 6 to 22 choices** (existing indices 0–5 unchanged). Saved sessions restore correctly (state stores plain values), but a host automation lane recorded against the 6-entry list will map to different styles because host automation is normalised. Documented in the release notes.
* New parameters are all additive (stable IDs, version hint 1). State version is now 3; versions 1 and 2 load (`setStateInformation`).
* `PluginProcessor` now exposes four extra stereo **output buses** (disabled by default); hosts that do not support multi-out ignore them.

## 5. Prioritised backlog after this work

| Pri | Item | Acceptance criteria | Depends on |
|---|---|---|---|
| P1 | Real-guitar fixture set + tuning of the tracker (SPEC 36) | ≥ 20 licensed clips (clean/distorted/palm-muted/strum/arpeggio); tempo error < 2 BPM and lock < 6 s on ≥ 90 %; documented in `docs/QA.md` | recordings |
| P1 | Host matrix runs (Reaper, Ableton, FL, Cubase, Bitwig) | load/save/automate/resize/close checklist per host recorded | access to hosts |
| P2 | Automatic meter detection (3/4, 6/8, 5/4, 7/8) | ≥ 85 % on fixtures; always overridable | real fixtures |
| P2 | Signed Windows installer / notarised macOS | Authenticode / Developer ID in CI secrets | certificates |
| P3 | Right-click menu on combo boxes / toggles (CDL 2.3) | sliders already offer reset/copy/paste on right-click | none |
| P3 | Virtual-limb humanization | no physically impossible limb overlaps in generated bars | none |
| P3 | Optional analysis worker thread | no regression in latency; allocation audit still zero | profiling data |
| P3 | Learned beat/downbeat model (Phase 5) | beats deterministic baseline on the fixture set | P1 fixtures |

## 6. Evidence commands (run on this branch)

```text
cmake -S . -B build-core -DROBODRUMMER_BUILD_JUCE=OFF && cmake --build build-core && ctest --test-dir build-core   # 44/44 pass
cmake -S . -B build && cmake --build build --target RoboDrummer_VST3 RoboDrummer_CLAP RoboDrummer_Standalone RoboDrummerHarness
./build/RoboDrummerHarness_artefacts/Release/RoboDrummerHarness                     # 1329 checks, 0 failures
xvfb-run -a ./build/RoboDrummerHarness_artefacts/Release/RoboDrummerHarness --screenshots shots
```

CI results for the final head are recorded in `docs/QA.md`.
