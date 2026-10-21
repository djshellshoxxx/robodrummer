#!/usr/bin/env bash
# Run the headless harness (if the target exists) and pluginval once, under xvfb. Linux only.
# usage: juce_checks.sh <build_dir>
set -euo pipefail
build="${1:?build dir}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
pv_version="v1.0.3"

if grep -q 'RoboDrummerHarness' "$root/CMakeLists.txt"; then
  harness="$(find "$build" -type f -name RoboDrummerHarness -perm -u+x | head -n1)"
  if [ -z "$harness" ]; then echo "::error::RoboDrummerHarness target exists but binary not found"; exit 1; fi
  echo "Running harness: $harness"
  xvfb-run -a "$harness"
  # docs/PARAMETERS.md is generated from the real parameter layout; fail when it has drifted.
  params_out="$(mktemp)"
  xvfb-run -a "$harness" --dump-parameters "$params_out"
  if ! diff -u "$root/docs/PARAMETERS.md" "$params_out"; then
    echo "::error::docs/PARAMETERS.md is out of date. Regenerate: RoboDrummerHarness --dump-parameters docs/PARAMETERS.md"
    exit 1
  fi
else
  echo "No RoboDrummerHarness target in CMakeLists.txt; skipping harness."
fi

tmp="$(mktemp -d)"
curl -fsSL -o "$tmp/pluginval.zip" "https://github.com/Tracktion/pluginval/releases/download/$pv_version/pluginval_Linux.zip"
unzip -q "$tmp/pluginval.zip" -d "$tmp"
chmod +x "$tmp/pluginval"
vst3="$build/RoboDrummer_artefacts/Release/VST3/RoboDrummer.vst3"
if [ ! -e "$vst3" ]; then echo "::error::Missing $vst3"; exit 1; fi
xvfb-run -a "$tmp/pluginval" --strictness-level 10 --validate-in-process --output-dir "$tmp/out" "$vst3"
