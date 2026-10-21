# RoboDrummer 0.4.0 feature and wiring audit

| | |
|---|---|
| Repository / branch | `djshellshoxxx/robodrummer`, `integrate/beta` |
| Audited HEAD | `dff8091` ("Docs: README, COMPLIANCE, build notes, spec status for 0.4.0 UI"); fixes below are on top of it |
| Auditor | independent pass (did not build the features); only code reading, builds and test runs count as evidence |
| Environment | Linux x86-64 container, GCC, 2 cores. Windows, macOS, AU and real DAW hosts were **not** available |
| Predecessor | [FEATURE_AUDIT_2026-10-14.md](FEATURE_AUDIT_2026-10-14.md) (0.3.0 baseline, SPEC 5-36) |

Status words: **Implemented** (code present and covered by a test that runs in ctest or the harness), **Partial**, **Missing**, **N/A** (the spec says so, or it is a decision record). Severity: High (wrong output or audio-thread hazard), Medium (spec acceptance or release plumbing broken), Low (hygiene, docs, test gap), Info.

## 1. Result in one paragraph

All 110 host parameters have a UI control with an accessibility title and description, are consumed by the audio or engine path, are saved, restored and range-sanitised, and are listed in `docs/PARAMETERS.md` (the file is byte-identical to a fresh `--dump-parameters`). No orphan parameter, dead button, MIDI command without effect or MIDI Learn target without effect was found. The audit did find **two audio-thread/robustness defects** (MIDI buffer swap that could allocate; a damaged preset file could put NaN on every parameter), **one broken release-plumbing item** (the pre-release tag shown in About was never defined by the build) and a **blind spot in the allocation audit** (it could not see JUCE containers and never switched the 0.4.0 features on). All were fixed with regression tests. The "all defaults" baseline hash `4a85caa7203610d1` is unchanged (the harness regression step compares it on every run and passes).

## 2. Findings

