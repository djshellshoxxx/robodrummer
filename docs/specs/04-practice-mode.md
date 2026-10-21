# 04 - Practice Mode

Status: spec for `src/core/PracticeMode.h` (JUCE-free, deterministic). Integration is a separate pass.

## 1. Purpose & user value
Lets a guitarist rehearse with RoboDrummer: a count-in before the drums start, a bar loop (current groove or a programmed
section range) to repeat a hard passage, and a tempo trainer that ramps BPM step by step toward a target.

## 2. Behavior (precise)

### 2.1 Driving model
`PracticeMode` is a pure state machine. The integrator calls `onBeat(PracticeBeat)` once per meter beat on the audio thread,
at the block where the beat falls, and applies the returned `PracticeAction`.
`PracticeBeat { long long barIndex; int beatInBar (0-based); int numerator; float trackerConfidence; }`.
`barIndex` is the monotonic TIMELINE bar (never rewinds on loop). A bar boundary is `beatInBar == 0`. "Beat" is the meter
beat (a quarter in x/4, an eighth in 7/8), so bar math is `bars * numerator` beats in every meter.
Output `contentBar = barIndex - offset` is the bar whose groove/section content should sound now. Loop wraps and rest bars
change `offset`; the integrator rewinds the groove cursor by `contentBarDelta` bars when `contentJump` is set
(`JamEngine::nudgePhaseSamples(delta * barSamples)`) and, for a section sequencer, calls `restorePosition`.

### 2.2 Session
`start()` begins a session (resets trainer, loop passes, offset) and schedules a count-in. `resume()` keeps trainer/loop
state and schedules a count-in. `stop()` ends the session: every later `onBeat` is passthrough (`active=false`).
`reset()` clears runtime state (settings kept). While not active, all actions are neutral.

### 2.3 Count-in
Length: Off, 1 beat, 2 beats, 4 beats, 1 bar (=numerator beats). Sound: Click or Sticks (integrator maps to
Cowbell/Sidestick or the sample set). On the first beat after `start()/resume()` the plan is made so the count-in ENDS
exactly on a bar boundary: total pre-roll `T` = beats to next boundary (a full bar if already on beat 1), grown by whole bars
until `T >= L`; first `T-L` beats are silent ("waiting"), then `L` click beats. The groove is muted (`muteGroove`) during
wait and count-in, the trainer's start BPM is already in force, and nothing else (loop/trainer counters) runs. First click
and every downbeat is accented. `countInOnlyOnStart=false` additionally plays an overlay lead-in (clicks, groove NOT
muted) on the last `min(L, numerator)` beats of the loop's final bar before each wrap.

### 2.4 Loop
Modes: `Bars` (loop the next N = 1..64 bars, UI offers 1/2/4/8) and `Range` (explicit content-bar start + length, e.g. from
`sectionLoopRange`). `armLoop()` arms; at the next bar boundary B the loop starts (Bars: start = contentBar(B), `loopStarted`
set so the integrator can snapshot the sequencer position). At any boundary where `contentBar >= start + N` the action is
`loopWrapped`, `loopBackToBar = start`, `contentJump`, pass++. Range mode plays through bars before the range. `disarmLoop()`
cancels at once (the groove continues forward with offset kept, so the content never jumps). Wrap happens only on bar
boundaries, so it is exact in any meter (3/4: 3 beats per bar, 7/8: 7).

### 2.5 Tempo trainer
Settings: startBpm, targetBpm, stepBpm (>0), every (1..64) with unit Bars or LoopPasses (passes fall back to bars when no
loop is active), direction Up / Down / PingPong, gate, restBar, pingPongCycles (0 = endless), stopAtTarget.
Ladder: Up begins at min(start,target) and ends at max; Down the reverse; PingPong begins at start, goes to target, holds
`every` units, returns to start, repeats. Rungs are `begin +/- k*step` computed by integer index (no FP drift); the last rung
is clamped to exactly the end value. Up/Down finish on reaching the end (`trainerFinished`, tempo holds at end, `stopRequested`
if stopAtTarget). A step is due when `every` bars (or passes) completed since the last step. Gate: for steps that RAISE BPM,
the mean `trackerConfidence` over the window since the last step must be >= `gateMinConfidence` (default 0.6), otherwise
`gateHeld`, tempo stays, the window restarts. Unknown/NaN confidence counts as 0. Rest bar: after a step the next timeline
bar is muted (`restBar`, `muteGroove`) and does not advance content or counters (speed drill).

### 2.6 Composition with timing authority
The trainer only ever produces a BASE tempo (the `baseBpm` fed to `TimingAuthorityController::update`). Rule, evaluated on
every beat from `setAuthority(LeadershipMode, leadership)` and `setExternalTempoSource(bool)`:
- DrummerLeads, internal tempo: DRIVE. `bpmOverrideValid`, integrator sets the base BPM (replaces the manual BPM for the session).
- Hybrid with leadership <= 0.5: DRIVE (guitar may still push inside followRange around the trainer base).
- Hybrid > 0.5, GuitaristLeads, or host/MIDI-clock tempo (`externalTempoSource`): HINT. No override; `tempoHintBpm` holds the
  rung for UI ("aim for N BPM"). The ladder still advances (and the gate still applies), count-in and loop still work.
