# 11. Real-guitar fixture corpus (SPEC 36, audit P1)

## Purpose & user value

Tracker thresholds were tuned on synthetic transients (`tests/test_tracking_fixtures.cpp`). Real guitar (distortion, palm muting, sustained chords, arpeggios) is the product's actual input and the largest unvalidated risk. This spec defines how to build, label, license, store and run a corpus so tempo error, lock time and later meter detection (`07-auto-meter.md`) and learned models (`14-learned-models-phase5.md`) are measured, not guessed.

## Behavior (precise)

### Clip list (minimum 20 clips, 24 targeted)

All clips are **isolated guitar** (DI or amp-simulated, no drums, no backing track), mono, 30 to 90 s, one or more tempo/meter segments each.

| ID | Category | Count | Notes |
|---|---|---|---|
| C01-C03 | Clean strumming | 3 | 70, 100, 140 BPM, 4/4 |
| C04-C06 | Distorted power chords / riffs | 3 | 90, 120, 160 BPM |
| C07-C09 | Palm-muted chugging | 3 | 110, 140, 180 BPM, 8th and 16th feel |
| C10-C11 | Arpeggios (clean) | 2 | dense onsets, 80 and 120 BPM |
| C12 | Blues shuffle | 1 | 100 BPM, swung 12/8 feel counted in 4/4 |
| C13 | Funk (muted, syncopated) | 1 | 100 BPM, 16th syncopation |
| C14 | Slow sustained chords | 1 | 60 BPM, weak onsets |
| C15 | Single-note riff | 1 | 120 BPM |
| C16 | Accelerando | 1 | 100 to 130 BPM over 30 s |
| C17 | Sudden tempo change | 1 | 100 to 140 BPM |
| C18 | Rubato / deliberately irregular | 1 | no stable tempo (expected: low confidence, no wrong lock) |
| C19 | Odd meter 3/4 | 1 | |
| C20 | 6/8 | 1 | |
| C21 | 5/4 | 1 | |
| C22 | 7/8 | 1 | |
| C23 | Start/stop with silences | 1 | tests re-acquire |
| C24 | Noisy: hum, hiss, amp noise | 1 | gate/calibration behaviour |

### Audio format

* WAV, 48 kHz, 24-bit PCM mono (original recordings may be 44.1 kHz; the harness resamples the same way a host would, so also keep a 44.1 kHz variant for three clips to cover rate handling).
* No loudness normalisation. Peak between -18 and -1 dBFS. No clipping. No dither or fades other than what the performer played.
* File name: `<ID>_<category>_<bpm>bpm_<meter>.wav`, for example `C07_palmmute_140bpm_4-4.wav`.

### Labeling schema

One sidecar JSON per clip, `<same name>.json`, UTF-8, schema version 1:

```json
{
  "schema": 1,
  "id": "C07",
  "file": "C07_palmmute_140bpm_4-4.wav",
  "sample_rate": 48000,
  "duration_s": 62.4,
  "category": "palm_muted",
  "tone": "distorted",
  "meter": { "num": 4, "den": 4, "segments": [{ "start_s": 0.0, "num": 4, "den": 4 }] },
  "tempo": { "type": "constant", "bpm": 140.0 },
  "tempo_map": [{ "start_s": 0.0, "bpm": 140.0 }],
  "beats_s": [0.512, 0.941],
  "downbeats_s": [0.512, 2.226],
  "expected": { "lock_by_s": 6.0, "tempo_tolerance_bpm": 2.0, "locks": true },
  "annotation": { "method": "metronome_click_track + manual_correction", "annotator": "initials", "reviewed_by": "initials", "date": "YYYY-MM-DD" },
  "license": { "spdx": "CC-BY-4.0", "holder": "Name", "source": "own recording", "release_form": "docs/fixtures/releases/C07.pdf" },
  "notes": "free text"
}
```

Rules:

* `beats_s` and `downbeats_s` are ground truth, in seconds from file start, recorded against a click track that was **not** printed into the audio (recorded on a separate channel and discarded from the repo copy), then corrected by ear against waveform onsets.
* `tempo_map` has one entry for constant tempo and one per breakpoint (linear ramp between entries) for C16 and C17. Rubato clips (C18) use `"tempo": {"type": "none"}` and `"expected": {"locks": false}`.
* `meter.segments` lists meter changes if any.
* Two annotators for at least C12, C13, C16, C18 (disagreement over 20 ms on any beat is resolved by listening and recorded in `notes`).

### Licensing rules

* Accept only: recordings the project owner makes (assign CC BY 4.0 or CC0), or clips with written permission or a clear licence permitting redistribution **and** use in automated tests and derived analysis (CC0, CC BY, CC BY-SA acceptable; no NC, no ND, no "all rights reserved", no "royalty free" licences that forbid redistribution).
* No commercial songs, riffs from copyrighted compositions, or commercial sample-pack material. Original riffs only, or public-domain / traditional material.
* Anything where a third party performs requires a signed release form stored with the corpus metadata (not in the repo if it contains personal data; store a hash and an internal reference).
* Each sidecar carries the licence; a CI step (below) fails if a clip lacks one or uses a non-allowed SPDX identifier.
* Corpus licence is recorded in `fixtures/real-guitar/LICENSES.md`. Models trained on the corpus follow `14-learned-models-phase5.md` (corpus licence must allow it; hold-out clips are never used for training).

### Storage and size limits

