#!/usr/bin/env bash
# All shipped formats must support the architecture running their native tests.
set -euo pipefail
build="${1:?build dir}"
native_arch="${2:-$(uname -m)}"
for binary in \
  "$build/RoboDrummer_artefacts/Release/VST3/RoboDrummer.vst3/Contents/MacOS/RoboDrummer" \
  "$build/RoboDrummer_artefacts/Release/CLAP/RoboDrummer.clap/Contents/MacOS/RoboDrummer" \
  "$build/RoboDrummer_artefacts/Release/AU/RoboDrummer.component/Contents/MacOS/RoboDrummer" \
  "$build/RoboDrummer_artefacts/Release/Standalone/RoboDrummer.app/Contents/MacOS/RoboDrummer"; do
  lipo "$binary" -verify_arch "$native_arch"
done
