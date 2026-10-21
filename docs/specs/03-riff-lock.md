# 03 Riff Lock

Status: Implemented (core, processor wiring and editor UI)

## Purpose & user value
The kick (and optionally a crash accent) mirrors the guitarist's low-note / palm-muted riff rhythm, so the drummer plays WITH the riff instead of beside it. It learns the riff continuously, only acts when the riff is clearly repeating, and does nothing on noodling.

## Behavior (precise)
Code: `src/analysis/RiffLock.h` (header-only, JUCE-free, namespace `robodrummer`).

**Grid.** `RiffGrid::forMeter(numerator, denominator, swing)`: slots = numerator * slotsPerBeat (max 64).
- Straight, denominator <= 4: 4 slots/beat (16ths). Denominator >= 8 (incl. 6/8, 7/8): 2 slots/beat.
- Triplet grid (3 slots/beat) when denominator <= 4 and style swing >= 0.20 (shuffle). Beat = the meter's denominator unit, as in `GrooveGenerator`.

**Learning (`RiffLock`, audio thread).** `addOnset(barPhase, strength, lowBand)` quantizes the onset to the nearest slot; presence weight = closeness(1 at slot centre, 0 at half a slot) * (0.25 + 0.75*low) * (0.7 + 0.3*strength). Onsets further than 0.4 slot from a slot are dropped. `endBar()` commits: histogram and bar-weight decay by 0.6 per bar (adapts in 2-4 bars), then add the finished bar. Per-slot consistency = decayed hits / decayed bars (0..1). Repetition = weighted Jaccard of this bar vs the previous bar OR the bar before that (2-bar riffs), smoothed (EMA 0.6). Silent bars decay repetition. Confidence = repetition * clamp(slotsWithConsistency>=0.5 / 2, 0, 1). Needs 2 completed bars with onsets before confidence can be non-zero, so kicks lock from the 3rd bar.

**Transform (`RiffLockTransform::apply`, pure, deterministic).** Inputs: generated bar (`std::array<DrumEvent,N>` + count), `GrooveContext`, `Style`, `RiffSnapshot`, `RiffLockParams`, seed. Passthrough (bytes identical, no change) when: amount <= 0 or non-finite, snapshot not `locked` (tracker unlocked), grid mismatch with ctx meter, confidence < gate, invalid ctx.
- Sensitivity s in 0..1: slot threshold = 0.85 - 0.45*s; confidence gate = 0.75 - 0.40*s.
- Effective strength e = amount * (0.35 + 0.65*smoothstep((conf-gate)/(1-gate))).
- (a) Add kicks on slots with consistency >= threshold: no existing kick there; not on a snare backbeat (meter backbeat beat, or an existing Snare event with velocity >= 0.5) unless style allows; not adjacent (+-1 slot) to a kick unless style allows; accepted with probability min(1, e*(0.3+0.9*consistency)) via a seeded hash of (seed, slot), so results are reproducible. Priority: consistency desc, on-beat first, lower slot first. Caps from style (see below).
- (b) Remove conflicting kicks: kick sitting on a slot with consistency < 0.2, only when >= 3 slots are locked, never slot 0 (beat 1), never on a meter kick-anchor beat; probability 0.85*e (hash).
- (c) Accent: existing/added kicks on locked slots get velocity + 0.10*e*slotStrength (clamped to 1). With `accentCrash`, a Crash is added on the strongest on-beat locked accent slot (not slot 0) if the bar has no crash there, probability e (hash).
- Snare events are never added, removed or moved. Event offsets for added hits use the generator formula `llround(beatPos*samplesPerBeat)` (pre-humanization grid), so the humanizer treats them like generated hits.
- Style limits (`RiffLockLimits::fromStyle`): maxKicks = clamp(round(beats*(1+3*extraKick)),2,16); maxAdded = clamp(ceil(beats*(0.5+1.5*extraKick)),1,12); allowBackbeatKick and allowAdjacent only when extraKickProbability >= 0.30 (metal-like double-kick styles). Added hits never exceed array capacity.
- Added kick velocity = 0.50 + 0.30*intensity + 0.15*slotStrength.

