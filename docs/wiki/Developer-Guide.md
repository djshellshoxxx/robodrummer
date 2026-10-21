# Developer Guide

## Core build and tests (no JUCE)

```sh
cmake -S . -B build-core -DCMAKE_BUILD_TYPE=Release -DROBODRUMMER_BUILD_JUCE=OFF
cmake --build build-core --config Release --parallel
ctest --test-dir build-core -C Release --output-on-failure --parallel 4
```

Checked on this snapshot: configure and build succeed; 53 of 53 tests pass.

## JUCE targets

`ROBODRUMMER_BUILD_JUCE` (default ON) builds the VST3 and Standalone targets; `ROBODRUMMER_BUILD_CLAP` adds CLAP. See `docs/BUILDING.md` and `docs/JUCE_BUILD.md`. CI (`.github/workflows/core-ci.yml`) runs the core build and tests; `release-build.yml` builds packages. The JUCE targets were not built in this pass.

## Rules

- The portable core has no JUCE dependency. Keep it that way so timing, groove and MIDI logic stay testable.
- The audio callback must not touch disk, network, block or allocate without bound (`docs/ARCHITECTURE.md`).
- Specs live in `docs/specs/`; check them before changing behaviour. `AGENTS.md` has the contributor rules.