| ID | Sev | Finding | Evidence | Status | Fix / test |
|---|---|---|---|---|---|
| F1 | High | `processBlock` ended with `midi.swapWith(midiOut_)`. That handed the pre-sized scratch buffer to the host and kept whatever buffer the host owned, so the next block's `addEvent` could reallocate on the audio thread. | With the C allocator counted, the existing audit reported 3 reallocations inside `processBlock` that came from `juce::MidiBuffer::addEvent` (`addr2line` of the captured stack). | Fixed `43959c4` | Copy out (`midi.clear(); midi.addEvents(midiOut_, ...)`). Harness "real-time allocation audit" now uses a persistent host-style MIDI buffer and counts only inside `processBlock`; it passes with 0. |
| F2 | Medium | The allocation audit counted only `operator new`. JUCE `HeapBlock`, `Array`, `MidiBuffer`, `AudioBuffer` use `malloc`/`realloc`, so it could not see F1. It also never enabled any 0.4.0 feature (bus, shaper, riff lock, limbs, auto meter, practice, auto setup). | `tests/harness/PluginHarness.cpp` before the change: hooks on `operator new` only; parameter set in `testRealtimeAllocations` contains no 0.4.0 parameter. | Fixed `43959c4`, `f1c056e` | glibc `malloc`/`calloc`/`realloc` are interposed and counted per thread (worker threads no longer pollute the count); `RD_ALLOC_TRACE=1` prints stacks. New `testFeatureInteractionsAndRealtime` runs every feature at once at 44.1/48/96/192 kHz with blocks of 1, 31, 64, 480, 512, 4096 and 40000 samples: 0 allocations, finite output, MIDI inside the block. |
| F3 | High | `PresetManager::setPlain` passed non-finite file values straight into `convertTo0to1`/`setValueNotifyingHost` (NaN and Inf survive `jlimit`). A hand-edited or damaged `.rdpreset` gave NaN parameters ("Internal BPM = nan") and non-finite audio. Saved *state* was already sanitised; presets were not. | New harness test failed 5 of 10 bad values (`nan`, `-nan`, `inf`) before the fix. | Fixed `0fc48ee` | Non-finite values are ignored, others clamped to the parameter range. `testPresetFuzz` loads presets whose every parameter and every arrangement slot is `nan`, `inf`, `1e99`, `-1`, `7`, text or empty, and requires finite audio and in-range parameters. |
| F4 | Medium | The About/Help version line supports a pre-release tag and a git hash (`ROBODRUMMER_PRERELEASE_TAG`, `ROBODRUMMER_GIT_HASH`), but nothing in CMake or CI ever defined them. A `0.4.0-beta.1` build said plain `0.4.0` (spec 17 acceptance: "About shows version with pre-release tag"). | `grep -rn PRERELEASE_TAG CMakeLists.txt scripts .github` found nothing. | Fixed `96bf6d7` | CMake cache variables `ROBODRUMMER_PRERELEASE_TAG` / `ROBODRUMMER_GIT_HASH` write a generated `RoboDrummerBuildInfo.h` (a new commit recompiles only its includers); `plan.sh` derives `pretag` from the version; all four platform jobs pass it. Harness checks the version line. Spec 18 text updated. |
| F5 | Low | CI never checked that `docs/PARAMETERS.md` matches the real parameter layout. | `scripts/ci/juce_checks.sh` | Fixed `96bf6d7` | The Linux harness step dumps the table and `diff`s it with the committed file. |
| F6 | Low | MIDI tab "Clear" button had no tooltip. | New harness check "every push button has a tooltip/description". | Fixed `2a0ecb0` | Tooltip added; check runs on all 9 tabs at both window sizes. Same check also requires a click handler on every visible button (0 dead buttons). |
| F7 | Low | Spec 05 requires a 1M-event capture soak; the test had 300k. | `tests/test_midi_capture.cpp` | Fixed `0fc48ee` | 334k blocks x 3 hits = 1.002M events, no drops, still exports. |
| F8 | Low | Stale documents: QA.md ("44 tests", "8 tabs", 1342 checks), SPEC.md 41-43 (0.3.0 status, specs 07/08 "planned"), spec 03 ("tests are NOT auto-globbed"), COMPLIANCE 3.2 audit text, spec 18 variable names. | Reading the files. | Fixed (docs commit) | Updated. |
| F9 | Low | Right-click menu (Reset/Copy/Paste/MIDI Learn, spec 15) is implemented on `ParamSlider`, `ParamCombo`, `ParamToggle` (`EditorWidgets.h:132`, mouse and Shift+F10 handlers at `:217`, `:280`, `:321`), and the paste parsers are unit-tested, but opening the menu itself is not automated (async `PopupMenu`). | Harness only calls `parseChoiceText`, `parseToggleText`, `findLearnTarget`. | **Open** | Gap labelled honestly; refactor the menu model into a testable function if it ever regresses. |
| F10 | Info | With a 40000-sample block (0.83 s, far outside real host use) in Hybrid mode the features were not engaged within 14 s of audio (limb revision 0, riff bars 0), whereas 4096-sample blocks and Drummer Leads at 40000 are fine. Not root-caused. | Observed while writing F2's test; the test treats that size as robustness-only (finite, no allocation). | **Open** | Not a supported configuration; recorded for completeness. |
| F11 | Info | `tempoSource = Internal BPM only` ignores host tempo and transport but still takes the host *meter* when the host supplies one (`PluginProcessor.cpp` "tempo source, transport and meter"). | Code reading. | **Open** (design question) | Documented behaviour in README says "free-running"; harmless in practice. |
| F12 | Medium | Nothing in this audit ran on Windows, macOS, AU or in a real DAW. The CI workflows were linted and the Linux packaging script was executed, not the Windows/macOS jobs. | Environment. | **Open** | See section 7. |
| F13 | Low | The malloc-level allocation audit is glibc-only. Windows and macOS CI runs of the harness still count `operator new` only. | `#if defined(__GLIBC__)` in the harness. | **Open** | Linux CI is the authoritative RT gate. |
| F14 | Low | Spec 11 (real-guitar fixtures), 12 (host matrix), 13 (signing, notarisation) are protocols or proposals with no execution evidence; no signing steps exist in the workflows. Spec 14 is gated and intentionally unimplemented. | `grep -rn secrets .github` finds none. | **Open** | Needs hardware, licences, accounts. |

Counts: 14 findings (High 2, Medium 3, Low 7, Info 2). Fixed 8 (F1-F8), open 6 (F9-F14), none High or Medium among the open ones except F12, which is an evidence gap rather than a defect.

