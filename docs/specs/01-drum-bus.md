# 01 - Drum Bus (compressor + parallel mix + drive)

## Purpose & user value
Glue and punch for the internal drum mix: a feed-forward bus compressor with soft knee, an optional sidechain high-pass (so the kick does not pump the hats), a dry/wet "New York" parallel blend, and a gentle saturation stage. Off by default so existing renders are unchanged.

## Behavior (precise)
- Header-only, JUCE-free: `src/dsp/DrumBus.h` (`robodrummer::dsp::DrumBus`), shared helpers in `src/dsp/DspCommon.h`. Stereo, linked detection (max of L/R), zero latency.
- Per sample: input sanitised (NaN/Inf -> 0, clamped to +/-1e4 inside the processed path) -> optional sidechain HPF (2nd-order Butterworth, detector only) -> level detector -> static gain computer -> ballistics -> gain (+ makeup) on the wet path -> drive -> dry/wet mix -> enable crossfade.
- Detector: Peak (`|x|` of the linked max, dB) or RMS (mean-square one-pole, 10 ms time constant, power of the linked max). Floor -120 dB.
- Gain computer (Giannoulis soft knee, dB): `over = L - T`, `W = knee`, `slope = 1/R - 1`. `2*over < -W`: 0. `2*|over| <= W`: `slope*(over+W/2)^2/(2W)`. else `slope*over`. Result is gain reduction <= 0 dB. W < 0.001 is a hard knee.
- Ballistics on the gain reduction in dB, branching: target lower than current uses attack, otherwise release. Times are 1-1/e time constants (`coef = exp(-1/(tau*fs))`), so they are sample-rate independent in milliseconds.
- Makeup is a gain added to the wet path (dB), smoothed.
- Drive (0..1): `g = 1 + 2*drive`, bias `b = 0.1*drive`, `shaped = (tanh(g*w + b) - tanh(b))/g` (unity small-signal slope, soft limiting near 1/g, mild asymmetry). Only the difference `shaped - w` is DC-blocked (one-pole, 10 Hz) and added back scaled by `a = min(1, 4*drive)`: drive = 0 is exactly transparent and the unprocessed phase is preserved. Aliasing is limited by the low maximum gain (3x) and the soft curve; no oversampling.
- Mix (0..1): `out = dry*(1-mix) + wet*mix`, dry = the stage input (time aligned, latency 0). Mix 1 is fully wet.
- Enable: linear 5 ms crossfade between input and processed. When disabled and settled the output is the input bit for bit (finite input) and internal state is reset (re-enable starts clean). Non-finite input is replaced by 0 even in bypass.
- Output samples with |y| < 1e-30 are flushed to 0 in the processed path (denormals). Internal states flush too.
- Parameter smoothing: threshold, ratio, knee, makeup, mix, drive: 10 ms exponential, per sample, snapping within 1e-6. Attack, release, detector and HPF frequency apply immediately (they only shape the detector/ballistics). Everything is per-sample, so output is independent of block size.

## Parameters (APVTS IDs, all <= 24 chars)
| ID | Type | Range | Default | Unit | Smoothing |
|---|---|---|---|---|---|
| drumBusEnabled | bool | off/on | off | - | 5 ms crossfade |
| drumBusThreshold | float | -60..0 | -18 | dB | 10 ms |
| drumBusRatio | float | 1..20 | 4 | :1 | 10 ms |
| drumBusKnee | float | 0..24 | 6 | dB | 10 ms |
| drumBusAttack | float | 0.1..100 | 10 | ms | none |
| drumBusRelease | float | 10..1000 | 120 | ms | none |
| drumBusMakeup | float | -12..24 | 0 | dB | 10 ms |
| drumBusDetect | choice | Peak, RMS | Peak | - | none |
| drumBusScHpf | float | 0 (off), 20..500 | 0 | Hz | none |
| drumBusMix | float | 0..1 | 1 | fraction (show %) | 10 ms |
| drumBusDrive | float | 0..1 | 0 | fraction (show %) | 10 ms |

## Actions / MIDI mapping
None required. All parameters are host-automatable and MIDI-learnable through the existing learn path. Read-only meter: `gainReductionDb()` (atomic, updated per block).

## State serialization
Only the 11 APVTS parameters above (state version bump by the integrator; missing IDs load defaults, i.e. effect off). No other state is saved.

## UI placement & labels & tooltips
Mixer tab, "Drum Bus" section right of the Master strip. Labels with units: "Threshold (dB)", "Ratio (:1)", "Knee (dB)", "Attack (ms)", "Release (ms)", "Makeup (dB)", "Detector: Peak/RMS", "Sidechain HPF (Hz, 0 = off)", "Mix (%)", "Drive (%)", "Drum Bus On/Off". The gain-reduction meter has a numeric dB readout, not colour only. Every control is keyboard focusable with a tooltip (e.g. Mix: "100% = fully compressed, lower blends in the uncompressed drums (parallel compression)"). On/off is a labelled toggle with a text state.

## Threading/real-time
`prepare` and `setParams` on the message/prepare thread (setParams is also safe on the audio thread: no allocation). `processBlock` audio thread only: no allocation, locks, I/O or logging; fixed storage; any block size including 0 and > maxBlock. Meter is a relaxed atomic float.

## Test plan
`tests/test_drum_bus.cpp`: static curve numbers (hard/soft knee, ratio 1, monotonic), steady-state gain reduction vs threshold/ratio/makeup through the processor, attack and release time constants at 44.1k..192k (+/-5%), RMS vs peak, sidechain HPF effect, parallel mix, drive (transparent at 0, DC-free, bounded), bypass bit-exactness, crossfade click bound, NaN/Inf, denormals, block-size independence (1/7/64/512, incl. mid-stream param change, < 1e-6), reset, sanitising setters, nullptr/zero-length.

## Acceptance criteria
All tests pass; bypass bit-exact; latency 0; no allocation after construction; GR matches the formula within 0.05 dB; time constants within 5%.

## CDL compliance notes
Stable IDs, documented ranges/units/smoothing, no abrupt parameter zipper, click-free bypass, host-safe (no standalone-only controls), finite output always.

## Integration hook points
The chain insertion point is specified in `docs/specs/02-transient-shaper.md` ("Integration hook points"). Drum Bus is the second stage, after the Transient Shaper:
1. `prepareToPlay`: `drumBus_.setParams(...)` then `drumBus_.prepare(sampleRate_, samplesPerBlock)` (prepare snaps smoothers to the params), next to `reverb_.setSampleRate` / `masterGain_.reset`. `setLatencySamples(shaper_.latencySamples() + drumBus_.latencySamples())` (both 0).
2. Each block, before processing: `drumBus_.setParams(paramsFromAPVTS)` (audio thread, cheap).
3. `drumBus_.processBlock(drumL_.data(), drumR_.data(), renderSamples)` immediately after the shaper.
4. `drumBus_.reset()` wherever `reverb_.reset()` is called and on transport reset.
5. UI timer reads `drumBus_.gainReductionDb()` for the meter.
