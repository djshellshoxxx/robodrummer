# Building RoboDrummer Phase 1

## Requirements

- CMake 3.22 or newer
- A C++20 compiler

No JUCE installation is required for the Phase 1 portable core.

## Configure and build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## Run tests

```bash
ctest --test-dir build -C Release --output-on-failure
```

The initial suite covers the musical clock, fixed-capacity event scheduler, deterministic groove generation and MIDI intervention state.

## Development rule

Core musical behavior should remain independently testable without a DAW. The upcoming JUCE wrapper should translate host audio/MIDI/transport data into these core contracts rather than moving musical logic into `AudioProcessor` or GUI classes.
