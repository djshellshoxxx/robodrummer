#!/usr/bin/env bash
# Run the processor harness and native format validation before packaging.
# usage: juce_checks.sh <build_dir> [linux|windows|mac]
set -euo pipefail
build="${1:?build dir}"
platform="${2:-linux}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
pv_version="v1.0.3"
build="$(cd "$build" && pwd)"
mkdir -p "$build/qa"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

run_gui() {
  if [ "$platform" = linux ]; then xvfb-run -a "$@"; else "$@"; fi
}
case "$platform" in
  linux) suffix=""; pv_archive="pluginval_Linux.zip" ;;
  windows) suffix=".exe"; pv_archive="pluginval_Windows.zip" ;;
  mac) suffix="" ;;
  *) echo "::error::Unsupported validation platform: $platform"; exit 1 ;;
esac

if grep -q 'RoboDrummerHarness' "$root/CMakeLists.txt"; then
  harness="$build/RoboDrummerHarness_artefacts/Release/RoboDrummerHarness$suffix"
  if [ ! -f "$harness" ]; then echo "::error::RoboDrummerHarness target exists but binary not found: $harness"; exit 1; fi
  echo "Running harness: $harness"
  run_gui "$harness" | tee "$build/qa/harness.txt"
  # docs/PARAMETERS.md is generated from the real parameter layout; fail when it has drifted.
  params_out="$tmp/parameters.md"
  run_gui "$harness" --dump-parameters "$params_out"
  if ! diff -u "$root/docs/PARAMETERS.md" "$params_out"; then
    echo "::error::docs/PARAMETERS.md is out of date. Regenerate: RoboDrummerHarness --dump-parameters docs/PARAMETERS.md"
    exit 1
  fi
else
  echo "::error::Required RoboDrummerHarness target missing from CMakeLists.txt"
  exit 1
fi

if [ "$platform" = mac ]; then
  # Catch wrong-architecture cache/output reuse before registering an Audio Unit.
  native_arch="$(uname -m)"
  for binary in \
    "$build/RoboDrummer_artefacts/Release/VST3/RoboDrummer.vst3/Contents/MacOS/RoboDrummer" \
    "$build/RoboDrummer_artefacts/Release/CLAP/RoboDrummer.clap/Contents/MacOS/RoboDrummer" \
    "$build/RoboDrummer_artefacts/Release/AU/RoboDrummer.component/Contents/MacOS/RoboDrummer" \
    "$build/RoboDrummer_artefacts/Release/Standalone/RoboDrummer.app/Contents/MacOS/RoboDrummer"; do
    lipo -verify_arch "$native_arch" "$binary"
  done
  plist="$build/RoboDrummer_artefacts/Release/Standalone/RoboDrummer.app/Contents/Info.plist"
  usage="$(/usr/libexec/PlistBuddy -c 'Print :NSMicrophoneUsageDescription' "$plist")"
  if [ -z "$usage" ]; then echo "::error::Standalone microphone permission description missing"; exit 1; fi
  component="$build/RoboDrummer_artefacts/Release/AU/RoboDrummer.component"
  if [ ! -d "$component" ]; then echo "::error::Missing $component"; exit 1; fi
  # Isolated GitHub runner: register the candidate in the user's Audio Unit domain.
  mkdir -p "$HOME/Library/Audio/Plug-Ins/Components"
  ditto "$component" "$HOME/Library/Audio/Plug-Ins/Components/RoboDrummer.component"
  codesign --force --deep --sign - "$HOME/Library/Audio/Plug-Ins/Components/RoboDrummer.component"
  codesign --verify --deep --strict "$HOME/Library/Audio/Plug-Ins/Components/RoboDrummer.component"
  auval -v aumf RDrm CDrf | tee "$build/qa/auval.txt"
  exit 0
fi

curl -fsSL -o "$tmp/pluginval.zip" "https://github.com/Tracktion/pluginval/releases/download/$pv_version/$pv_archive"
unzip -q "$tmp/pluginval.zip" -d "$tmp"
chmod +x "$tmp/pluginval$suffix"
vst3="$build/RoboDrummer_artefacts/Release/VST3/RoboDrummer.vst3"
if [ ! -e "$vst3" ]; then echo "::error::Missing $vst3"; exit 1; fi
run_gui "$tmp/pluginval$suffix" --strictness-level 10 --validate-in-process --output-dir "$build/qa/pluginval" "$vst3" \
  | tee "$build/qa/pluginval.txt"
