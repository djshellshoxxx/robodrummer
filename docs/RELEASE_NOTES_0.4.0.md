# RoboDrummer 0.4.0

Adds practice tools, drum-bus processing, riff following, automatic setup and a full editor for them. Everything new is off or neutral by default: a session saved with 0.3.x sounds the same after loading (checked against a recorded baseline in the test harness).

## New
* **Drum bus**: compressor (threshold, ratio, knee, attack, release, make-up, peak/RMS detection, sidechain high-pass) with parallel mix and drive, on the main drum mix. The room return and the four aux stems stay unprocessed.
* **Transient shaper**: attack and sustain control on the drum mix, level independent, with mix and output gain.
* **Riff Lock**: learns a repeating low-note or palm-muted riff and makes the kick follow its rhythm (optional crash accents). It does nothing on free-form playing.
* **Playability** (virtual limbs): removes hits a four-limb drummer could not physically play, keeping the hits that define the groove.
* **Practice mode** (new PRACTICE tab): count-in (click or sticks), loop of 1, 2, 4 or 8 bars or a range of programmed sections, and a tempo trainer (up, down or ping-pong, optional "only when I am in time", rest bar, stop at target). MIDI notes 51 to 55 on channel 16 control it.
* **MIDI capture**: always records what the drummer plays; save or drag the last 4, 8, 16 or 32 bars out as a Standard MIDI File with tempo and meter.
* **Learn My Playing** (Auto Setup): about 10 seconds of playing give proposed input gain, gate, follow settings, timing mode, BPM, style and intensity with reasons. Nothing changes until Apply, each field can be unticked, and Undo restores exactly the applied fields.
* **Automatic meter detection** (STYLE tab): suggests 3/4, 5/4, 6/8, 7/8 and so on from the guitar when no host or manual meter is set.
* **Editor**: nine tabs (LIVE, KIT, DRUMMER, STYLE, SONG, PRACTICE, MIDI, ANALYSIS, SETTINGS); right-click or Shift+F10 on any control for Reset, Copy, Paste and MIDI Learn; keyboard focus ring; Reduce motion and High-contrast options; sectioned Help and About generated from `docs/HELP.md`; optional first-run tour; Riff Lock grid and limb report on ANALYSIS.
* 110 host parameters in total (all appended; see [PARAMETERS.md](PARAMETERS.md)).

## Fixed
* **Audio-thread allocation risk**: the plug-in handed its MIDI scratch buffer to the host with a swap and kept the host's buffer, so a later block could allocate. It now copies the MIDI out.
* **Damaged preset files**: a preset containing NaN or infinite values put NaN on parameters and in the audio. Such values are now ignored and the rest are clamped.
* **About text**: pre-release builds now show their tag (for example `0.4.0-beta.1`) and the build hash; before this fix they showed only `0.4.0`.
* The MIDI tab "Clear" button had no tooltip.

## Compatibility
* State version 4. Versions 1 to 3 still load; parameters a session does not contain take their defaults. A running practice session is never restored (the Practice settings are).
* Parameter IDs are unchanged from 0.3.x; all new ones are appended. `jamStyle` keeps its 22 choices.
* MIDI commands are now 20 (notes 36 to 55 on channel 16 by default); 0.3.x used 36 to 50.
* The pre-release tag is set at build time with `-DROBODRUMMER_PRERELEASE_TAG=beta.1`; the release workflow does this from the version you enter.

## Historical 0.4.0 verification

These bullets record the original feature-audit evidence. Current repaired-source counts, native runs and remaining qualification are maintained in [QA](QA.md).
* 53 JUCE-free tests pass, also under AddressSanitizer and UndefinedBehaviorSanitizer (Linux).
* The headless plug-in harness (2300+ checks) passes. It includes a real-time allocation audit (including JUCE containers on Linux) with every new feature switched on at 44.1, 48, 96 and 192 kHz and block sizes from 1 to 40000 samples, fuzzing of saved state and preset files, and a check that every parameter has a control, tooltip and documentation entry.
* `pluginval` 1.0.3 at strictness 10 passes for the VST3 on Linux.
* Linux package script run against a real build.

## Known limitations
* Real DAW/device host-matrix acceptance remains outstanding. Native builds, validator runs and isolated installer checks are recorded by exact source in [QA](QA.md); they do not replace physical clean-machine or DAW acceptance.
* Without authorized signing credentials, prerelease packages are explicitly unsigned/ad-hoc. Stable Windows/macOS packaging fails instead of silently falling back to unsigned output. Actual paid signing/notarization acceptance remains outstanding.
* Tempo and beat tracking were tuned and tested on synthetic guitar; a licensed real-guitar corpus does not exist yet.
* Native-window regressions open menus, invoke actions and cover asynchronous teardown. Actual host key forwarding and screen-reader operation remain qualification work.
* Oversized guitar-following callbacks now schedule entry on a trusted future beat and render the whole buffer in bounded scratch chunks. Automated audio/MIDI/capture/practice regressions cover the repaired path; real host/device acceptance remains separate.
* macOS/Windows reduced-motion defaults are read when no explicit saved choice exists; unavailable Linux preference defaults off. Reduced motion freezes onset/grid presentation until accessible Update while analysis and FIFO draining continue.
* Deterministic analysis remains on the audio thread under ADR 10. Current measured CPU evidence belongs to [QA](QA.md); the ADR governs when a worker is warranted.

## Install
See INSTALL.md inside each package. Verify downloads with `SHA256SUMS.txt`.

* Windows: `-windows-x64.zip` or the `-setup.exe` installer
* macOS Apple silicon: `-mac-arm64.zip`; Intel: `-mac-x64.zip`
* Linux: `-linux-x64.tar.gz`


## Source hardening (2026-10-31)

- Bound dense/long MIDI input and extreme arrangement seeks, clamp unrepresentable PPQ timelines before conversion, and retain Stop/voice safety without callback allocation or sampler destruction.
- Prevent adaptive-entry starvation at oversized buffers; align audio, MIDI, capture and practice at the predicted future beat and stream audio beyond fixed scratch capacity.
- Sanitize imported floating-point audio, bound kit decoding memory and apply mixer changes to ringing voices smoothly.
- Publish coherent MIDI bindings; keep analysis finite and predicted beat-in-bar advancing through rests.
- Validate preset slices transactionally and invalidate stale Learn My Playing reviews/Undo across preset and host-state replacement.
- Validate complete numeric menu paste, balance host gestures, guard detached drum-map actions and honor platform reduced motion with frozen, manually refreshed onset presentation.
- Add offline fixture ingestion/metrics, native Windows qualification, pinned CLAP reporting, archive inspection, isolated installer smoke and fail-closed signing preparation.

These are source changes, not a published release. Real recordings, physical DAW/device/screen-reader acceptance and paid signing/notarization remain outstanding; see [QA](QA.md).
