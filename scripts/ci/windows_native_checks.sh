#!/usr/bin/env bash
# Native PR qualification; shares the release build's target and validator policy.
# Run after configuring a Windows JUCE build. No packaging or release writes.
set -euo pipefail
build="${1:?usage: windows_native_checks.sh configured-build-dir}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
parallel="${CMAKE_BUILD_PARALLEL_LEVEL:-3}"
target_names="$(bash "$root/scripts/ci/targets.sh" windows)"
read -ra targets <<< "$target_names"
cmake --build "$build" --config Release --parallel "$parallel" --target "${targets[@]}" RoboDrummerHarness
# The default build includes every auto-discovered portable core regression.
cmake --build "$build" --config Release --parallel "$parallel"
mkdir -p "$build/qa"
# juce_checks runs the real processor fixture and exact historical comparison
# once. Exclude only those two CTests here, then run its blocking pluginval10.
ctest --test-dir "$build" -C Release --output-on-failure --parallel "$parallel" \
  -E '^(plugin_harness|default_output_reference)$' | tee "$build/qa/core-tests.txt"
bash "$root/scripts/ci/juce_checks.sh" "$build" windows
