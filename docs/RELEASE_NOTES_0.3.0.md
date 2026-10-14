# RoboDrummer 0.3.0

Completes the build to SPEC.md (Phases 1–4) and adds a Windows installer.

## New
* **22 styles** with seven groove families (backbeat, four-on-the-floor, reggae one-drop, half-time, breakbeat, experimental, jazz swing) and soft tempo regions.
* **Humanization** (style- and tempo-aware micro-timing, limb bias, velocity), **push/lay-back feel**, **drummer personalities** (Balanced, Tight, Laid-back, Pushing, Aggressive, Busy, Minimal).
* **Fills**: frequency, 1 beat / 2 beats / 1 bar / 2 bars, complexity, surprise; queued fill and *immediate* fill.
* **Tracking**: phase-locked beat loop (follows gradual accelerando), fast re-lock after a sudden tempo change, movement classification (steady/jitter/drift/accelerating/decelerating/abrupt/half-double), rich tempo hypotheses, lock hysteresis, multi-feature onset detection with spectral flux, richer performance features and intensity model.
* **Input**: mono/stereo-sum/left/right, DC removal, high-pass, gain, auto-gain, noise gate, optional calibration.
* **Timing controls**: Follow Strength, Tempo Inertia, Beat Correction Speed, Confidence Sensitivity; tap tempo; MIDI clock; internal-BPM-only mode.
* **Sound**: user WAV/AIFF kits (velocity layers, round robin), four built-in kits, kit tuning, mixer (Kick, Snare, Toms, Hi-Hat, Ride, Cymbals, Room, Master) with room reverb, four optional aux output buses.
* **MIDI**: MIDI Learn for notes and controllers, expression-pedal defaults (CC11 Intensity, CC12 Leadership), editable drum map and output channel.
* **Song mode**: per-section name, length, style, intensity, half/double time, transition (crash/fill/break/none), personality, fill frequency, leadership and follow strength.
* **Presets**: Jam, Drummer, Style, Kit, Song and Analysis presets with factory content.
* **UI**: eight tabs (LIVE, KIT, DRUMMER, STYLE, SONG, MIDI, ANALYSIS, SETTINGS), resizable, CDL palette.
* **Windows**: Inno Setup installer and zip produced by CI; AU target on macOS.

## Fixed
* Humanization declared by the styles was never applied.
* Host/MIDI tempo changes were smeared by the guitar-follow slew limit.
* The tracker could not follow a gradual accelerando and never re-locked after a sudden tempo change.
* Tracker lock had no hysteresis; tempo range was 45–220 instead of 40–240 BPM.
* NaN/Inf input samples reached the monitor path and the analysis.

## Compatibility
* `jamStyle` now has 22 choices (indices 0–5 unchanged). Saved sessions restore correctly; automation lanes of that parameter recorded with 0.2.x map to different styles.
* State version 3; versions 1–2 still load. All new parameters are appended.

## Known limitations
See [QA.md](QA.md).
