# RoboDrummer JUCE Build

RoboDrummer uses JUCE 9.0.3 and free-audio/clap-juce-extensions (pinned commit) through CMake FetchContent. The JUCE shell is deliberately thin: musical logic, timing, groove generation and tests remain in JUCE-independent C++ where practical.

## Requirements

- CMake 3.22 or newer
- C++20 compiler
- Git
- Windows: Visual Studio with Desktop C++ workload
- Linux: ALSA/X11 (including libxi-dev)/Freetype development packages used by JUCE

## Core-only build

```bash
cmake -S . -B build-core -DROBODRUMMER_BUILD_JUCE=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-core --config Release
ctest --test-dir build-core -C Release --output-on-failure
```

## VST3 + CLAP + Standalone

```bash
cmake -S . -B build -DROBODRUMMER_BUILD_JUCE=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target RoboDrummer_VST3 RoboDrummer_CLAP RoboDrummer_Standalone
```

CMake downloads the pinned JUCE release and clap-juce-extensions during configuration. Pass `-DROBODRUMMER_BUILD_CLAP=OFF` to skip CLAP. Outputs land in `build/RoboDrummer_artefacts/Release/{VST3,CLAP,Standalone}`.

## Host parameters

Every main control is a host-automatable parameter with a stable ID. The complete, generated table (IDs, ranges, choices, defaults) is in [PARAMETERS.md](PARAMETERS.md); regenerate it with `RoboDrummerHarness --dump-parameters docs/PARAMETERS.md`. Parameters are only ever appended; an ID is never reused for a different meaning.

Intensity and Jam Style can also be changed by the audio thread (MIDI intensity notes, programmed sections). Those changes are published back to the host from the message thread. Non-parameter state saved with the session (state version 4): arrangement slots and names, MIDI learn map, MIDI drum map and the user-kit folder path. Versions 1, 2 and 3 are migrated on load: parameters a session does not contain (everything added in 0.4.0) are reset to their defaults, so an old session sounds exactly as before. A running practice session is never restored (the Practice settings are).

### Added in 0.4.0 (all off or neutral by default)

* **Drum bus compressor and transient shaper** (`drumBus*`, `transientShaper*`): processed on the main drum mix only; the room return and the aux stems stay dry. Latency is reported to the host.
* **Riff Lock** (`rlAmount`, `rlSensitivity`, `rlAccentCrash`): the kick follows the repeated guitar riff.
* **Practice mode** (`practiceEnabled`, `pracCountInLen`, `pracCountInSound`, `pracCountInStartOnly`, `pracLoopOn`, `pracLoopMode`, `pracLoopBars`, `pracLoopSecFirst`, `pracLoopSecLast`, `pracTrain*`): count-in, bar or section loop, tempo trainer. The trainer sets the base tempo only when the timing authority allows (Drummer Leads with the internal tempo, or Hybrid with leadership at or below 50%); with a host or MIDI-clock tempo, or when the guitarist leads, it is only shown as a hint. Section numbers are positions among the enabled arrangement sections.
* **MIDI capture** (SMF export of what the drummer played, including the drum map) and drag-and-drop temp files in `<system temp>/RoboDrummer/<instance>/`; stale files from earlier sessions are removed at start-up and when an instance closes.
* **Auto Setup** ("Learn My Playing"): proposals only; nothing changes until Apply, and Undo restores exactly the applied fields.
* **Auto meter** (`autoMeterEnabled`, `autoMeterThreshold`) and **Playability** (`limbModelAmount`).

### Editor (0.4.0)

Nine tabs (LIVE, KIT, DRUMMER, STYLE, SONG, PRACTICE, MIDI, ANALYSIS, SETTINGS), minimum size 960×680. The in-plug-in Help is generated at build time from `docs/HELP.md` (`juce_add_binary_data`, namespace `rdhelp`). Per-user interface options live in `<userApplicationData>/Circuit Drift Labs/RoboDrummer/settings.xml`. `RoboDrummerHarness --ui-only` runs only the editor checks; `--screenshots <dir>` renders every tab and the Learn My Playing, Help, tour, focus-ring and high-contrast states at the default and minimum sizes.

## Targets

VST3, CLAP and Standalone on Windows, macOS and Linux, plus AU on macOS. `RoboDrummerHarness` is a headless console test of the real processor (`ctest` runs it as `plugin_harness`).

Release builds run the core tests and processor harness on every native target,
pluginval strictness 10 on Windows/Linux, and auval on both macOS architectures.
Run `bash scripts/ci/juce_checks.sh build linux` after building the harness and
plug-ins (use `windows` or `mac` on those systems). Linux requires Xvfb.
`python3 -B -m unittest discover -s tests -p test_release_pipeline.py -v` checks
the release scripts without network access or publication.

Only a complete four-architecture matrix creates a new draft release. A subset
workflow dispatch builds downloadable CI artifacts without creating a release.
Existing releases cannot be overwritten; use a new beta/rc or patch version.
The qualification record is [RELEASE_QA_2026-10-22.md](feature-audit/RELEASE_QA_2026-10-22.md).

## Tempo sources

`tempoSource`: **Host / Internal** (host tempo and transport when available, Internal BPM otherwise), **Internal BPM only** (free-running, ignores the host transport), **MIDI Clock** (24 ppqn with Start/Stop/Continue; falls back to the host/internal tempo when no clock is present). Tap tempo sets Internal BPM.

## Current timing modes

### Drummer Leads

Host BPM and PPQ are authoritative when available. Internal BPM is used as the fallback. Guitar analysis continues to run for telemetry but does not move the clock.

### Hybrid

