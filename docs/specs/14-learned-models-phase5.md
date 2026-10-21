# 14. Learned models (SPEC 38 Phase 5)

Status: **Proposed, gated.** Nothing in this document is implemented. SPEC 38 allows Phase 5 only after the deterministic and statistical layers are validated; that gate is not met (no real-guitar corpus yet, `11-real-guitar-fixtures.md`).

## Purpose & user value

A compact causal beat/downbeat model could improve acquisition time and robustness on material where transparent DSP struggles (dense arpeggios, sustained chords, syncopated funk, odd meters) and could supply downbeat and meter evidence (`07-auto-meter.md`). It must never reduce the stability, predictability or transparency of the deterministic engine, which stays the fallback and the timing authority unless the model proves better on the fixture set.

## Behavior (precise)

### Scope (what a model may do)

1. **Beat and downbeat activation** (frame-wise probabilities at about 100 Hz) from the conditioned guitar signal. Output feeds the existing hypothesis/PLL machinery as an additional evidence stream, not a replacement: `RhythmAnalyzer` consumes activations the same way it consumes `OnsetEvent` strength.
2. **Meter evidence** (probabilities for 3, 4, 5, 6, 7 beats per bar) consumed by the meter selector with manual override unchanged.
3. Out of scope for the first model: section/playing-state classification and learned style adaptation (also listed in SPEC 38); each needs its own spec after the beat model ships.

### Model class

* Causal only (SPEC 40.6): no look-ahead beyond the frame; unidirectional layers.
* Compact: target <= 300k parameters, <= 1.5 MB on disk (float32; 8-bit weights allowed if accuracy holds), <= 25 MFLOP per second of audio.
* Input: log-magnitude mel spectrogram (about 40 bands, 20 Hz to 8 kHz, 10 ms hop) or the existing band-flux features plus a short raw-flux history. Feature extraction is deterministic C++ already close to what `AdaptiveOnsetDetector` computes.
* Architecture candidates (evaluate in order): (a) small causal TCN (dilated 1-D convolutions) with a beat/downbeat head; (b) 1 to 2 layer GRU/LSTM (16 to 48 units) over TCN features, as in BeatNet-like CRNNs; (c) the same followed by a conventional particle filter or the existing PLL. Start with (a)+PLL since it is simplest to bound and test.
* Latency budget: model contributes <= 1 frame (10 ms) of added algorithmic latency; total tracker acquisition must not be slower than the deterministic baseline.

### Data

Training data must be isolated or guitar-forward, because full-mix trained models do not transfer (research/realtime-tracking.md section 7):

* Sources, in preference order: (1) the project's own licensed real-guitar corpus (`11-real-guitar-fixtures.md`, training split); (2) synthesised guitar from MIDI with beat/downbeat labels (render with open guitar samples or physical models, many tempi, meters, tones, palm mute, strums, noise, amp sims); (3) public beat datasets restricted to stems or guitar-dominant tracks only where the licence allows training and redistribution of derived weights.
* **Splits:** by performance/recording session, never by clip segment. The fixtures corpus (`11`) is the **frozen test set** and is never used for training or hyper-parameter selection; a separate validation split is carved from the training sources.
* Augmentation: gain, EQ tilt, noise, mild distortion, time stretch within 0.85x to 1.15x (labels scaled), channel selection, silence insertion.

### Licensing

Every dataset, code snippet and pretrained model gets its own licence row in `docs/MODEL_LICENSES.md` (new) before use. Rules from `research/open-source-prior-art.md`:

* BeatNet repository is CC BY 4.0: reference and benchmark only until a legal review says embedding code or weights is acceptable; attribution required if used.
* madmom code is BSD, but its models/data are CC BY-NC-SA 4.0: **never ship madmom model files**; usable for generating comparison numbers offline only.
* aubio is GPL: reference only, never linked.
* Datasets with non-commercial terms (check each) may be used for offline research benchmarking but never for weights that ship.
* The shipped weights must be traceable to datasets whose licence permits commercial redistribution of derived models, and the training pipeline (scripts, seeds, config) must be in the repository so weights can be reproduced.
* No network access at runtime (CDL section 8): weights are embedded in the binary (or in a bundled resource file), never downloaded.

### Runtime options

| Option | Pros | Cons | Verdict |
|---|---|---|---|
| **RTNeural** (header-only C++, templates or runtime, supports Dense, Conv1D, GRU, LSTM; designed for audio threads) | No allocation after construction with static-shape models; small; compiles into the plug-in; used in audio plug-ins | Limited layer set; need to export weights to its JSON format; verify its licence (BSD-style) and bundled dependencies at adoption | **First choice** for the GRU/TCN-sized model |
| **ONNX Runtime** (C API) | Standard export from PyTorch; wide operator set; quantisation tools | Large binary (several MB), threading and allocation behaviour must be audited, harder to guarantee real-time safety; per-platform builds including macOS universal | Only for non-real-time use (offline analysis tool, evaluation) or on a worker thread |
| Hand-written C++ inference | Total control, zero dependencies, trivial to audit | Engineering effort; only practical for a very small TCN/GRU | Fallback if RTNeural does not fit |
| Larger models (full BeatNet-size CRNN) | Accuracy | CPU and binary size; domain mismatch | Out of scope for the plug-in |

