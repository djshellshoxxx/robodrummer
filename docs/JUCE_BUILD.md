# RoboDrummer JUCE Build

RoboDrummer uses JUCE 9.0.3 through CMake FetchContent. The JUCE shell is deliberately thin: musical logic, timing, groove generation and tests remain in JUCE-independent C++ where practical.

## Requirements

- CMake 3.22 or newer
- C++20 compiler
- Git
- Windows: Visual Studio with Desktop C++ workload
- Linux: ALSA/X11/Freetype/WebKit development packages used by JUCE

## Core-only build

```bash
cmake -S . -B build-core -DROBODRUMMER_BUILD_JUCE=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-core --config Release
ctest --test-dir build-core -C Release --output-on-failure
```

## VST3 + Standalone

```bash
cmake -S . -B build -DROBODRUMMER_BUILD_JUCE=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target RoboDrummer_VST3 RoboDrummer_Standalone
```

CMake downloads the pinned JUCE release during configuration.

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
- Fill
- Reset Listening

## MIDI intervention map

RoboDrummer treats MIDI channel 16 as its intervention/control channel. Generated drum MIDI is sent on General MIDI drum channel 10.

| Note | Action |
|---:|---|
| 36 | Fill request |
| 37 | Next section |
| 38 | Previous section |
| 39 | Crash |
| 40 | Intensity up |
| 41 | Intensity down |
| 42 | Half-time |
| 43 | Double-time |
| 44 | Break (one musical bar, then automatic groove re-entry) |
| 45 | Stop drummer |
| 46 | Resume drummer |
| 47 | Reset listening |

Break is implemented as a one-bar musical dropout. The request is consumed once, normal groove generation is suppressed for that bar, and RoboDrummer re-enters automatically on the following bar. If a fill is already queued, it is deferred until after the break rather than discarded.

Not every higher-level command has a complete arrangement behavior yet; the command model is in place so those behaviors can be added without changing the MIDI contract.

## Real-time notes

The audio thread does not perform disk I/O or network I/O. The current generated starter kit is created during `prepareToPlay`. The event scheduler, sampler trigger path, timing-authority controller and phase follower use fixed/preallocated state in the processing path.

## VST3 compatibility

RoboDrummer has never shipped a VST2 build. `VST3_CAN_REPLACE_VST2` is therefore disabled in the CMake target, avoiding JUCE's VST2/VST3 parameter-compatibility guard and making the VST3 identity explicit.