The plugin blends trusted guitar tempo toward the host/internal base tempo according to Leadership, tracker confidence and Follow Range. Phase correction is bounded and gradual so normal timing drift does not create sudden drum jumps.

### Guitarist Leads

The guitar receives full requested timing authority, still subject to confidence gating and Follow Range. RoboDrummer remains silent while acquiring a reliable timing lock, then enters on a detected beat boundary.

Large phase disagreement is currently flagged rather than immediately corrected. A later stage will turn that flag into a musical hard-resync action such as a short fill/break and re-entry.

## Current live controls

- Internal BPM
- Intensity
- Timing mode: Drummer Leads / Hybrid / Guitarist Leads
- Leadership
- Follow Range
- detected guitar BPM
- tempo confidence
- beat confidence
- tracker lock state
- effective guitar authority
- Performance buttons: Fill, Crash, Break, Half-time, Double-time, Stop, Resume, Previous/Next Section, Solo, End Jam, Reset Listening (each mirrors the matching channel-16 MIDI note)
- Output mode: Internal Drums / MIDI Only / Internal + MIDI

## MIDI intervention map

RoboDrummer treats MIDI channel 16 as its intervention/control channel. Channel-16 messages are consumed and not forwarded to the MIDI output; all other incoming MIDI passes through. Generated drum MIDI is sent on the *MIDI Output Channel* parameter (default 10, General MIDI drums) using an editable drum map (SETTINGS → EDIT DRUM MAP). Every command note, and every controller → parameter assignment, can be re-learned on the MIDI tab; learned notes fire only on the channel they were learned on. Defaults: CC 11 → Intensity, CC 12 → Leadership. All Sound Off / All Notes Off silence the sampler.

| Note | Action |
|---:|---|
| 36 | Fill request |
| 37 | Next section |
| 38 | Previous section |
| 39 | Crash |
| 40 | Intensity up |
| 41 | Intensity down |
| 42 | Half-time (toggle; again returns to normal time) |
| 43 | Double-time (toggle; again returns to normal time) |
| 44 | Break (one musical bar, then automatic groove re-entry) |
| 45 | Stop drummer |
| 46 | Resume drummer |
| 47 | Reset listening (clears tempo, phase, phrase, silence, coordinator and jam-memory tracking) |
| 48 | Solo support cue |
| 49 | End jam: plays an outro (fill into a final crash + kick on a downbeat), then stops; Resume restarts |
| 50 | Immediate fill (starts on the next beat; note 36 queues the fill instead) |
| 51 | Practice mode on/off (starts or ends a practice session; a session always starts with its count-in) |
| 52 | Practice loop on/off (arms at the next bar line; again releases the loop) |
| 53 | Tempo trainer on/off |
| 54 | Tempo trainer restart (back to the start tempo) |
| 55 | Count-in on/off (switches between Off and the last count-in length, 1 bar by default) |

Notes 51-55 flip the matching Practice parameters on the message thread (within about 30 ms), so the host and the editor always show the real state. The practice count-in sounds as a cowbell ("Click") or side stick ("Sticks") on the drum output and the MIDI output; count-in clicks are never written into the MIDI capture buffer.

Break is implemented as a one-bar musical dropout. The request is consumed once, normal groove generation is suppressed for that bar, and RoboDrummer re-enters automatically on the following bar. If a fill is already queued, it is deferred until after the break rather than discarded.

MIDI intensity up/down updates the persistent live intensity control, so the change remains active across subsequent processing blocks and is reflected in the editor.

Not every higher-level command has a complete arrangement behavior yet; the command model is in place so those behaviors can be added without changing the MIDI contract.

## Kits, mixer and outputs

Built-in kits: Studio, Tight, Big Room, Lo-Fi (synthesised at load). **User kits:** point the KIT tab at a folder of WAV/AIFF/FLAC files named `kick`, `snare`, `closedhat`, `openhat`, `ride`, `crash`, `tom1`/`hightom`, `tom2`/`midtom`, `floortom`, `rimshot`, `sidestick`, `china`, `splash`, `cowbell`, `tambourine`, `clap`; add `_v1`…`_v4` for velocity layers and `_rr1`…`_rr4` for round robin (e.g. `snare_v2_rr3.wav`). Missing instruments fall back to the Studio kit; files are resampled to the host rate on a background thread and swapped in without blocking the audio thread.

Mixer channels (Kick, Snare, Toms, Hi-Hat, Ride, Cymbals, Room, Master) have gain, pan, mute and (except Room/Master) solo. The Room channel is a reverb fed by every voice, whatever its routing. Enable the **Kick / Snare / Toms / Hats + Cymbals** output buses in the host to receive each group separately; a disabled bus falls back to the main mix.

## Internal sampler status

The core sampler now supports velocity layers, up to four round-robin variants per layer, per-instrument choke groups, gain/pan, tuning from -24 to +24 semitones, and basic attack/release envelopes. Sample configuration occurs outside the audio callback; trigger and render paths use fixed voice/layer storage and do not allocate. The generated starter kit uses a shared choke group for open and closed hi-hats.

## Real-time notes

The audio thread does not perform disk I/O or network I/O. MIDI capture only copies note events into a preallocated ring; exporting, temp-file writing and the start-up sweep run on the message thread. Auto Setup and the practice parameter toggles from MIDI commands are finalized or applied on the message thread. The current generated starter kit is created during `prepareToPlay`. The event scheduler, sampler trigger path, timing-authority controller and phase follower use fixed/preallocated state in the processing path.

## VST3 compatibility

RoboDrummer has never shipped a VST2 build. `VST3_CAN_REPLACE_VST2` is therefore disabled in the CMake target, avoiding JUCE's VST2/VST3 parameter-compatibility guard and making the VST3 identity explicit.
