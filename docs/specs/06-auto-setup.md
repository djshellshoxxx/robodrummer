# 06 - Auto Setup ("Learn My Playing") and optional First-Run Tour

Status: Implemented (core+processor wiring); UI pending

## 1. Purpose and user value
One button. The guitarist plays naturally for about 10 seconds and the plug-in proposes sensible values for input gain, noise gate, follow behaviour, timing mode, base BPM, Jam Style and intensity. Nothing changes until the player presses Apply, Apply can be undone, and every proposal comes with a plain-language reason ("Gate set to -52 dB because noise floor measured -63 dB"). Goal: a new user gets a usable setup without understanding gate thresholds or inertia.

## 2. Behavior (precise)
Class `robodrummer::AutoSetup` (`src/analysis/AutoSetup.h`, header-only, JUCE-free). It consumes the RAW guitar signal (mono sum, before `InputConditioner`, so proposed gain is absolute) and runs the same 10 ms / 1 dB RMS histogram method as `CalibrationAnalyzer` (levels, noise floor), `AdaptiveOnsetDetector` + `TempoTracker` (onsets, tempo) and `PerformanceAnalyzer` (band shares, strength, intensity). These are the same feature extractors the live path uses.

### 2.1 State machine
`Idle -> Listening -> Analyzing -> Done` plus terminal `Cancelled`. `Listening` has prompts driven by elapsed time and activity:
| Prompt | When |
|---|---|
| `GetReady` | first 1.0 s (noise-floor window; "stay quiet for a second, then play") |
| `PlayNaturally` | after 1.0 s while enough activity is heard |
| `KeepPlaying` | activity is low/sparse, or the nominal duration passed without enough material |
| `PlayLouder` | after 3 s, peak below -45 dBFS and almost nothing detected |

Timing (seconds of audio fed, not wall clock): nominal duration `D` = 10 s (configurable 8-12). At `t >= D` the run ends if it holds at least 4.0 s of active playing and 12 onsets. Otherwise it is extended up to the hard cap `1.6*D` (timeout). Early out: at `t >= 5 s` with under 0.3 s of activity and under 2 onsets the run ends as "not enough signal". Cancel is honoured at the next block. `progress()` rises 0..0.99 while running and is 1 when Done.

A window is "active" when its 10 ms RMS is more than 12 dB above the measured lead noise floor and above -70 dBFS.

### 2.2 Measurements
* Lead noise floor: median 10 ms RMS (dBFS) over the first 1 s. Final noise floor = min(lead median, p10 of the whole run) so a player who starts immediately is still measured from gaps.
* Levels (same histogram method as `CalibrationAnalyzer`): typical, loud (p95) RMS, peak. Clipping = peak >= 0.98 or 3+ samples at >= 0.99.
* Rhythm: onset times kept (cap 256). Tracked tempo sampled every 250 ms once 8 onsets exist; median of samples with confidence >= 0.25 (all samples if none). The median is folded by octaves into 60..170 BPM. Candidate beat tempi are the folded value, x2, x1/2, x2/3 and x3/2 (so swung 8ths at 90 are not mistaken for a dotted grid at 135). Grid fit: for subdivisions {1,2,3,4} of the folded beat, onsets are assigned to the nearest grid index and a least-squares line `t = a + b*n` gives refined period and residuals. Normalised residual = RMS residual / grid step; random onsets give 0.289, so `regularity = 1 - normalised/0.289` (clamped 0..1). The best (candidate, subdivision) wins on `normalised + 0.002*(sub-1) + 0.02*|log2(bpm/105)|` (tiny penalties that only settle genuine ties). `timingSdMs` is the residual RMS in ms for that grid. If regularity >= 0.6 the refined line BPM replaces the median.
* Looseness `L = clamp(0.6*clamp((timingSdMs-12)/28,0,1) + 0.4*(1-regularity), 0, 1)`.
* Swing ratio: over consecutive onset pairs whose two intervals sum to one beat (+/-12%), mean of longer/(sum). 0.5 straight, 0.67 triplet swing; needs 4 pairs else treated as unknown.
* Style features: onset density (onsets/s over the run), low-band share (low/(low+mid+high), averaged over active blocks), transient strength (mean onset strength), swing, base BPM.

