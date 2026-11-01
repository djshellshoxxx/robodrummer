# Developer Guide

## Core build and tests (no JUCE)

Run from the repository root with CMake 3.22 or newer and a C++20 compiler:

```sh
cmake -S . -B build-core -DCMAKE_BUILD_TYPE=Release -DROBODRUMMER_BUILD_JUCE=OFF
cmake --build build-core --config Release --parallel
ctest --test-dir build-core -C Release --output-on-failure --parallel 4
```

Core tests are discovered from `tests/test_*.cpp`. Counts grow with the source; record results against the tested commit in [QA](../QA.md) rather than treating an old count as current evidence.

## JUCE targets

`ROBODRUMMER_BUILD_JUCE` defaults to ON and enables VST3, Standalone and, on macOS, AU. `ROBODRUMMER_BUILD_CLAP` also defaults to ON and adds CLAP when JUCE is enabled. The native integration target is `RoboDrummerHarness`. See [BUILDING.md](../BUILDING.md) and [JUCE_BUILD.md](../JUCE_BUILD.md) for dependencies, target commands and outputs.

## Automated coverage

The [PR/push workflow](../../.github/workflows/core-ci.yml) defines:

| Gate | Coverage |
| --- | --- |
| Portable core | CMake/CTest on Linux, Windows and macOS; Python script regressions on Linux |
| Native Linux | VST3, CLAP, Standalone, real processor harness, pinned historical default-output comparison, generated parameter-reference comparison and VST3 pluginval at strictness 10 |
| Native Windows x64 | VST3, CLAP, Standalone, core suite, real processor harness, historical/default and parameter-reference comparisons, VST3 pluginval at strictness 10 |
| Native macOS arm64 and x86_64 | VST3, CLAP, Standalone, AU, real processor harness and compatibility checks, architecture and microphone-permission checks, AU discovery and strict auval validation |

The [release build workflow](../../.github/workflows/release-build.yml) repeats required core/native gates before packaging. Workflow definitions describe configured coverage; [QA](../QA.md) records what actually passed for a given source revision. Hosted-runner checks do not establish manual DAW, device, clean-machine or screen-reader acceptance.

## Rules

- Keep portable algorithms independent of JUCE so timing, groove and MIDI logic remain directly testable; see the [code map](Codebase-Map.md).
- The real-time audio callback must perform **no allocation or deallocation**, locking, waiting, file/network I/O, logging, GUI calls or unbounded work. Prepare buffers and state before processing and release resources outside the callback. Cross-thread handoffs need explicit ownership and synchronization contracts. This is the contract in [AGENTS.md](../../AGENTS.md) and [CDL baseline section 3.2](../standards/CDL_PLUGIN_BASELINE.md#32-real-time-safety).
- Read the [specifications](../specs/) before changing behavior. Preserve parameter IDs, formats and ordinary output; keep behavior-preserving refactors in separate small PRs and merge only passing, independently reviewed changes under [AGENTS.md](../../AGENTS.md).
