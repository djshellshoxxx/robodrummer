# 08 Virtual limb model (SPEC 22 "future")

Status: Implemented (core, processor wiring and editor UI)

## Purpose & user value
Generated grooves and fills should be physically playable by one four-limb drummer. The limb model post-processes a
bar of `DrumEvent`s, assigns every hit to RH/LH/RF/LF, and removes only what a real drummer could not play (too fast
for one limb, too far for a hand to travel, more than four limbs at once), always sparing the hits that define the
groove. An amount control blends from untouched to fully realistic.

## Behavior (precise)
Component: `core/LimbModel.h` (`LimbModel`, `LimbSettings`, `LimbReport`), JUCE-free, header-only, fixed storage.
`size_t LimbModel::process(DrumEvent* events, size_t count, const LimbContext&, float amount, LimbReport* report)`
rewrites `events` in place (kept events only, original order and `sequence` preserved, nothing added or moved) and
returns the new count. `amount` is 0..1 (NaN = 0); `amount <= 0` returns the input bit-exact.

Instrument-to-limb rules (lower cost wins; secondary limb +3..4 as cross-over cost; ties go to lower travel, then
limb order RH, LH, RF, LF): Kick RF (LF as second pedal only if `doublePedal`); Snare/Rimshot/Sidestick/Clap LH (RH
secondary); toms either hand (floor tom RH preferred); ClosedHat RH, LH secondary, or LF foot-chick when velocity <=
`footHatMaxVelocity`; OpenHat/Ride/Crash/China/Splash/Cowbell/Tambourine RH (LH secondary). The second hand may join a hat/cymbal
line while the first is busy on another instrument, but never alternates with it on the same instrument to beat
the single-stroke limit (snare and toms may, as in a roll).

Constraints (all scaled by `amount`; amount 1 = full):
* Stroke limit: consecutive hits on one limb are >= `handMinIntervalMs` (default 85, 16ths up to ~176 BPM) or
  `footMinIntervalMs` (default 105, 16ths up to ~143 BPM; the double-kick limit). Simultaneous hits therefore need
  distinct limbs, so more than four simultaneous hits are impossible by construction.
* Travel: a hand moving between different instruments needs `travelBaseMs + travelPerZoneMs * distance` (kit zones:
  hat -3, snare -1, hi tom -0.3, mid tom 0.5, floor 1.4, ride 2, crash 2.4, china 2.8, splash/cowbell -2.5); a foot
  switching role needs `footSwitchMs`. The limit is max(stroke, travel).

Processing is chronological (ties: higher priority first). An event takes the cheapest feasible limb. If none is
feasible, the lowest-priority conflicting event is removed: a blocker is evicted if it has lower priority, is not
protected and the newcomer then fits; otherwise the newcomer is removed. Priority = instrument base (snare 70, other
backbeat voices 65, kick 60, crash 55, toms/china 50, ride 45, open hat 40, hat 35, ...) + 10 on a beat / +4 on an
eighth + velocity/2 (snare below velocity 0.4 is a ghost, base 20). Protected (never evicted or dropped unless blocked
only by other protected hits): kick on the bar downbeat, backbeat snare (MeterProfile backbeats, velocity >= 0.5) and
crash on the downbeat (window = 1/8 beat clamped to 5..40 ms).
Ghosting/retiming of kept hits is deferred; the model only keeps or removes. Optional `carry`: remembers each limb's
last hit across strictly consecutive bars (`resetCarry()` clears it).
Report (for the ANALYSIS tab): counts, `limb[i]` per kept event, up to 128 removed events with reason (Rate, Travel,
NoLimb, Evicted), number played by a non-default limb, protected kept/dropped. `countViolations()` re-checks an output
against the settings independently of the solver.

## Parameters
Proposed: `limbModelAmount` (float 0..100, default 0 = off and bit-exact, unit %, 1% step, no smoothing, read per
bar). Settings struct fields stay internal constants for now (no stable IDs).

## Actions/MIDI mapping
`limbModelAmount` is MIDI-learnable like other floats. No new MidiCommand.

## State serialization
`limbModelAmount` only, as a normal parameter (missing = 0, behaviour unchanged).

## UI placement & labels & tooltips
Groove/Humanize panel: slider "Playability" (0-100 %). Tooltip: "Removes hits a single drummer could not physically
play. 0 % leaves the groove untouched." ANALYSIS tab text line: "Limbs: 3 hits removed (2 rate, 1 travel)".

## Threading/real-time
Audio-thread safe: no allocation/locks/I/O, fixed arrays (<= 512 events; larger input is left untouched and flagged
in the report), insertion sort over <= 512 indices, deterministic (no RNG, no global state except opt-in carry).

## Test plan
`tests/test_limb_model.cpp`: passthrough at 0 and NaN, five-plus simultaneous hits reduced to <= 4 limbs, max-rate caps
vs tempo (hat/snare/kick streams), priority preserved (downbeat kick/crash, backbeat snare), determinism, partial
amount, carry, report consistency, 22 styles x tempos 40-240 x seeds, with fills and heavy humanize: zero violations
after processing and output is a subset of input.

## Acceptance criteria
Zero violations on all generated bars at amount 1; protected hits kept in every groove-only bar; amount 0 bit-exact;
identical output for identical input; no event added or altered.

## CDL compliance notes
Deterministic, transparent (report lists every removal), off by default, no colour-only state.

## Integration hook points
In `JamEngine`, after a bar's groove events are generated and the planned fill merged (before slicing into audio
blocks, bar-relative sample offsets): `limbs_.process(barEvents, n, {bpm, sampleRate, numerator, denominator}, amount, &report_)`.
Call on the thread that generates the bar; copy a report summary to atomics for the ANALYSIS tab. Call `resetCarry()`
on transport reposition or tempo jump; keep `carry` false unless bars are strictly consecutive.