### Real-time constraints

* Inference runs in fixed-size frames; all buffers preallocated in `prepareToPlay`; no allocation, locks, I/O or logging in `processBlock`; denormals disabled (already `ScopedNoDenormals`); NaN/Inf outputs sanitised to 0 before use.
* **CPU budget:** model inference <= 3 % of real time at 48 kHz on the reference machine (analysis total stays <= 5 %, `10-analysis-worker-thread.md`). If the budget cannot be met inline, this is the trigger to adopt the analysis worker design from that ADR; the model then runs on the worker with snapshot extrapolation.
* Deterministic: same input and weights give bit-identical output on a given platform; across platforms outputs must agree within a stated tolerance (activations within 1e-3) and all fixture metrics must pass on every platform.
* Sample rates 11 to 192 kHz: resample the feature stream to the model's rate inside the analysis path; the model only sees its native frame rate.
* Failure mode: if weights fail to load, are disabled, or the model reports low confidence, behaviour is exactly the deterministic baseline (feature flag off = bit-identical to today).

### Evaluation vs deterministic baseline

All on the frozen fixture set (`11`), same metric code:

* Tempo error and time to lock (thresholds from audit P1: < 2 BPM, lock < 6 s, >= 90 %).
* Beat F-measure (70 ms), phase error median/95th percentile, downbeat accuracy, meter accuracy (`07`).
* Stability metrics: number of spurious relocks, octave errors, behaviour on rubato (C18) and silence/re-acquire (C23).
* Paired comparison per clip; report where the model is worse. Use a bootstrap over clips for confidence intervals; with about 24 clips differences must be large to be meaningful.
* Cost: CPU, memory, binary size, load time.

### Gating (all must hold before the model is enabled by default)

1. Real-guitar corpus complete and the deterministic baseline's numbers published (`11`, `docs/QA.md`).
2. Model beats the baseline on tempo error and lock time by a pre-registered margin (>= 20 % relative on the median, no clip worse than baseline by more than 2 BPM) **and** does not increase spurious relocks.
3. Licence review complete; weights reproducible from the repository.
4. CPU and allocation audits pass; pluginval strictness 10 passes; host matrix re-run on primary hosts.
5. A user-visible switch exists (below) and the default stays "Deterministic" for at least one beta cycle, then flips only with explicit approval of the maintainer.

## Parameters

| ID | Type | Range | Default | Unit | Smoothing |
|---|---|---|---|---|---|
| `trackingEngine` | choice | Deterministic, Assisted | Deterministic | n/a | none (switched on a bar boundary; analyser state keeps running) |

Appended last to the parameter list; state version hint increases by one when it ships.

## Actions/MIDI mapping

None.

## State serialization

`trackingEngine` is a normal parameter. Model identity (name, version, hash) is not saved in the session; it is a property of the build and shown in About/ANALYSIS.

## UI placement & labels & tooltips

SETTINGS tab, follow-behaviour group: combo "Tracking engine" with tooltip "Deterministic uses the standard onset and tempo tracker. Assisted adds a small built-in model that helps with dense or sustained playing. Both run locally; nothing is sent anywhere." The ANALYSIS tab shows a text line "Model: off / on, confidence x" (not colour only).

## Threading/real-time

See constraints above. Weight loading happens off the audio thread (message thread during construction) into preallocated storage; swapping engines is an atomic index flip applied at a bar boundary.

## Test plan

1. Unit: layer-by-layer numeric comparison against the PyTorch reference on fixed inputs (tolerance 1e-4); NaN/Inf, zero-length and huge block sizes; save/restore state with the parameter at both values.
2. Allocation audit with the model enabled: 0 allocations.
3. Fixture evaluation harness (shared with `11`) producing the paired table.
4. Cross-platform determinism check in CI (Windows, Linux, macOS).
5. Fallback test: corrupt weights file leads to deterministic behaviour with a status message.

## Acceptance criteria

Gating list above, plus: binary size increase <= 3 MB, load time < 50 ms, no new third-party licence without an entry in `docs/MODEL_LICENSES.md`.

## CDL compliance notes

CDL 8: no network access, no telemetry, no downloads; all processing local. CDL 3.2: real-time safety evidenced by the allocation audit. Transparency: the model is optional and its state visible.

## Integration hook points

`RhythmAnalyzer` (accept an `ActivationFrame` evidence input beside onsets), meter selector (`src/core/MeterSelection.h`) for meter evidence, `PluginProcessor::prepareToPlay` (allocate model, load weights), the analysis block of `processBlock` (run model per hop), parameter layout in `createParameterLayout` (append `trackingEngine`), SETTINGS panel (combo), `docs/PARAMETERS.md` regeneration.