* Target total corpus: <= 400 MB (24 clips x about 60 s x 48 kHz x 3 bytes = about 250 MB mono 24-bit). Optional FLAC variants cut this by about half but CI must decode with the existing loader (JUCE) or a small decoder; WAV is the reference.
* **Not stored in plain git history.** Two supported options, choose one at implementation time:
  1. **Git LFS** under `fixtures/real-guitar/` (`.gitattributes: fixtures/real-guitar/**/*.wav filter=lfs diff=lfs merge=lfs -text`). Sidecar JSON stays as plain git. Needs LFS bandwidth quota on the repo.
  2. **GitHub release asset** `fixtures-real-guitar-v<N>.zip` plus `fixtures/real-guitar/MANIFEST.json` (file names, SHA-256, sizes, corpus version) committed in git. CI downloads the asset and verifies hashes.
* Recommended: option 2 (no LFS quota, immutable versions, offline cache with `actions/cache` keyed by the manifest hash).
* A small **smoke subset** (6 clips, 30 s each, mono 16-bit 44.1 kHz, <= 12 MB) may live in plain git under `fixtures/real-guitar-smoke/` so every pull request runs real-guitar tests without downloads. Choose C01, C04, C07, C13, C16, C17.

### Running fixtures in CI

* New test `tests/test_real_guitar_fixtures.cpp` (core, JUCE-free): read WAV with a minimal PCM reader (add `tests/support/WavReader.h`; no external dependency), feed `InputConditioner` -> `RhythmAnalyzer` in 512-sample blocks at the file rate, sample the estimate every block.
* Skips with a clear "SKIPPED (corpus not present)" message and exit 0 if the corpus directory is absent, so fork/local builds do not fail. In CI the full job (`fixtures` workflow job, Linux only, on push to `main`/release branches and on PRs touching `src/analysis/**`) downloads the corpus and sets `ROBODRUMMER_FIXTURES=<dir>`; the smoke subset runs on every PR.
* The job writes a Markdown results table (clip, expected BPM, final BPM, error, time to lock, false lock yes/no) to `$GITHUB_STEP_SUMMARY` and uploads `fixtures-results.json` as an artifact.
* Metrics:
  * **Tempo error**: absolute difference between the tracker BPM at 80 % of the clip (or at each segment's last 20 %) and ground truth, folded for octave errors only if the octave error is reported separately.
  * **Time to lock**: first time the tracker is locked (hysteresis rule, `RhythmAnalyzer`) with tempo within tolerance and staying so for 2 s.
  * **Beat F-measure** (70 ms tolerance) and **phase error** (median absolute offset of predicted beats vs `beats_s`), reported but not gating initially.
  * **False lock**: lock on clips with `locks: false`.

### Acceptance thresholds (from audit P1)

Gating once the corpus is complete:

* >= 20 licensed clips present and labelled (schema valid).
* Tempo error < 2 BPM **and** lock within 6 s on **>= 90 %** of the clips that have `expected.locks = true` (C18 and C24 excluded from the denominator; reported separately).
* C18 (rubato): no lock held for more than 5 s with confidence above the "locked" threshold.
* Threshold changes made to reach this are committed with the before/after table.

### Synthetic stand-in plan (already exists)

Until recordings exist, `tests/test_tracking_fixtures.cpp` is the stand-in: deterministic seeded renderer (decaying 196 Hz tone plus 1.8 kHz click per hit) covering steady 70 to 190 BPM, ramp, missing events, syncopation, silence/re-acquire, half/double ambiguity, sudden change (re-lock about 2 s), jittered noisy transients and noise-only. It stays as the fast regression layer. Extension while waiting for recordings: add a "guitar-like" synthesiser variant (Karplus-Strong plucks, distortion by tanh, palm-mute by shortened decay, strums as 20 ms staggered 6-string onsets, arpeggio at 16th density) generating the same JSON sidecars, so the real-fixture runner can be built and CI-tested against synthetic "clips" now. Synthetic results are never reported as real-guitar results.

## Parameters

None.

## Actions/MIDI mapping

None.

## State serialization

None. Corpus metadata schema version 1 above.

## UI placement & labels & tooltips

None. (Calibration/ANALYSIS tab already exist for field debugging; users can export nothing from them. A later "Save diagnostic clip" button is out of scope and would need a privacy review per `docs/COMPLIANCE.md` section 8.)

## Threading/real-time

Offline test code only; the runner uses the same `RhythmAnalyzer` code path as the plug-in at block size 512 (also run at 64 and 2048 to confirm block-size independence).

## Test plan

1. Schema validator script `tools/check_fixtures.py` (or CTest `fixture_schema`): validates every JSON, file existence, sample rate, duration, SPDX in the allowed list, hashes vs manifest.
2. `test_real_guitar_fixtures`: per-clip metrics above; asserts thresholds only when the full corpus is present, otherwise asserts smoke-subset sanity (no crash, finite outputs, tempo within 5 BPM on steady clips).
3. Regression rule (SPEC 36): every field-discovered tracking bug adds a clip or a synthetic fixture.

## Acceptance criteria

See thresholds above, plus: corpus manifest hash recorded in `docs/QA.md`; the QA row "Real guitar fixtures" changes from **not run** to a dated result with the table.

## CDL compliance notes

CDL section 8 (privacy): no recordings of users are collected or uploaded by the plug-in. Contributors must consent in writing. CDL section 9 (documentation): results recorded in QA.md.

## Integration hook points

* the `CMakeLists.txt` test glob (`tests/test_*.cpp`) picks up the new test automatically; `ROBODRUMMER_FIXTURES` env var selects the corpus.
* `.github/workflows/core-ci.yml`: add the `fixtures` job (download release asset, verify manifest, run `ctest -R real_guitar`).
* `docs/QA.md`: replace the "not run" row with the generated table at each release.