## 3. Parameter matrix (all 110, generated `PARAMETERS.md` verified identical)

For every row: (a) UI control, (b) engine consumer, (c) saved, restored and sanitised, (d) documented. Column (c) is the same for every parameter: all go through the APVTS state, and `sanitiseParameters()` (`PluginProcessor.cpp:413`, called from `setStateInformation` at `:2301`) replaces non-finite values by the default and clamps to 0-1; presets now do the same (F3). Column (d): present in `docs/PARAMETERS.md` for all 110 (diff clean, and now enforced by CI). Column (a) is enforced by the harness for **all** parameters (`testEditorUi`: at least one bound control each, with accessibility title and tooltip, control follows a host-side change; 110 parameters counted). Column (b) is by code reading, evidence in the table.

| Parameters | UI (tab / panel) | Engine consumer (file:line) |
|---|---|---|
| `bpm`, `intensity`, `leadershipMode`, `leadership`, `followRange`, `followResponse` | LIVE | `parameterChanged` `PluginProcessor.cpp:387-410`; `followResponse` `:997` |
| `dynamicFollow` | SETTINGS | `parameterChanged` -> `setDynamicFollow`, used in the `SessionMemoryPolicy` call `:1120` |
| `jamStyle` | LIVE, STYLE | `parameterChanged` -> `setJamStyle` |
| `outputMode`, `roomSize`, `kitTune`, `kit` | KIT (`kit` also LIVE) | `:1367` output mode, `:1465` room, `:530` tune, `:456` kit loader |
| `arrangementEnabled` | SONG | `parameterChanged`, `:1060-1085` arrangement loop |
| `jamMemoryEnabled`, `manualMeterEnabled`, `meterNumerator`, `meterDenominator`, `silenceMode`, `silenceStopBars` | STYLE | `parameterChanged` `:395-410`; silence `:1168` |
| `tempoSource` | LIVE, MIDI | `:874` (`rawTempoSource_`) |
| `humanize`, `feel`, `complexity`, `fillFrequency`, `fillLength`, `fillComplexity`, `fillSurprise`, `drummer`, `crashTendency` | DRUMMER (several also LIVE) | `:1089-1110` (`DrummerControls`, `FillSettings`) |
| `inputMode`, `inputGain`, `autoGain`, `gateThreshold` | SETTINGS (`gateThreshold` also ANALYSIS) | `:760-763` (`InputConditioner`) |
| `followStrength`, `tempoInertia`, `beatCorrection`, `confidenceSensitivity` | SETTINGS | `:998-1001` |
| `midiOutChannel`, `guitarMonitor` | SETTINGS | `:1428`, `:865` |
| `gain_*`, `pan_*`, `mute_*`, `solo_*` (29) | KIT (`MixerStrip`) | `:525-528` (`applyMixerToSampler`), `:1450-1461` (room, master) |
| `drumBus*` (11), `transientShaper*` (5) | KIT | `readDrumBusParams` `:539`, `readShaperParams`, applied `:1446-1447` |
| `rlAmount`, `rlAccentCrash`, `rlSensitivity` | DRUMMER (`rlSensitivity` also ANALYSIS) | `feedRiffLock` `:1688-1693`, `JamEngine.h:244` |
| `limbModelAmount` | DRUMMER | `:1145` -> `JamEngine.h:262` |
| `autoMeterEnabled`, `autoMeterThreshold` | STYLE | `:945`, `:965` |
| `practiceEnabled`, `prac*` (19) | PRACTICE | `rawPrac_` `:1796`, `updatePracticeSettings`, `processJamWithPractice` `:1874` |

Related checks:

