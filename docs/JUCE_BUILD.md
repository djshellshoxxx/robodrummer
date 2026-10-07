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

All main controls are host-automatable parameters (stable IDs, version 1). The GUI uses JUCE parameter attachments, so host automation, generic host editors and the RoboDrummer editor always agree.

| ID | Name | Range / choices | Default |
|---|---|---|---|
| `bpm` | Internal BPM | 40–240 BPM | 120 |
| `intensity` | Intensity | 0–100 % | 50 % |
| `outputMode` | Output Mode | Internal Drums / MIDI Only / Internal + MIDI | Internal + MIDI |
| `leadershipMode` | Timing Mode | Drummer Leads / Hybrid / Guitarist Leads | Drummer Leads |
| `leadership` | Leadership | 0–100 % | 50 % |
| `followRange` | Follow Range | 0–80 BPM | 15 |
| `dynamicFollow` | Dynamic Follow | 0–100 % | 60 % |
| `jamStyle` | Jam Style | Rock / Blues / Funk / Punk / Metal / Shuffle | Rock |
| `arrangementEnabled` | Programmed Arrangement | off / on | off |
| `jamMemoryEnabled` | Learn This Jam | off / on | on |
| `manualMeterEnabled` | Manual Meter | off / on | off |
| `meterNumerator` | Meter Numerator | 2–12 | 4 |
| `meterDenominator` | Meter Denominator | 2 / 4 / 8 / 16 | 4 |
| `silenceMode` | Guitar Silence | Keep playing / Reduce intensity / Hold groove / Fill during silence / Stop after bars / Wait for resume | Keep playing |
| `silenceStopBars` | Silence Stop Bars | 1–16 bars | 2 |

Intensity and Jam Style can also be changed by the audio thread (MIDI intensity notes, programmed arrangement sections). Those changes are published back to the host from the message thread, so automation lanes and saved state follow what is playing. Arrangement slot contents are non-parameter state saved alongside the parameters. Saved state carries `stateVersion` 2; version-1 sessions (plain properties on a `RoboDrummerState` root) are migrated on load.

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

RoboDrummer treats MIDI channel 16 as its intervention/control channel. Channel-16 messages are consumed and not forwarded to the MIDI output; all other incoming MIDI passes through. Generated drum MIDI is sent on General MIDI drum channel 10.

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
| 49 | End jam cue |

Break is implemented as a one-bar musical dropout. The request is consumed once, normal groove generation is suppressed for that bar, and RoboDrummer re-enters automatically on the following bar. If a fill is already queued, it is deferred until after the break rather than discarded.

MIDI intensity up/down updates the persistent live intensity control, so the change remains active across subsequent processing blocks and is reflected in the editor.

Not every higher-level command has a complete arrangement behavior yet; the command model is in place so those behaviors can be added without changing the MIDI contract.

## Internal sampler status

The core sampler now supports velocity layers, up to four round-robin variants per layer, per-instrument choke groups, gain/pan, tuning from -24 to +24 semitones, and basic attack/release envelopes. Sample configuration occurs outside the audio callback; trigger and render paths use fixed voice/layer storage and do not allocate. The generated starter kit uses a shared choke group for open and closed hi-hats.

## Real-time notes

The audio thread does not perform disk I/O or network I/O. The current generated starter kit is created during `prepareToPlay`. The event scheduler, sampler trigger path, timing-authority controller and phase follower use fixed/preallocated state in the processing path.

## VST3 compatibility

RoboDrummer has never shipped a VST2 build. `VST3_CAN_REPLACE_VST2` is therefore disabled in the CMake target, avoiding JUCE's VST2/VST3 parameter-compatibility guard and making the VST3 identity explicit.
