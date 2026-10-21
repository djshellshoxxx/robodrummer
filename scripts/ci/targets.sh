#!/usr/bin/env bash
# Print the CMake plug-in targets to build for a platform (windows|mac|linux).
# AU is only included on mac and only when CMakeLists.txt enables it.
set -euo pipefail
os="${1:?usage: targets.sh windows|mac|linux}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
t="RoboDrummer_VST3 RoboDrummer_CLAP RoboDrummer_Standalone"
if [ "$os" = "mac" ] && grep -Eq 'FORMATS[^)]*\bAU\b' "$root/CMakeLists.txt"; then
  t="RoboDrummer_VST3 RoboDrummer_AU RoboDrummer_CLAP RoboDrummer_Standalone"
fi
echo "$t"