* MIDI Learn targets: 34 (`kLearnTargets`, `PluginProcessor.cpp:19`), all are real parameters with a consumer above; the table is a compile-time sized `std::array`. Learned CCs are written through the parameter, snapped to its steps (`drainMidiLearnToParameters`).
* MIDI commands: 20 (`MidiCommand`), default notes 36-55 on channel 16 (`MidiLearn.h:30`). `handleCommand` (`PluginProcessor.cpp:2036`) gives every command an effect: practice commands toggle parameters through `pendingParamToggles_` (applied on the 30 Hz timer), `TrainerRestart` calls `practice_.restartTrainer()`, the rest reach `jam_.apply`. Covered by harness "MIDI commands, learn" and "practice mode ... MIDI commands".
* Presets: Jam presets contain 108 of 110 parameters (all except `practiceEnabled` and `pracLoopOn`, deliberately, so a preset never starts a session); checked by the harness. Kit presets carry bus, shaper, mixer; Drummer presets carry riff lock and playability; Analysis presets carry auto-meter, follow and input settings. Factory presets are partial overlays by design.
* Tooltips: every parameter control and every visible push button has a tooltip or accessibility description (F6 fixed the only miss).
* State: version 4; versions 1-3 migrate with new parameters at defaults (harness "regression ... old state migrates"); NaN, huge and negative values for the 0.4.0 parameters (harness "fuzz: NaN / huge values") and 400 mutated states plus 100 garbage blobs.

## 4. Spec matrix

### 4.1 Spec files 01-18

| Spec | Status | Evidence |
|---|---|---|
| 01 Drum bus | Implemented | `src/dsp/DrumBus.h:44`; wiring `PluginProcessor.cpp:539-547`, `:1446-1447`; KIT panel; `tests/test_drum_bus.cpp` |
| 02 Transient shaper | Implemented | `src/dsp/TransientShaper.h:27`; wiring `:554-556`, `:1446`; `tests/test_transient_shaper.cpp` |
| 03 Riff lock | Implemented | `src/analysis/RiffLock.h:115`, `:218`; `feedRiffLock` `PluginProcessor.cpp:1686`; `JamEngine.h:244`; `tests/test_riff_lock.cpp`, harness "riff lock wiring" |
| 04 Practice mode | Implemented | `src/core/PracticeMode.h:110`; `PluginProcessor.cpp:1004-1020`, `:1341`, `:1874`; PRACTICE tab; `tests/test_practice_mode.cpp`, harness "practice mode" |
| 05 MIDI capture | Implemented | `src/core/MidiCapture.h:61`; `PluginProcessor.cpp:1417`; CAPTURE panel; `tests/test_midi_capture.cpp`, harness "MIDI capture and export wiring" |
| 06 Auto setup | Implemented | `src/analysis/AutoSetup.h:25`; `PluginProcessor.cpp:1723`; Learn My Playing card; `tests/test_auto_setup.cpp`, harness "auto setup" |
| 07 Auto meter | Implemented | `src/analysis/MeterDetector.h:18`; `PluginProcessor.cpp:943-985`; `tests/test_meter_detector.cpp`, harness "automatic meter detection wiring" |
| 08 Virtual limbs | Implemented | `src/core/LimbModel.h:69`; `JamEngine.h:262-295`; `tests/test_limb_model.cpp`, harness "virtual limb model wiring" and the new double-time/half-time/riff-lock check |
| 10 Analysis worker ADR | N/A (decision: keep on audio thread) | Allocation and CPU evidence re-run: 0 allocations, 0.46 % load |
| 11 Real-guitar fixtures | Missing (protocol only) | No licensed corpus in the repository |
| 12 Host matrix QA | Missing (not run) | No DAW available; pluginval part executed, see section 6 |
| 13 Signing / notarisation | Missing (proposal) | No signing steps in `.github/workflows`; unsigned beta documented in INSTALL.md |
| 14 Learned models | N/A (gated, nothing implemented by design) | Spec header |
| 15 Combo context menu | Implemented, menu invocation untested (F9) | `EditorWidgets.h:132`, `:217`, `:280`, `:321`; parsers tested |
| 16 Transient enhancement ADR | N/A (decision: skip) | Spec header |
| 17 Accessibility and help | Implemented (two documented deviations in COMPLIANCE) | `EditorWidgets.h:408` focus ring; `EditorPanelsExtra.h:645` Help overlay generated from `docs/HELP.md`; About tag fixed in F4 |
| 18 Release process | Partial | Versioning, checksums, pre-release flag, AU ad-hoc signing, Linux archive: present. Blocking pluginval in the release job: not present (pluginval runs in `core-ci.yml`). Signing: absent (spec 13) |

### 4.2 Acceptance criteria of specs 01-08, 15 and 17

