# RoboDrummer QA record

Status: **BETA READY candidate** — see "Known blockers" before calling it RELEASE READY (CDL §9). Nothing below is claimed unless it was run and the result checked.

## Automated suites (run in CI on every push)

| Suite | What it covers | Result on this branch |
|---|---|---|
| Core suite (`ctest`, 44 tests, JUCE-free) | clock, scheduler, grooves, 22 styles × odd meters, humanization, fills, personalities, tracking fixtures, performance features, follow controls, MIDI tools, input conditioning, mixer, kit parsing, allocation audit of the core | 44/44 pass (Linux, Windows) |
| Tracking fixtures (`tests/test_tracking_fixtures.cpp`) | steady 70–190 BPM, ramp, missing events, syncopation, silence/re-acquire, half/double ambiguity, sudden change (re-locks in ~2 s), noisy jittered transients, noise-only | pass |
| Plug-in harness (`RoboDrummerHarness`, headless, real `AudioProcessor`) | playback and output modes, tempo sources (host, internal, MIDI clock start/stop), MIDI commands + learn, guitarist-leads acquisition/accelerando, mixer/room/aux outputs, WAV kit loading, state/preset round trips, programmed arrangement, calibration, **SPEC 37 beta scenario**, allocation audit, CPU budget, block sizes (1–70000, zero-length), 11–192 kHz, NaN/Inf, 400 mutated states + 100 garbage blobs, 40 randomised parameter/MIDI/block-size rounds, 5-minute simulated soak | 1342 checks, 0 failures |
| Editor snapshots | all 8 tabs rendered to PNG (`--screenshots`) | rendered and reviewed |
| pluginval 1.0.4 | VST3 at the **maximum strictness (10)**: scan, cold/warm open, editor (also while processing), audio processing at 44.1/48/96 kHz × 64–1024 blocks, state save/restore, automation, parameter thread safety, background-thread state, bus layouts (all buses enabled/disabled), parameter fuzzing | **SUCCESS** on Linux x64 (run locally under Xvfb, pluginval from the Tracktion `latest` release, 2026-10-14). Windows/Linux runs are also performed by the release workflow (non-blocking step, report in CI artifact `pluginval-*`). |

Measured on the development container (1 core of a shared VM): audio-thread cost ≈ **0.7 %** of real time at 48 kHz / 512 samples with aux buses, room reverb and guitar following; **0 allocations** per block after warm-up.

Run locally:

```bash
cmake -S . -B build -DROBODRUMMER_BUILD_JUCE=ON && cmake --build build
ctest --test-dir build -C Release --output-on-failure
./build/RoboDrummerHarness_artefacts/Release/RoboDrummerHarness --soak-minutes 30   # longer soak
```

## Manual / host checks

| Check | Status |
|---|---|
| Standalone starts and the editor renders (Xvfb) | done (harness snapshots) |
| Real DAW load/save/automate/resize/close (Reaper, Ableton Live, FL Studio, Cubase, Bitwig) | **not run** |
| Real guitar fixtures (clean, distorted, palm-muted, strumming, arpeggios, blues, funk, irregular time) | **not run** — no licensed recordings in the repository |
| AU validation (`auval`) | **not run** (macOS only) |
| Windows installer on a clean machine | **not run** (built by CI; install test pending) |

## Known blockers for RELEASE READY

1. Real-guitar fixtures and tracker tuning (SPEC 36).
2. Host-matrix testing and an `auval`/`pluginval` pass recorded per platform.
3. Code signing / notarisation.

## Known limitations

* Tempo tracking is validated on synthetic transients; real guitar may need threshold tuning (the ANALYSIS tab and Calibration are there for that).
* Analysis runs on the audio thread (cheap and allocation-free, but not a separate worker as in SPEC 33's suggested layout).
* Changing the number of Jam Style choices (6 → 22) means old *automation lanes* of that parameter map to different styles; saved sessions are unaffected.