**Telemetry.** `RiffSnapshot`: `consistency[64]`, `strength[64]`, `slots`, `slotsPerBeat`, `triplet`, `confidence`, `repetition`, `locked`, `barsSeen`. Plain copyable struct for the ANALYSIS tab (16th grid bars + confidence meter).

## Parameters
| ID | Type | Range | Default | Unit | Smoothing |
|---|---|---|---|---|---|
| `rlAmount` | float | 0-100 | 0 | % | none (read per bar) |
| `rlSensitivity` | float | 0-100 | 50 | % | none |
| `rlAccentCrash` | bool (optional) | off/on | off | - | none |

Integrator divides by 100 before calling the API. Default 0 = off (bit-exact passthrough); integrator may raise the default after listening tests.

## Actions / MIDI mapping
None required. Optional later: toggle `rlAmount` 0 / last value.

## State serialization
Only the 3 parameters (version bump of parameter state, defaults on missing keys). Learned histogram is NOT saved (relearned in 2-3 bars).

## UI placement & labels & tooltips
GROOVE or JAM tab: knob "Riff Lock" (%) tooltip "How strongly the kick follows the guitar's repeating low-note rhythm. 0% = off."; knob "Riff Sensitivity" (%) tooltip "Higher locks on sooner and to looser riffs; lower needs a very consistent riff."; toggle "Riff crash accents". ANALYSIS tab: 16th-grid bar graph of `consistency`, text readout "Riff lock: 72% (locked)" / "Riff lock: searching". State is always shown in text, not colour only; controls keyboard focusable.

## Threading/real-time
All classes are fixed-size value types, no allocation/locks/IO. `RiffLock` and `apply` run on the audio thread only. `snapshot()` returns by value; UI gets a copy via the integrator's existing lock-free analysis mailbox. All inputs sanitized (NaN/Inf ignored or clamped); outputs finite.

## Test plan
`tests/test_riff_lock.cpp`: 8th palm-mute lock within 2 bars; syncopated pattern reproduced; random input no change; amount 0 identity; NaN amount identity; determinism; style caps; snare backbeat preserved; beat-1 kick never removed; triplet meter; 6/8 grid; unlocked tracker passthrough; silence decay / re-adaptation; NaN/huge input; capacity-full safety.

## Acceptance criteria
All tests pass; amount 0 output bit-identical; no snare event changes; added+existing kicks <= style cap; confidence < gate on random input in 200 seeded trials.

## CDL compliance notes
Labels with units, tooltips explain product effect, state shown as text, no tooltip-only critical info, parameters automatable and stable IDs.

## Integration hook points
1. `prepare`/meter change (message->audio via existing meter path): `riffLock.configure(num, den, style.swing)`.
2. Audio thread, per block after `AdaptiveOnsetDetector::processBlock`: for each onset compute `barPhase` from `MusicalClock` (bar position / bar length), call `riffLock.addOnset(barPhase, ev.strength, lowRatio)`; `lowRatio` from `LowBandMeter::processBlock(input, n)` (one-pole ~200 Hz) read at the block.
3. At each bar boundary, BEFORE generating the next bar: `riffLock.setTrackerLocked(tempoTrackerLocked)`, `riffLock.endBar()`.
4. After `GrooveGenerator::generateBar` (and fills/phrase modifiers), BEFORE the humanizer and scheduler push: `RiffLockTransform::apply(events, count, ctx, style, riffLock.snapshot(), params, barSeed)`; use returned `count`. Skip on fill bars.
5. UI timer: copy `snapshot()` for ANALYSIS tab.

6. Build: add `riff_lock` to the explicit test-name list in `CMakeLists.txt` (tests are NOT auto-globbed in this repo); the test is verified standalone (g++ -std=c++20 -UNDEBUG, also under ASan/UBSan).
