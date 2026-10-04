# RoboDrummer JUCE Build

RoboDrummer Phase 2 builds with CMake and JUCE 9.0.3.

## Requirements

- CMake 3.22 or newer
- C++20 compiler
- Git (CMake FetchContent downloads JUCE)
- Windows: Visual Studio 2022/2026 with Desktop C++ workload
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

## Current Phase 2 behavior

The plugin currently provides:

- VST3 and Standalone targets
- stereo guitar/analysis input and stereo mixed output
- internal tempo fallback when the host does not expose tempo
- host tempo and time-signature following when available
- generated starter kick/snare/hat/crash sounds so the plugin works without external sample assets
- basic-rock procedural groove generation
- intensity control
- MIDI intervention notes 36–47
- generated General MIDI drum output on MIDI channel 10
- persistent BPM/intensity state
- basic Live GUI

The incoming guitar signal is currently passed through unchanged and reserved for the next phase's analysis engine. Phase 2 does not yet infer guitar tempo, beats or musical sections.

## MIDI intervention map

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
| 44 | Break |
| 45 | Stop drummer |
| 46 | Resume drummer |
| 47 | Reset listening |

The control map is deliberately compact because normal performance is intended to remain mostly automatic.