| Spec | Criterion | Test that runs | Result |
|---|---|---|---|
| 01 | Bypass bit-exact | `test_drum_bus.cpp:208-236` (`memcmp`) | pass |
| 01 | Latency 0 | `test_drum_bus.cpp:237` | pass |
| 01 | No allocation after construction | harness 1500-block audit and new all-features audit (7 configurations) | pass |
| 01 | GR within 0.05 dB of formula | `test_drum_bus.cpp:81-101` | pass |
| 01 | Time constants within 5 % | `test_drum_bus.cpp:112-129`, six sample rates 44.1-192 kHz | pass |
| 02 | A=+1 / -1 shifts the first 10 ms by > 3 dB, S likewise for the tail | `test_transient_shaper.cpp:65-82`, four sample rates | pass |
| 02 | Gain profile within 0.5 dB across -30..-6 dBFS | `test_transient_shaper.cpp:86-91` | pass |
| 02 | Bypass bit-exact, latency 0 | `:146-182` | pass |
| 03 | Amount 0 bit-identical, unlocked and grid mismatch identity | `test_riff_lock.cpp:138` | pass |
| 03 | No snare change; kicks within style cap | `test_riff_lock.cpp:174-230` | pass |
| 03 | Confidence below gate on random input in 200 seeded trials | `test_riff_lock.cpp:118-122` | pass |
| 04 | Header JUCE-free | core tests build with `-DROBODRUMMER_BUILD_JUCE=OFF` | pass |
| 04 | No allocation | all-features audit with practice on (count-in, loop, trainer, gate, rest bar) | pass |
| 04 | Trainer never overrides BPM in guitar-following modes | harness practice block "only a hint when the host owns the tempo, and when the guitarist leads" | pass |
| 05 | Fixed storage, 1M-event soak | `test_midi_capture.cpp` (1.002M events, added) | pass |
| 05 | Exported ticks equal recorded ticks | `test_midi_capture.cpp:92-127` | pass |
| 05 | File opens in a DAW with correct tempo and meter | SMF parsed and checked (tempo/meter meta events, harness). Opening in a DAW: **not run** | partial |
| 06 | Steady 100 BPM: 98..102, Hybrid, leadership 0.70 | `test_auto_setup.cpp:102-113` | pass |
| 06 | Irregular: Drummer Leads, inertia +0.2, lower follow | `test_auto_setup.cpp:126-135` | pass |
| 06 | Silence, clipping, NaN, cancel/timeout | `:148-166`, `:226-250` | pass |
| 06 | Identical results regardless of block size; Undo exact | `:209`, `:281-309` | pass |
| 07 | >= 85 % per meter over 100 seeds; <= 1 change; gate never applies with manual meter | `test_meter_detector.cpp:94-101`, `:164-179`, `:253-291` | pass |
| 08 | Zero violations at amount 1; protected hits kept; amount 0 bit-exact; deterministic; nothing added | `test_limb_model.cpp:81-278`, sweep `:338`; harness unplayable-hit count at 240 BPM and, new, at double-time/half-time with riff lock | pass |
| 15 | Reset, Copy, Paste on every combo and toggle; invalid paste changes nothing | code (`EditorWidgets.h`), parsers tested; menu itself not automated | partial (F9) |
| 15 | MIDI Learn / Clear only for learn targets | `findLearnTarget` tests; targets are the 34 listed | pass |
| 15 | Zero new audio-thread allocations | audits above | pass |
| 17 | Every control reachable by keyboard; focus visible | harness "keyboard focus" (Tab traversal on 4 tabs, no trap) | pass (Reaper not run) |
| 17 | Every parameter control has a screen-reader title and description | harness, now for all 110 | pass |
| 17 | Reduce motion and high contrast persist | harness settings round trip | pass |
| 17 | Help from `docs/HELP.md`; About shows version with tag and format list | `juce_add_binary_data`; harness Help checks; tag fixed (F4) | pass |

### 4.3 SPEC.md sections 1-43