Output BPM is clamped to 20..400 (same as MusicalClock).

## 3. Parameters (proposed, stable IDs)
| ID | Type | Range | Default |
|---|---|---|---|
| practiceEnabled | bool | - | off |
| pracCountInLen | choice Off/1 beat/2 beats/4 beats/1 bar | 0..4 | Off |
| pracCountInSound | choice Click/Sticks | 0..1 | Click |
| pracCountInStartOnly | bool | - | on |
| pracLoopOn | bool (arms) | - | off |
| pracLoopMode | choice Bars/Range | 0..1 | Bars |
| pracLoopBars | choice 1/2/4/8 | 0..3 | 4 |
| pracLoopSecFirst / pracLoopSecLast | int | 1..16 | 1 / 1 |
| pracTrainOn | bool | - | off |
| pracTrainStart / pracTrainTarget | float BPM | 20..400 | 80 / 120 |
| pracTrainStep | float BPM | 1..40 | 5 |
| pracTrainEvery | int | 1..64 | 4 |
| pracTrainUnit | choice Bars/Loop passes | 0..1 | Bars |
| pracTrainDir | choice Up/Down/Ping-pong | 0..2 | Up |
| pracTrainGate | bool | - | off |
| pracTrainGateMin | float | 0.2..0.95 | 0.6 |
| pracTrainRest | bool | - | off |
| pracTrainStopEnd | bool | - | off |

No smoothing: all take effect at the next bar boundary (BPM changes apply at the boundary, not ramped within a bar).

## 4. Actions / MIDI mapping
Proposed additions to `MidiCommand` (NOT edited here): `PracticeToggle` (start/stop session), `LoopToggle` (arm/disarm at next
bar), `TrainerToggle`, `TrainerRestart`, `CountInToggle`. `MidiCommandMapper` routes them to `start()/stop()`, `toggleLoop()`,
trainer enable flip, `restartTrainer()`, count-in on/off. Host transport start/resume should call `start()/resume()`.

## 5. State serialization
Settings only (the parameters above) under a `practice` child, `version=1`. Runtime state (position, rung, passes) is not
saved; sessions always begin with `start()`.

## 6. UI placement & labels
New "Practice" panel (or sub-section of the Jam tab). Labels: "Count-in" (Off, 1 beat, 2 beats, 4 beats, 1 bar), "Count-in
sound", "Only on start/resume", "Loop", "Loop length (bars)", "Section range", "Tempo trainer", "Start BPM", "Target BPM",
"Step (BPM)", "Every", "Direction", "Only speed up when I'm in time", "Rest bar after each step". Tooltips state units. State is
shown as text ("Count-in 3/4", "Loop pass 3", "Trainer 95 BPM", "Trainer: hint only, guitarist leads"), never color alone. All
controls keyboard focusable per the CDL baseline.

## 7. Threading / real-time
`onBeat` is allocation-free, lock-free, noexcept, O(1). Setters are called on the audio thread from the processor's parameter
snapshot at block start. NaN/Inf inputs are replaced by defaults or clamped; all outputs are finite.

## 8. Test plan
`tests/test_practice_mode.cpp`: exact ladders (up/down/ping-pong, non-multiple step, cycles, finish/hold), every-N bars,
loop-pass unit, gate hold/pass/down-ungated, rest bar, loop wrap math for 4/4, 3/4, 7/8, range loop, disarm, count-in beat counts
and bar alignment (all lengths, mid-bar start, 3/4), lead-in overlay, authority rule, seek, stop/reset/resume, NaN setters.

## 9. Acceptance criteria
All tests pass with -UNDEBUG; header JUCE-free; no allocation; trainer never overrides BPM in guitar-following modes.

## 10. CDL compliance notes
Deterministic, no hidden state across sessions, only settings persisted, host tempo respected (hint-only), documented limits.

## 11. Integration hook points (audio thread, per block)
1. Block start: push the parameter snapshot via `setCountIn/setLoop/setTrainer/setAuthority/setExternalTempoSource`;
   edge-detect `practiceEnabled`/transport start -> `start()`, transport stop -> `stop()`, resume -> `resume()`; loop param edge
   -> `armLoop()/disarmLoop()`.
2. For each meter beat inside the block: `auto a = practice.onBeat(beat)`.
3. If `a.bpmOverrideValid`: use `a.bpmOverride` as `baseBpm` for `TimingAuthorityController::update` and the engine base tempo.
4. If `a.contentJump`: nudge the groove cursor by `a.contentBarDelta * barSamples`; section sequencer `restorePosition` to the
   snapshot taken at `loopStarted` (or the `sectionLoopRange` start).
5. If `a.muteGroove`: drop engine events for that beat; if `a.click.active`: emit Cowbell (Click) or Sidestick (Sticks),
   higher velocity when `accent`.
6. If `a.stopRequested`: issue `MidiCommand::Stop`.
Events belong at the beat's sample offset; act on the action at that offset.
