# Open-Source Prior Art and Related Projects

Date: 2026-10-04

## Purpose

This note identifies open-source projects relevant to RoboDrummer. The goal is not to copy code blindly. It is to understand existing approaches, evaluate licenses, and identify components worth benchmarking or learning from.

## 1. BeatNet

Repository:

- https://github.com/mjhydri/BeatNet

What it does:

- real-time/online/offline beat tracking
- downbeat tracking
- tempo tracking
- meter tracking
- causal CRNN analysis
- particle-filter inference
- streaming microphone mode

Why it matters:

BeatNet is the closest open project found to RoboDrummer's real-time timing-perception layer. Its use of causal processing plus probabilistic particle filtering is especially relevant to the Guitarist Leads mode.

Potential use:

- research baseline
- offline comparison
- isolated-guitar benchmark
- architectural reference for confidence/hypothesis tracking

Caution:

The repository declares CC BY 4.0. Before embedding code or model assets into a distributed commercial plugin, verify the exact license scope and compatibility with the intended RoboDrummer license. Treat this as a reference implementation until that review is complete.

## 2. aubio

Repository:

- https://github.com/aubio/aubio

What it does:

- onset detection
- tempo/beat tracking
- pitch detection
- MFCCs
- FFT/phase-vocoder processing
- filters
- transient/steady-state separation
- live audio/MIDI-oriented utilities

Why it matters:

Aubio is useful as a conventional DSP/MIR baseline for:

- guitar onset detection
- tempo estimation
- beat timestamps

Its straightforward algorithms are useful for building a transparent early prototype before adopting heavier learned models.

License:

- GPLv3 in the repository's `COPYING` file.

Implication:

Do not directly embed GPLv3 aubio source into RoboDrummer if RoboDrummer is intended to be distributed under an incompatible proprietary or permissive license. It remains valuable as a research benchmark and algorithm reference.

## 3. madmom

Repository:

- https://github.com/CPJKU/madmom

What it does:

- music information retrieval library
- onset, beat and downbeat analysis
- neural-network activation models
- dynamic Bayesian beat tracking
- some online/live processing modes

Why it matters:

Madmom provides high-quality reference implementations for beat/downbeat research and is useful for generating comparison data while developing RoboDrummer's C++ tracker.

License note:

The project states that source code is generally BSD-licensed, while its model/data files are generally CC BY-NC-SA 4.0. That distinction matters: model/data licensing is not automatically equivalent to the source-code license.

Implication:

- source algorithms may be useful as reference depending on file-level terms
- bundled/pretrained model assets should not be assumed safe for commercial redistribution

## 4. Marsyas / IBT lineage

Related research:

- Oliveira, Gouyon, Martins and Reis: *IBT: A Real-time Tempo and Beat Tracking System*.

IBT adapts competing beat hypotheses for causal continuous input and was implemented in C++ inside the Marsyas framework.

Why it matters:

This is conceptually close to RoboDrummer's need for:

- causal input processing
- multiple competing tempo/beat hypotheses
- robust recovery from noisy input
- C++-friendly implementation

Even if the old code is not directly reused, the design direction is highly relevant.

## 5. CPJKU madmom online beat processors

The madmom codebase includes online-capable beat processors and dynamic Bayesian approaches. It is useful for benchmarking what happens when a tracker assumes stronger continuity priors.

RoboDrummer can use this comparison to tune how strongly its own tracker resists sudden tempo/phase changes.

## 6. Magenta / GrooVAE lineage

Google Magenta's drum-generation research, particularly GrooVAE-related work, is relevant to expressive groove generation and humanized drum timing.

Potential lessons:

- expressive timing/velocity can be represented as more than a quantized MIDI grid
- groove transformation and generation can be learned from performance data
- latent/style representations may eventually help RoboDrummer create variation

Recommended RoboDrummer policy:

Do not make a neural groove generator a Phase 1 dependency. Build the deterministic/probabilistic style engine first, then compare a learned groove system later.

## 7. Common Music Information Retrieval libraries

Other useful research/prototyping tools include:

- librosa — offline/prototyping MIR features
- Essentia — broad audio/music analysis library
- Sonic Visualiser — useful for manually inspecting timing annotations and regression fixtures

These are useful for analysis tooling even if none belongs in the production plugin.

## 8. Product prior art that is not necessarily open source

Several commercial or closed systems solve portions of the problem:

- Rayzoon Jamstix: virtual drummer with real-time behavior models and audio-jam features
- Logic Pro Drummer: generated drummer and source-follow behavior
- EZdrummer Bandmate: analyzes supplied musical material and suggests/adapts drum grooves
- Ableton Tempo Follower / BeatSeeker: live tempo-follow concepts

These are important product references because they show market demand for individual pieces of the concept, but RoboDrummer's target combination is different: a live guitarist-focused virtual drummer whose autonomous clock can be led by the performer and whose generated drumming remains interactive during an open-ended jam.

## 9. What appears to be missing

The open-source ecosystem has strong components for:

- onset detection
- beat tracking
- downbeat/meter tracking
- offline groove generation
- MIR feature extraction

The gap is integration into a polished low-latency drummer that combines:

```text
live isolated-guitar analysis
+ predictive tempo/phase following
+ confidence/recovery
+ procedural style-aware drum generation
+ jam/phrase coordination
+ optional MIDI intervention
```

That integration is the core RoboDrummer project.

## 10. Dependency policy recommendation

For the production plugin:

1. prefer original C++ implementations of required real-time DSP/tracking logic
2. use open projects to benchmark behavior and validate algorithms
3. document every third-party code/model license separately
4. do not assume source and pretrained-model licenses are identical
5. keep research Python tooling separate from shipping plugin dependencies

## 11. Recommended benchmark matrix

Create a development harness that runs the same guitar fixtures through:

- RoboDrummer baseline DSP tracker
- aubio
- BeatNet
- madmom online tracker where practical

Compare:

- acquisition time
- tempo accuracy
- phase continuity
- recovery time
- half/double-time errors
- CPU usage
- behavior on clean versus distorted guitar

This turns prior art into measurable engineering evidence rather than subjective selection.