| § | Topic | Status | Evidence |
|---|---|---|---|
| 1-3 | Product, principles, workflow | Implemented | `PluginProcessor.cpp`; harness "beta acceptance scenario (SPEC 37)" |
| 4 | Architecture | Implemented, one deviation | Analysis on the audio thread (ADR 10) |
| 5 | Input and conditioning | Implemented | `InputConditioner.h:16`, `CalibrationAnalyzer.h:29`; "may" transient enhancement skipped by ADR 16 (N/A) |
| 6-7 | Features, onsets | Implemented | `PerformanceAnalyzer.h:26`, `OnsetDetector.h:16` |
| 8-11 | Tempo, phase, prediction, movement | Implemented | `TempoTracker.h:34`, `PhaseFollower.h:14`, `Lookahead.h`, `EventScheduler.h:7`, `TempoMovement.h:16`; `test_tracking_fixtures` |
| 12-15 | Leadership, confidence, recovery, silence | Implemented | `TimingAuthorityController.h:28`, `ResyncPlanner.h:13`, `SilenceController.h:33` |
| 16 | Meter and downbeat | Implemented (auto meter, formerly "later", added) | `DownbeatTracker.h:15`, `MeterSelection.h:12`, `MeterDetector.h:18` |
| 17-18 | Analyzer, intensity | Implemented | `PerformanceAnalyzer.h`, `DynamicsFollower.h:7` |
| 19-23 | Philosophy, styles, grooves, humanisation, fills | Implemented (virtual limbs, formerly "future", added) | `Style.h:29`, `GrooveGenerator.h:12`, `FillGenerator.h:30`, `LimbModel.h:69` |
| 24-25 | Jam coordination, song mode | Implemented | `JamCoordinator.h:55`, `SectionSequencer.h:36` |
| 26-27 | MIDI intervention and output | Implemented | `MidiLearn.h:15`, `PluginProcessor.cpp:2036`, `:1426` |
| 28-29 | Sampler, mixer | Implemented | `DrumSamplePlayer.h:12`, `KitBuilder.h:22`, `Mixer.h` |
| 30 | GUI | Implemented (9 tabs; SPEC lists 8, PRACTICE added) | harness editor checks at 960x680 and default size |
| 31-32 | Presets, jam memory | Implemented (F3 fixed) | `PresetManager.h:23`, `SessionMemory.h:38` |
| 33 | Threading, real-time safety | Implemented with evidence now covering 0.4.0 (F1, F2) | allocation audits, ASan/UBSan |
| 34-35 | Modules, pseudocode | Implemented / N/A (informative) | repository layout |
| 36 | Testing strategy | Partial | Synthetic fixtures, fuzz, soak, pluginval done; real-guitar corpus and host matrix missing (F14) |
| 37 | Beta acceptance scenario | Implemented (automated) | harness `testBetaAcceptanceScenario`; manual host part not run |
| 38 | Phases | Phases 1-4 Implemented, Phase 5 N/A (gated, spec 14) | |
| 39 | Non-goals | N/A | |
| 40 | Research requirements 1-7 | Implemented | causal analysis (no look-ahead beyond the lookahead window), separate sync/coordination layers, hysteresis and hypotheses, style-aware microtiming |
| 41-43 | v0.3 additions, spec index, status | Implemented; index and status text updated | `SPEC.md` |

## 5. Real-time safety audit of the new components

Code inspection (grep for `std::vector`, `push_back`, `new`, `std::string`, `mutex`, `printf`, `std::function`, `juce::String` in the audio-thread headers `DrumBus.h`, `TransientShaper.h`, `RiffLock.h`, `LimbModel.h`, `MeterDetector.h`, `PracticeMode.h`, `AutoSetup.h`): no allocation, locking or I/O. The only hits are `snprintf` into caller buffers for UI text (message thread) and `MidiCapture.h` vectors, which are used only by the drain/export side (message thread); the audio side writes a fixed 8192-entry SPSC ring. Non-finite inputs: bus and shaper tests (`testNonFinite`), riff lock NaN/Inf/huge onsets, limb `NaN` amount, auto meter `std::isfinite` guard, practice parameters via fuzz.

Dynamic evidence: allocation audit with malloc counting (F2): 0 allocations in `processBlock` for the legacy scenario (1500 blocks) and for the new all-features scenario in 7 sample-rate/block combinations, which also applies Auto Setup while playing, restores saved state in the middle of a practice session, switches manual and automatic meter, changes timing mode, cycles every MIDI command, sends expression CCs and plays an outro. The same test requires the features to have been exercised (bus gain reduction, limb revisions, riff bars, practice phase, auto meter) and that output stays finite and below 64.

## 6. Cross-feature interactions checked

