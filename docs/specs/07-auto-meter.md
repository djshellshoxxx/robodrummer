# 07 Automatic meter detection (SPEC 16 "later")

## Purpose & user value
Guitarists playing 3/4, 6/8, 5/4 or 7/8 should not have to set the meter by hand. A causal detector watches the
played accents and suggests a meter plus a confidence. It never overrides anything: manual meter and host meter
always win, and 4/4 stays the default whenever the evidence is weak.

## Behavior (precise)
Component: `analysis/MeterDetector.h` (`MeterDetector`, `AutoMeterGate`), JUCE-free, header-only, no allocation.

Input: onsets expressed in tracked pulses (`observeOnset(beatPos, strength)`; `beatPos` is a monotone position on the
tracker's beat grid, `strength` 0..1). The pulse is whatever the tracker follows (quarter for 4/4, 3/4, 5/4; dotted
quarter or eighth for 6/8; eighth for 7/8). Non-finite input is ignored; a backwards jump > 0.5 pulse resets.

1. Pulse accent: onsets within +-0.18 pulse of an integer pulse contribute `max(strength)` as that pulse's accent.
   A pulse with no onset has accent 0 (a rest is evidence of structure, not an error). A pulse is committed once the
   position passes pulse+0.25. The last 60 accents are kept.
2. Bar-length fit: for each candidate bar length L in 2..7 pulses, fold the newest `floor(n/L)*L` accents modulo L and
   compute the weighted variance explained by the slot (eta squared, independent of bar phase), then the adjusted
   value `1-(1-eta2)(N-1)/(N-L)`. Rest pulses (accent 0) have weight 0.1 so a dropped strum on a strong beat does not
   wreck the fit while real rests still carry structure. An L needs >= 2 bars to be valid.
3. Choice: best adjusted fit (L=4 gets a +0.05 prior). The winner is then reduced to the shortest divisor d of itself
   for which the extra structure of the longer fold is not significant (nested F test, F <= 6), so 2 beats 4 only when
   beat 3 is as strong as beat 1 and 3 beats 6 unless the second half-bar is genuinely distinct. A winner with fit
   < 0.35 falls back to 4/4.
4. Subdivision feel: off-beat onsets (phase 0.18..0.82) are classified to the nearest of duple {1/4,1/2,3/4} or
   triplet {1/3,2/3} centres (tolerance 0.07), strength weighted, decayed 0.97 per pulse. Ternary when
   triplet/(triplet+duple) > 0.65 with >= 2.5 evidence, back to duple below 0.35 (hysteresis).
5. Mapping L,feel to meter: L2 ternary = 6/8 (duple = 4/4), L3 = 3/4, L4 = 4/4 (ternary = shuffle/12-8 feel, still
   4/4), L5 = 5/4, L6 = 6/8, L7 = 7/8.
6. Confidence = adjusted fit x evidence ramp ((pulses-6)/14, clamped) x separation from the best fit of an unrelated L
   (not a multiple or divisor of the winner; gap/0.3 clamped), smoothed 0.35 per pulse. It is 0 until >= `minBars`
   (3) bars and >= 12 pulses. Below fit 0.35 the suggestion falls back to 4/4 with the computed (low) confidence.
7. Hysteresis: the published meter changes only after the same challenger wins `switchPulses` (8) consecutive
   evaluations; while challenged the confidence decays 0.9 per pulse. The default published meter is 4/4, confidence 0.

`AutoMeterGate` (decision helper for the integrator): `update(manualEnabled, hostAvailable, suggestion)`. Returns
`apply=false` and resets whenever manual meter is on or a host meter is available. Otherwise it applies a meter only
when `confidence >= threshold` (default 0.75) has been held for >= `holdBars` (2) bars of that meter (counted in
pulses via `Suggestion::evaluations`); once applied it releases when the suggestion changes or confidence drops under
`threshold-0.25`. `formatReadout()` renders `Auto meter: 3/4 (82%)` (or `Auto meter: --`).

## Parameters
Proposed (integrator adds): `autoMeterEnabled` (bool, default off, no smoothing; off = detector idle, behaviour
identical to today), `autoMeterThreshold` (float 0.50..0.95, default 0.75, unit fraction shown as %, read once per
block, no smoothing). Manual meter parameters are unchanged and take priority.

## Actions/MIDI mapping
`autoMeterEnabled` is MIDI-learnable like other toggles (no new MidiCommand).

## State serialization
`autoMeterEnabled`, `autoMeterThreshold` as ordinary parameters in the existing state tree (version unchanged;
missing = defaults). Detector state is never saved.

## UI placement & labels & tooltips
Timing area next to Manual Meter: toggle "Auto meter", slider "Auto-meter confidence" (%). Read-out text
"Auto meter: 3/4 (82%)" in the ANALYSIS tab and under the meter selector (text, never colour-only). Tooltip:
"Suggests a meter from your playing and applies it only when Manual Meter is off and the host gives no meter."
Keyboard focusable like the other controls.

## Threading/real-time
Allocation-free, about 6 x 60 multiply-adds per pulse. Call `observeOnset` from the thread that owns the tracker, in
beat order. The suggestion is a plain struct: publish to the UI via atomics.

## Test plan
`tests/test_meter_detector.cpp`: synthesized guitar-like onset streams for 4/4, 3/4, 5/4, 6/8 (ternary 2-pulse and
6-pulse), 7/8 (three groupings) x 100 seeds with timing jitter, amplitude noise and missing events; flat and noise
streams default to 4/4; no flapping over long runs; NaN/Inf/empty input; gate rules (manual, host, threshold, hold).

## Acceptance criteria
>= 85% correct per meter over 100 seeds (final suggestion), >= 95% 4/4 on ambiguous input, <= 1 meter change per
stable fixture, gate never applies with manual meter on, all outputs finite.

## CDL compliance notes
Visible text read-out, no colour-only state, feature off by default, deterministic, no I/O.

## Integration hook points
1. Processor prepare/reset: `detector.reset()`; also when manual meter is toggled or the transport restarts.
2. Per onset from `OnsetDetector` once the tracker has a beat grid (only when `autoMeterEnabled`):
   `detector.observeOnset(pulsePosition, onset.strength)`.
3. Once per block after step 2: `gate.update(manualMeterEnabled, hostMeterAvailable, detector.suggestion())`.
4. If `decision.apply`, use its meter in place of the Default result of `MeterSelection::resolve` (source == Default
   only); then continue as today (`rhythmAnalyzer_.setMeterNumerator`, `JamEngine::setMeter`).
5. UI timer: `AutoMeterGate::formatReadout`.
