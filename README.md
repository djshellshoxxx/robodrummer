# RoboDrummer

RoboDrummer is a real-time adaptive virtual drummer designed to jam with a single live guitarist.

The intended product is a VST3/CLAP/AU instrument plus a standalone application. It can either lead at a fixed/host tempo, follow a guitarist's changing tempo and beat, or operate in a hybrid mode where the drummer provides stability while allowing the guitarist to push and pull the time.

The design target is roughly 90% autonomous performance with the remaining 10% exposed as optional MIDI intervention: fills, next section, intensity changes, half/double time, crash, break, stop/resume, and listening reset.

## Current development status

Phase 1 established the JUCE-independent C++ core and tests. Phase 2 is now integrating that core into a JUCE VST3 and Standalone application.

The Phase 2 shell includes host tempo/meter intake, MIDI intervention mapping, deterministic block scheduling, an internal fixed-voice drum player, generated starter drum sounds, generated MIDI drum output, state persistence, and a minimal live GUI. The incoming guitar input is preserved for Phase 3, where causal onset/beat/tempo analysis will be added.

See [docs/JUCE_BUILD.md](docs/JUCE_BUILD.md) for build instructions and the current MIDI map.

## Project documents

- [SPEC.md](SPEC.md) — engineering/product specification
- [docs/PHASE2_SCOPE.md](docs/PHASE2_SCOPE.md) — JUCE shell scope
- [docs/JUCE_BUILD.md](docs/JUCE_BUILD.md) — build instructions and Phase 2 behavior
- [research/jam-session-behavior.md](research/jam-session-behavior.md) — improvisation, entrainment, leader/follower behavior, tempo drift, turn-taking and implications for the engine
- [research/genre-tempo-drumming.md](research/genre-tempo-drumming.md) — groove, genre, tempo, drum-pattern and song-form research
- [research/realtime-tracking.md](research/realtime-tracking.md) — causal beat/downbeat/tempo tracking research and proposed tracking architecture
- [research/open-source-prior-art.md](research/open-source-prior-art.md) — open-source projects relevant to onset, beat, downbeat, meter and generative groove work
- [research/bibliography.md](research/bibliography.md) — academic and technical source list

## Core idea

The guitarist should be able to choose a kit and musical style, press **Listen**, play naturally, and have RoboDrummer establish a groove, join on a musical boundary, follow reasonable tempo and energy changes, make stylistically appropriate variations and fills, and recover gracefully when the input becomes ambiguous.

RoboDrummer intentionally does **not** attempt multi-guitarist source tracking in the current specification.

## Research status

Initial literature and prior-art research was added on 2026-10-04. The most important finding is that RoboDrummer should treat short-timescale beat synchronization and longer-timescale musical coordination as separate problems. The first layer tracks pulse/phase; the second layer reasons about phrase boundaries, energy, turn-taking, transitions and style.
