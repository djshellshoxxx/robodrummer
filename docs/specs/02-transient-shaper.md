# 02 - Transient Shaper (attack / sustain)

Status: Implemented (core, processor wiring and editor UI)

## Purpose & user value
Level-independent control of drum attack and sustain: snap up the stick or soften it, lengthen or tighten tails, with no thresholds to set per kit or style. Off by default.

## Behavior (precise)
- Header-only, JUCE-free: `src/dsp/TransientShaper.h` (`robodrummer::dsp::TransientShaper`), helpers in `src/dsp/DspCommon.h`. Stereo, linked detection, zero latency (no lookahead).
- Detector input `r = max(|L|,|R|)` (inputs sanitised, clamped to +/-1e4). Two peak envelope followers (1-1/e time constants): fast (attack 1 ms, release 35 ms) and slow (attack 25 ms, release 150 ms).
- Differential in dB: `d = 20*log10((fast+eps)/(slow+eps))`, `eps = 1e-4` (-80 dBFS, so noise-floor ratios are ignored). `d > 0` = attack phase (fast above slow), `d < 0` = decay/sustain phase. Both followers see the same scaling, so `d` is independent of absolute level.
- Gain in dB (A, S in [-1, +1] are attack and sustain amounts, percent / 100): `d > 0`: `A * 12 * tanh(max(d - 1.5, 0)/12)`; `d < 0`: `S * 18 * tanh(3*max(-d - 0.3, 0)/18)`. The dead zones (1.5 dB attack, 0.3 dB sustain) keep steady low-frequency material, where peak followers disagree by about 1 dB, exactly neutral; the 3x sustain scale compensates for the small differential on decays. Soft caps: 12 dB (attack), 18 dB (sustain). A = -1 pulls the transient toward the slow envelope; S = +1 lifts tails, S = -1 shortens them.
- Output: `wet = in * 10^(gainDb/20) * outTrim`; `out = dry*(1-mix) + wet*mix`. With A = S = 0, mix = 1, trim 0 dB the output equals the input exactly.
- Enable: linear 5 ms crossfade; when disabled and settled the output is the input bit for bit (finite input) and state is reset. Non-finite input -> 0 even in bypass. Processed output flushed below 1e-30.
- Smoothing: attack, sustain, mix, output trim: 10 ms exponential per sample (snap 1e-6). Per-sample processing makes output independent of block size.

## Parameters (APVTS IDs)
| ID | Type | Range | Default | Unit | Smoothing |
|---|---|---|---|---|---|
| transientShaperEnabled | bool | off/on | off | - | 5 ms crossfade |
| transientShaperAttack | float | -100..100 | 0 | % | 10 ms |
| transientShaperSustain | float | -100..100 | 0 | % | 10 ms |
| transientShaperMix | float | 0..100 | 100 | % | 10 ms |
| transientShaperOutput | float | -12..12 | 0 | dB | 10 ms |

`TransientShaperParams` holds attack, sustain and mix in percent (mix 0..100) so APVTS values map straight across.

## Actions / MIDI mapping
None required; parameters are automatable and MIDI-learnable. Read-only meter `gainDb()` (gain applied at the end of the last block).

## State serialization
The five APVTS parameters only; missing IDs load defaults (effect off). Integrator bumps the state version.

## UI placement & labels & tooltips
Mixer tab, "Transient Shaper" section left of "Drum Bus". Labels: "Attack (%)", "Sustain (%)", "Mix (%)", "Output (dB)", "Transient Shaper On/Off". Bipolar sliders have a centre detent at 0 and a signed numeric readout ("+35 %"). Tooltips: Attack "Positive = punchier hits, negative = softer hits"; Sustain "Positive = longer ring, negative = tighter". Keyboard focusable; state shown as text, not colour.

## Threading/real-time
As Drum Bus: prepare/setParams off the audio path (setParams is audio-thread safe), `processBlock` free of allocation/locks/I/O, any block size, meter via relaxed atomic.

## Test plan
`tests/test_transient_shaper.cpp`: attack +/- and sustain +/- on a synthetic click+decay (window energy in dB), level independence (-30 dB vs -6 dB input), neutrality at 0 and on steady sines, mix 0, output trim, bypass bit-exactness and crossfade click bound, 44.1k..192k, NaN/Inf, denormals, block-size independence (< 1e-6), reset, sanitising setters, nullptr/zero-length.

## Acceptance criteria
All tests pass; A=+1 raises the first 10 ms by > 3 dB, A=-1 lowers it by > 3 dB, S likewise for the tail; gain profile within 0.5 dB between -30 and -6 dBFS inputs; bypass bit-exact; latency 0.

## CDL compliance notes
As Drum Bus: stable IDs, documented units/ranges/smoothing, click-free bypass, finite output.

## Integration hook points
Insertion in `RoboDrummerAudioProcessor::processBlock` (src/plugin/PluginProcessor.cpp, "drum rendering" section): after `renderSpan(renderedUntil, numSamples - renderedUntil);` and inside `if (renderInternal) {`, i.e. after the main-bus voices are rendered into `drumL_/drumR_`, BEFORE the room return is summed and before `masterGain_`:
1. Immediately before `const auto channelGain = ...` add `shaper_.processBlock(drumL_.data(), drumR_.data(), renderSamples); drumBus_.processBlock(drumL_.data(), drumR_.data(), renderSamples);`. The room reverb return (`roomL_/roomR_`, fed from every voice) is added afterwards in the final per-sample loop, so ambience is not pumped or sustain-boosted, and `masterGain_` (master fader/mute) then applies to the whole mix. Aux outputs 1-4 (`auxLeft/auxRight`) remain unprocessed stems. Alternative (not recommended): sum the room into `drumL_/R_` first and process the sum, which means splitting the final loop in two.
2. Order: Transient Shaper first, Drum Bus second (the compressor sees the shaped transients).
3. `prepareToPlay` (next to `reverb_.setSampleRate`, `masterGain_.reset`): `setParams(...)` then `prepare(sampleRate_, samplesPerBlock)` on both. `setLatencySamples(shaper_.latencySamples() + drumBus_.latencySamples())` (0 today).
4. Per block, before rendering: `shaper_.setParams(...)`, `drumBus_.setParams(...)` from the parameter atomics (cheap, audio-thread safe, sanitising).
5. `shaper_.reset(); drumBus_.reset();` wherever `reverb_.reset()` is called (prepare, transport reset, `resetListeningState`).
6. Skip both when `!renderInternal`. Blocks longer than `MaxScratchSamples` already clamp via `renderSamples`.
