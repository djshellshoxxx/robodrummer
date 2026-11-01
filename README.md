# RoboDrummer

RoboDrummer is a real-time adaptive virtual drummer designed to jam with a single live guitarist.

CMake provides VST3, CLAP and Standalone targets, plus AU on macOS. Native CI builds these formats and runs platform validators; observed results and remaining host qualification are recorded in [docs/QA.md](docs/QA.md). RoboDrummer can lead at a fixed or host tempo, follow a guitarist's changing tempo and beat, or operate in a hybrid mode where the drummer provides stability while allowing the guitarist to push and pull the time.

The design target is roughly 90% autonomous performance with the remaining 10% exposed as optional MIDI intervention: fills, next section, intensity changes, half/double time, crash, break, stop/resume, and listening reset.

## Current development status (0.4.0)

The source version is 0.4.0. The editor has nine tabs: LIVE, KIT, DRUMMER, STYLE, SONG, PRACTICE, MIDI, ANALYSIS, SETTINGS. Features include Learn My Playing with per-field Apply and Undo, Transient Shaper and Drum Bus controls, Riff Lock and Playability, count-in/loop/tempo-trainer practice, automatic meter selection, and MIDI capture export. Parameter controls support reset/copy/paste menus and MIDI Learn where applicable. SETTINGS includes Reduce motion and High contrast; Help/About is generated from [docs/HELP.md](docs/HELP.md).

By default, MIDI **note numbers 51–55 on channel 16** control Practice on/off, Loop on/off, Trainer on/off, Trainer restart and Count-in on/off. These bindings can be learned again on the MIDI tab; see the [wiki user guide](docs/wiki/User-Guide.md).

Implementation and qualification are tracked separately. See [docs/QA.md](docs/QA.md) for dated test evidence and remaining manual/platform checks, [docs/COMPLIANCE.md](docs/COMPLIANCE.md) for the shared standard, and [docs/specs](docs/specs) for implemented, deferred and gated work. This version number does not establish BETA READY or RELEASE READY status.

## Previous status (0.3.0)

The 0.3.0 baseline introduced 22 styles, humanization, 1-beat to 2-bar fills, drummer personalities, a phase-locked tempo/beat tracker with movement classification, input conditioning and calibration, tap tempo and MIDI Clock, user WAV/AIFF kits, mixer with room and aux outputs, presets, MIDI Learn, and a tabbed editor. Historical traceability and findings are in [the 0.4.0 feature audit](docs/feature-audit/FEATURE_AUDIT_0.4.0.md) and [the 0.3.0 feature audit](docs/feature-audit/FEATURE_AUDIT_2026-10-14.md). Current validation is in [docs/QA.md](docs/QA.md); every host parameter is listed in [docs/PARAMETERS.md](docs/PARAMETERS.md).

Windows users: the installer is named `RoboDrummer-<version>-windows-x64-setup.exe`; a zip is also packaged. See [docs/INSTALL.md](docs/INSTALL.md) for available build artifacts and installation.

### Earlier phases

Phase 1 established the JUCE-independent C++ core and tests. Phase 2 added the JUCE VST3/Standalone shell, generated starter kit, host transport integration, MIDI intervention, MIDI drum output and the first playable drummer path.

The current engine includes a causal guitar onset detector, competing tempo hypotheses, predictive beat phase, confidence tracking, Drummer Leads / Hybrid / Guitarist Leads timing authority, bounded phase correction and confidence-gated adaptive entry. In adaptive modes RoboDrummer waits for a reliable lock and enters on a beat boundary. A separate resynchronization planner handles persistent large phase disagreements.

See [docs/JUCE_BUILD.md](docs/JUCE_BUILD.md) for build instructions and the current MIDI map.

## Build and test

The portable core builds and tests without JUCE. Run these commands from the repository root:

```sh
cmake -S . -B build-core -DCMAKE_BUILD_TYPE=Release -DROBODRUMMER_BUILD_JUCE=OFF
cmake --build build-core --config Release --parallel
ctest --test-dir build-core -C Release --output-on-failure --parallel 4
```

The VST3, CLAP, Standalone and macOS AU targets need JUCE; see [docs/BUILDING.md](docs/BUILDING.md), [docs/JUCE_BUILD.md](docs/JUCE_BUILD.md) and the [wiki developer guide](docs/wiki/Developer-Guide.md). Installation for users is in [docs/INSTALL.md](docs/INSTALL.md). Test counts and host results belong to the dated evidence in [docs/QA.md](docs/QA.md).

## Licence and wiki

Proprietary licence: see [LICENSE](LICENSE). Copyright 2026 Sheldon Davidson. Wiki source pages are in [docs/wiki/](docs/wiki/Home.md).

## Project documents

- [SPEC.md](SPEC.md) — engineering/product specification
- [docs/PHASE2_SCOPE.md](docs/PHASE2_SCOPE.md) — JUCE shell scope
- [docs/JUCE_BUILD.md](docs/JUCE_BUILD.md) — build instructions and current behavior
- [docs/PARAMETERS.md](docs/PARAMETERS.md) — generated parameter reference
- [docs/QA.md](docs/QA.md) — validation record and known limitations
- [docs/COMPLIANCE.md](docs/COMPLIANCE.md) — CDL standard compliance table
- [docs/feature-audit/FEATURE_AUDIT_0.4.0.md](docs/feature-audit/FEATURE_AUDIT_0.4.0.md) — 0.4.0 traceability matrix, findings and fixes
- [docs/feature-audit/FEATURE_AUDIT_2026-10-14.md](docs/feature-audit/FEATURE_AUDIT_2026-10-14.md) — 0.3.0 feature audit and backlog
- [docs/RELEASE_NOTES_0.4.0.md](docs/RELEASE_NOTES_0.4.0.md) — what changed in 0.4.0
- [docs/RELEASE_NOTES_0.3.0.md](docs/RELEASE_NOTES_0.3.0.md) — what changed
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


## Required shared plug-in standard

This project follows the [Circuit Drift Labs Shared Audio Plugin Standard](docs/standards/CDL_PLUGIN_BASELINE.md). It is required for the plug-in target; standalone-only requirements apply only when a standalone target is included. The product-specific specification supplements the shared standard and records the applicable profiles, compliance status, and any exceptions.