| Interaction | How verified | Result |
|---|---|---|
| Practice trainer vs guitar-following modes | harness practice block (guitarist leads, host tempo) plus all-features run switching to Guitarist Leads with the trainer on | trainer only a hint; finite, no allocation |
| Riff lock vs fills, outro, half-time | `JamEngine.h:244-247` skips riff transform on fill bars; new harness case riff lock + double/half time + fills + limbs; all-features run issues Fill, Half-Time, Double-Time, End Jam, Resume | pass |
| Limb model vs double-time / half-time | new `testLimbsWithTimeFeelAndRiffLock`: zero unplayable hits at the effective tempo (130 x 2, 220 x 0.5), model off produces hits so the check is meaningful | pass |
| Capture vs tempo changes, loops, stop/start | `test_midi_capture.cpp` (tempo map, seek/resync, stop gaps, free-running ticks) and harness capture block; practice clicks excluded by `kClickTag` | pass |
| Auto meter vs manual vs host meter | harness: manual wins (5/4), host wins, ambiguous input stays 4/4; `test_meter_detector.cpp:253-291` | pass |
| Auto Setup applied while playing; state restored mid-practice | all-features run | pass |
| Effects at 44.1/48/96/192 kHz, tiny blocks | `test_drum_bus.cpp:113`, `:357`, `test_transient_shaper.cpp:66`, block independence at 1 sample; harness 7 configurations | pass |
| Everything at parameter extremes with commands and odd blocks | new `testExtremeParameterFuzz`: 60 rounds, all 110 parameters set to 0, 1 or random, re-rolled in mid-run, random block sizes (1-1500), MIDI commands, CCs and clock | pass |

Tools: ASan + UBSan build of all 53 core tests: 53/53 pass, no reports. `pluginval` 1.0.3 at strictness 10 on the VST3 under Xvfb (`--validate-in-process`): exit 0, 25 test groups completed, including parameter fuzzing, bus layouts and state restore.

## 7. Release plumbing

* `actionlint` (1.7.12) on both workflows and `shellcheck` on `scripts/ci/*.sh`: clean, before and after the changes.
* `targets.sh mac` yields VST3, AU, CLAP, Standalone; Linux and Windows yield no AU. CMake adds AU only `if(APPLE)`, matching.
* `package.sh linux-x64` was executed against a real build: the archive holds `RoboDrummer.vst3/`, `RoboDrummer.clap`, the standalone, LICENSE, COPYRIGHT-TRADEMARK.md and INSTALL.md, plus a `.sha256`. Artefact paths `build/RoboDrummer_artefacts/Release/{VST3,CLAP,Standalone,AU}/...` agree with the actual build tree.
* `installer/RoboDrummer.iss`: file names and components agree with `package.sh` (`RoboDrummer.vst3\*`, `.clap`, `.exe`, INSTALL.md, LICENSE, COPYRIGHT-TRADEMARK.md); it receives the numeric `X.Y.Z` only. Not executed (needs Inno Setup).
* Version consistency: CMake `0.4.0`; JUCE version string derives from it; README, COMPLIANCE, JUCE_BUILD, BUILDING and the new release notes say 0.4.0; the in-plug-in line is `RoboDrummer 0.4.0[-tag]` (F4). Parameter table unchanged by this audit (110).
* `docs/RELEASE_NOTES_0.4.0.md` created; `publish.sh` uses it automatically for any `0.4.0*` version.

## 8. Remaining gaps (honest list)

1. No execution on Windows, macOS, AU or in any DAW (F12). The Windows installer and macOS ad-hoc signing steps are untested here.
2. Real-guitar fixtures, host matrix and signing/notarisation (specs 11-13) are not done (F14).
3. Context-menu opening is not automated (F9); a 0.83 s block in Hybrid mode behaves differently from normal sizes (F10); host meter under "Internal BPM only" (F11); malloc-level audit is glibc-only (F13).
4. OS reduced-motion preference is not read and onset scrolling is slowed rather than frozen (documented in COMPLIANCE).

## 9. Test totals observed at the end of this audit

Core suite: 53/53 ctest tests pass (JUCE-free; also 53/53 under ASan+UBSan). Plug-in harness: 2330 checks, 0 failures (it was 1991 at the start of the audit), including the baseline-hash regression step. pluginval strictness 10: pass.