### 2.3 Proposals (each field has `apply`, `value`)
| Field | Rule |
|---|---|
| `inputGainDb` | `clamp(-9 - peakDb, -12, 24)`; if clipping, `min(that, -6)` and an explicit "lower the input" advice line. Level validity needs >= 1.5 s active audio. |
| `autoGain` | on only when the manual gain needed would exceed +24 dB (very quiet source), else off. |
| `gateThresholdDb` | in the post-gain domain: `noisePost = noiseFloorDb + gain`; if `noisePost < -72` gate off (-90); else `clamp(noisePost + 12, -80, min(-20, max(-80, min(-30, loudPost - 20))))` (+12 dB because the conditioner's envelope follower reads noise peaks about 8 dB above RMS), floor -80, max -20. |
| `followStrength` | `1 - 0.45*L` |
| `tempoInertia` | `0.35 + 0.50*L` |
| `confidenceSensitivity` | `0.60 - 0.35*L`, minus 0.10 when tempo confidence < 0.4 (demand stronger evidence) |
| `timingMode` | `Hybrid` with `leadership = 0.70` when `L < 0.35`, tracker tempo confidence >= 0.4 and regularity >= 0.5; otherwise `DrummerLeads` (leadership not proposed). |
| `bpm` | folded median/refined BPM, clamped 40..240. |
| `jamStyle` | transparent score table over 18 styles (below); falls back to Rock below score 0.45. |
| `intensity` | `0.25 + 0.60*meanActiveIntensity`, clamped 0.25..0.85. |

Style score = 0.40 tempo fit + 0.25 density fit + 0.15 low-band fit + 0.10 strength fit + 0.10 swing fit. Tempo fit is 1 inside the style's soft region (`Style::tempoMinBpm..tempoMaxBpm`, via `styleFor`) and falls linearly to 0 over 25 BPM outside. Other fits are `max(0, 1-|x-target|/scale)` against a small constexpr table in the header (scales: density 3/s, low share 0.3, strength 0.4, swing 1). Unknown swing scores 0.5. Experimental, Breakbeat, HalfTime and ElectronicRock are never auto-suggested. Ties keep table order, so results are deterministic.

### 2.4 Result and degradation
`AutoSetupResult { Status status; bool levelsValid, rhythmValid; AutoSetupProposal proposal; explanations[16] }`.
Status: `Ok`, `NotEnoughSignal` (silence/NaN/too quiet; no proposals at all), `TimedOut` (hard cap reached with some signal but neither level nor rhythm evidence), `Cancelled`. With levels but too few onsets, status is `Ok`, `rhythmValid=false`, and only level fields are proposed (an explanation says why). Every explanation is a fixed 112-char buffer; lines are in the order gain, gate, timing, follow, bpm, style, intensity, then warnings.

### 2.5 Apply, snapshot and Undo
`AutoSetupSnapshot` holds the 11 current values (same units as the host parameters). Apply flow (message thread, one gesture/undo group):
1. UI shows a review card listing every proposed field with its explanation, current value, new value and a per-field checkbox bound to `apply` (default on for fields with a proposal; disabled for fields with none).
2. Apply: `AutoSetupUndo::capture(current, proposal)` stores the before-values and a mask of applied fields; then the integrator writes `applyProposal(proposal, current)` to the parameters.
3. Undo: `AutoSetupUndo::restore()` returns a snapshot plus mask; only masked fields are written back, so later manual edits to other parameters survive. Undo stays available until the next Auto Setup run, preset load or state restore (integrator clears it), and is also reachable from the card's toast.
4. Cancel/close discards the proposal; nothing was changed.

## 3. Parameters
No new automatable parameters. Proposals map onto existing IDs: `inputGain`, `autoGain`, `gateThreshold`, `followStrength`, `tempoInertia`, `confidenceSensitivity`, `leadershipMode`, `leadership`, `bpm`, `jamStyle`, `intensity`. Optional non-automated UI preference (editor state, not a host parameter): `autoSetupTourSeen` (bool, default false), see section 10.

## 4. Actions / MIDI mapping
Action IDs for the existing command/MIDI-learn system (momentary, not automatable): `autoSetupStart`, `autoSetupCancel`, `autoSetupApply`, `autoSetupUndo`. No default MIDI mapping.

## 5. State serialization
Proposals, snapshot and run data are transient and NOT saved in presets or plug-in state (a preset load must never be altered by a stale proposal). Only the applied parameter values persist through the normal parameter state. UI state (tour seen) is a single bool in the editor-state block, version tolerant (missing = false).

## 6. UI placement, labels, tooltips
* Button "Learn My Playing" in the Input tab header (also reachable from the Help menu and the first-run tour). Tooltip: "Play naturally for about 10 seconds and RoboDrummer suggests settings. Nothing changes until you press Apply."
* Running: a card with a progress bar (numeric "6 s of 10 s"), the current prompt as TEXT (not colour-only), a live input level meter and a Cancel button. Escape cancels. Prompts are exposed as accessibility text.
* Review card: title "Suggested setup"; one row per field: checkbox, parameter label with units, current -> proposed value, one-line reason. Buttons "Apply" (default focus) and "Cancel". After applying: "Undo Auto Setup" button and toast. A failed result shows the reason (e.g. "I could not hear enough playing. Check your input and try again") and "Try again"; no Apply.
* All controls keyboard operable with visible focus; the card blocks a second run while open.

## 7. Threading and real-time
* `process()` runs on the audio thread: fixed storage, no allocation, no locks, no I/O, no string formatting, bounded work per block, NaN/Inf sanitised (counted, treated as 0), outputs finite.
* `start()` / `cancel()` are message-thread calls that only set atomics; the audio thread performs the actual reset at the top of the next `process()`.
* When the input phase ends `process()` sets state `Analyzing` (release store) and stops touching data. The message thread (UI timer) sees `Analyzing` and calls `finalize()` (acquire) which does the math and formats explanations with snprintf, then state becomes `Done`. `result()` is valid only after `Done`.
* Cost: three light extractors, comparable to the live analysis path, and only while a run is active.

## 8. Test plan (`tests/test_auto_setup.cpp`)
Synthetic fixtures (deterministic LCG noise, pluck = decaying noisy sine): quiet noise + steady 8ths at 100 BPM -> BPM within 2, Hybrid, gate above the noise floor; irregular timing -> Drummer Leads and higher inertia than the steady case; silent input -> `NotEnoughSignal`, no `apply` flags; clipping -> lower gain advice; cancel mid-run; timeout (sparse playing) -> `TimedOut`, no proposals; determinism (two runs identical); NaN/Inf input and all-NaN input; block-size independence; snapshot/apply/undo masks; style choice for fast low-heavy playing vs slow swung playing; explanation text contains the measured numbers.

## 9. Acceptance criteria
* Steady 100 BPM 8ths: BPM in 98..102, `timingMode == Hybrid`, leadership 0.70.
* Irregular playing: `DrummerLeads`, `tempoInertia` at least 0.2 higher and `followStrength` lower than the steady run.
* Silence: status `NotEnoughSignal`, zero fields with `apply`.
* Clipping fixture: `inputGainDb <= -6` with a warning line.
* Cancel/timeout/NaN never crash, never propose junk, all numbers finite and inside parameter ranges.
* Identical input gives identical results regardless of block size.
* Undo restores exactly the applied fields.

## 10. First-run tour (optional, skippable, restartable) - CDL 7.3
* Non-blocking overlay of 4 short coachmarks: (1) "Connect your guitar: input meter and Learn My Playing", (2) "Pick how the drummer follows you: Timing Mode", (3) "Choose a Jam Style and press Play", (4) "Need help? Help > Show Tour". Each step has Next, Back, and an always-visible "Skip tour". Escape skips. No step requires an action; the plug-in is fully usable while the tour is shown and the overlay does not capture host keyboard shortcuts beyond Escape/Tab inside the plug-in window.
* Shown once on first editor open when `autoSetupTourSeen == false`; set true on finish or skip. "Show Tour" in the Help menu restarts it at any time. The first coachmark offers "Run Learn My Playing now" as an optional shortcut, never automatically.
* Coachmark text is real text (accessible), contrast-compliant, no colour-only meaning, no animation required; reduced-motion safe.
* The tour never changes any parameter and cannot start Auto Setup by itself. Core operation stays discoverable without it.

## 11. CDL compliance notes
7.3 (skippable, non-blocking, restartable from Help); momentary actions show visible state (progress card); no colour-only state (text prompts); concise tooltips; keyboard focus/Escape; automation unaffected (no new parameters); no data collected; undo for every automatic change.

## 12. Integration hook points
1. Processor `prepareToPlay`: `autoSetup.prepare(sampleRate)`.
2. Processor `processBlock` (audio thread): after mono-sum of the raw input and BEFORE `InputConditioner::process`, call `autoSetup.process(monoRaw, n)` (cheap no-op when Idle). Never feed conditioned audio.
3. Editor button (message thread): `autoSetup.start(seconds)`; Cancel: `autoSetup.cancel()`.
4. Editor 30 Hz timer: read `state()`, `progress()`, `prompt()`, `inputPeakDb()`; when `state()==Analyzing` call `finalize()` once; when `Done` read `result()` and show the review card.
5. Apply: build `AutoSetupSnapshot current` from the parameters, `undo.capture(current, result.proposal)`, write `applyProposal(...)` values through the normal parameter setters, one change gesture per parameter.
6. Undo: `undo.restore()` -> write masked fields back. Clear undo on preset/state load.
7. Tour: editor-state flag `autoSetupTourSeen`; Help menu item "Show Tour".
