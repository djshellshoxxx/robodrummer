# MIDI capture robustness audit — 2026-10-22

Base: main at `b1ce9f794484ec991de24b0715938fb692ed41a4`. Scope: capture velocity conversion and producer/consumer take tracking, with the complete portable core regression suite. This is a focused follow-up, not a replacement for the full 0.4.0 feature audit.

## Confirmed defects and fixes

1. A maximum finite float velocity exported as MIDI velocity 1 rather than 127. Multiplication and integer rounding happened before clamping. Clamp the finite float to 0..1 first; retain the established NaN/Inf fallback of 64. Regression covers both signs of the maximum finite float.
2. Clear before drain discarded segment records while the consumer inferred take sequence by counting them. The packed latest-position sequence then no longer matched: a take expected to end at tick 3840 ended at 1920. Segment records now explicitly encode the same 24-bit producer sequence in their previously unused bytes. Regressions cover clear before drain, trailing silent blocks and 20 consecutive clears.

The ring record remains 24 bytes. Neither change adds allocation, locks, I/O or a public API/preset/state-format change. The take sequence is transient and is not serialized.

## Verification

- Both new regressions failed before their corresponding fixes.
- Release build, GCC 13.3, C++20: all 53 core tests pass after both fixes (3.09 seconds with four test jobs).
- Existing capture tests include concurrent producer/consumer stress, overflow recovery, tempo/meter changes, export parsing, retention limits and 1.002 million events with no drops.
- Debug build with AddressSanitizer + UndefinedBehaviorSanitizer: 53/53 pass (414.43 seconds). Leak detection was disabled with `ASAN_OPTIONS=detect_leaks=0`: the initial run failed because LeakSanitizer cannot inspect `/proc` in this environment. No memory-leak pass is claimed.
- `git diff --check`: clean.

## Outstanding product validation

The existing 0.4.0 audit lists implemented product features and separately records unexecuted real-guitar corpus, DAW host matrix, signing/notarization and platform validation. Those gaps remain open. Learned models and transient enhancement remain gated decisions in their existing specs. No product completion or release-ready claim is made here.

JUCE plug-in/harness and pluginval were not executed in this environment: required Linux development packages are absent and the package installation failed on filesystem permissions. Windows/macOS execution requires CI or the corresponding systems.

## Next code-audit investigation

`PluginProcessor::swapInPendingKit` replaces the active sampler before attempting to retire its previous pointer into four slots. Code inspection shows the previous pointer has no owner if every retirement slot is full. Reproduce that saturation path in the JUCE harness and preserve the incoming kit until retirement capacity is available; do not delete a sampler on the audio thread. This suspected ownership defect is outside the capture fixes and remains open.
