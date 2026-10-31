#!/usr/bin/env bash
# Install an isolated CI candidate, wait for native discovery, then validate it.
set -euo pipefail
component="${1:?component}"
qa="${2:?QA directory}"
components="${3:-$HOME/Library/Audio/Plug-Ins/Components}"
mkdir -p "$qa"
cp "$component/Contents/Info.plist" "$qa/au-info.plist"
python3 - "$component/Contents/Info.plist" <<'PY' | tee "$qa/au-identity.txt"
import plistlib
import sys
with open(sys.argv[1], "rb") as source:
    components = plistlib.load(source).get("AudioComponents", [])
expected = ("aumf", "RDrm", "CDrf")
actual = [tuple(item.get(key) for key in ("type", "subtype", "manufacturer")) for item in components]
if actual != [expected]:
    raise SystemExit(f"Unexpected Audio Unit identity: {actual!r}; expected {expected!r}")
print("Audio Unit identity:", *expected)
PY
mkdir -p "$components"
ditto "$component" "$components/RoboDrummer.component"
codesign --force --deep --sign - "$components/RoboDrummer.component"
codesign --verify --deep --strict "$components/RoboDrummer.component"
# A freshly copied component may not be in the registrar's cached inventory.
# This is the refresh prescribed by spec 12 for isolated native CI runners.
killall -9 AudioComponentRegistrar 2>/dev/null || true
discovered=false
attempt=0
while [ "$attempt" -lt 10 ]; do
  attempt=$((attempt + 1))
  if auval -a > "$qa/au-components.txt" 2>&1 &&
     grep -Eq '^[[:space:]]*aumf[[:space:]]+RDrm[[:space:]]+CDrf([[:space:]]|$)' "$qa/au-components.txt"; then
    discovered=true
    break
  fi
  if [ "$attempt" -lt 10 ]; then sleep 1; fi
done
if [ "$discovered" != true ]; then
  echo "::error::RoboDrummer Audio Unit not discovered after $attempt native scans"
  cat "$qa/au-components.txt"
  exit 1
fi
auval -v aumf RDrm CDrf | tee "$qa/auval.txt"
