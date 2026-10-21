# 16. ADR: input transient enhancement (SPEC 5 "may")

Status: **Decided, skip for now (not implemented).** Revisit when fixture evidence shows missed onsets that enhancement would fix.

## Context

SPEC 5 lists "transient enhancement" among the optional stages of the analysis path (DC removal, high-pass, optional gate, gain normalisation, transient enhancement, multi-band feature extraction). The word is "may". Implemented today (`src/analysis/InputConditioner.h`): input mode, DC removal, high-pass, gain, auto-gain, gate. `AdaptiveOnsetDetector` (`src/analysis/OnsetDetector.h`) already combines broadband and band-split (about 250 Hz and 2.5 kHz) spectral flux with an adaptive threshold, a 45 ms refractory period and an onset probability. The feature audit marks enhancement "Missing, optional; flux feature already covers the use case".

Not to be confused with `02-transient-shaper.md`: that is an audible effect on the **drum output** bus. This ADR is about a non-audible pre-processing stage on the **guitar analysis copy**. The two share no code and no parameters; the audible guitar is never altered (SPEC 5).

## Decision

Do not implement an analysis-path transient enhancer in 0.4.x.

Reasoning:

1. **No evidence of need.** All onset and tracking tests pass on synthetic fixtures; there are no real-guitar results yet (`11-real-guitar-fixtures.md`). Adding a stage without a failing measurement is speculative tuning.
2. **Spectral flux is already differential.** Flux responds to rises in spectral energy, which is the quantity a transient enhancer amplifies. Pre-emphasis would mostly rescale input to a detector that adapts its threshold (about 10 s accent reference, `noveltyPeakDecay_`).
3. **Risk of false positives.** Enhancement boosts noise, pick scrape, fret buzz and amp hiss as much as true attacks; with the gate and calibration already controlling noise, an additional stage multiplies the tuning surface. Distorted, compressed guitar has little transient contrast to enhance anyway.
4. **Determinism and cost.** Each stage changes every downstream fixture result; keeping the chain minimal keeps regressions attributable.
5. **Cheaper levers exist.** Input gain, auto-gain, noise gate and calibration (SETTINGS) address weak input; a "sensitivity" control would be a smaller, more understandable lever than a shaper.

## Consequences

* SPEC 5 keeps "transient enhancement" as optional; mark it "evaluated, deferred (ADR 16)" on the next SPEC revision.
* `docs/COMPLIANCE.md` and `docs/QA.md` unchanged.
* The decision is cheap to reverse: the design below is self-contained and would add one parameter.

## Revisit when (any one, measured on the real-guitar corpus)

1. Onset recall (within 30 ms of annotated beats-subdivisions, using `beats_s` plus onset annotations) is below **85 %** on clean arpeggio/sustained clips (C10, C11, C14) at the default settings **and** raising input gain/lowering the gate does not fix it.
2. Tempo error or lock time thresholds (< 2 BPM, < 6 s, >= 90 % of clips) fail on a category where the cause is traced to weak attacks (soft picking, volume swells, legato).
3. A prototype enhancer improves the failing category by >= 20 % relative tempo-error reduction without increasing false locks on C18 (rubato) or C24 (noise) and without worsening any passing clip by more than 0.5 BPM.

## Design if reversed (ready to implement)

* **Where:** inside `InputConditioner`, after DC removal/high-pass and gain, before the gate decision (gate uses the original level), producing only the analysis copy.
* **Algorithm:** dual-envelope differential shaper. Fast envelope (attack 1 ms, release 30 ms) and slow envelope (attack 20 ms, release 200 ms) of the rectified signal; ratio `r = (fast + eps) / (slow + eps)`; gain `g = clamp(r^k, 1/gMax, gMax)` with `k = amount * 0.5`, `gMax = 4` (+12 dB), `eps = 1e-5`; gain smoothed with a 0.5 ms one-pole. Output `x * g`. Stateless apart from two envelope followers and the smoother; NaN-safe by sanitising input and clamping `amount`.
* **Parameter:** `inputTransientBoost`, float, 0.0 to 1.0, default 0.0 (off, bit-identical to today), unit none (shown as %), smoothing 20 ms. Appended at the end of the parameter list; state version unchanged if it is additive with default (state version hint +1 for the record).
* **UI:** SETTINGS tab, input group: slider "Transient emphasis" with tooltip "Makes picked attacks stand out in the signal RoboDrummer listens to. It does not change the sound of your guitar. Raise for soft or sustained playing; keep low for noisy or distorted tone." Shown as a percentage; ANALYSIS shows the effect through the existing onset marks.
* **Tests:** unit (steady sine passes at unity gain when `amount` 0; step onsets get a gain > 1 at `amount` 1; bounded gain; NaN/Inf, tiny/huge blocks); fixture comparison table (with/without) on synthetic and real fixtures; allocation audit unchanged; confirm the monitor path is untouched (guitar passthrough bit-identical).

## Parameters

None now. If reversed: `inputTransientBoost` as above.

## Actions/MIDI mapping

None now. If reversed: could be added to `kLearnTargets`; not required.

## State serialization

None now.

## UI placement & labels & tooltips

None now.

## Threading/real-time

If reversed: audio thread, fixed state, zero allocation, cost estimated at < 0.1 % of real time.

## Test plan

Decision only; the revisit criteria are evaluated by the fixture runner in `11-real-guitar-fixtures.md`.

## Acceptance criteria

The decision is recorded and linked from the SPEC index (`SPEC.md` section 42). Revisit is triggered by a documented measurement, not opinion.

## CDL compliance notes

No UI or behaviour change. If reversed, the new control follows CDL 2.1 (label, units, tooltip) and 2.4 (tooltip explains product effect).

## Integration hook points

None now. If reversed: `InputConditioner::process` (analysis copy only), `createParameterLayout`, SETTINGS panel, `docs/PARAMETERS.md`.
