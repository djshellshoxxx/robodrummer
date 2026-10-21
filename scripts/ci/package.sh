#!/usr/bin/env bash
# Stage built plug-ins and produce RoboDrummer-<version>-<platform>.(zip|tar.gz) + .sha256 in <out_dir>.
# Windows also gets RoboDrummer-<version>-windows-x64-setup.exe (Inno Setup) + .sha256.
# usage: package.sh <platform: windows-x64|mac-arm64|mac-x64|linux-x64> <version> <build_dir> <out_dir>
set -euo pipefail
platform="${1:?platform}"; version="${2:?version}"; build="${3:?build dir}"; out="${4:?out dir}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
mkdir -p "$out"
out="$(cd "$out" && pwd)"
src="$build/RoboDrummer_artefacts/Release"
name="RoboDrummer-$version-$platform"
stage="$out/$name"
rm -rf "$stage"; mkdir -p "$stage"

need() { if [ ! -e "$1" ]; then echo "::error::Missing build output: $1"; exit 1; fi; }
need "$src/VST3/RoboDrummer.vst3"; need "$src/CLAP/RoboDrummer.clap"
cp -R "$src/VST3/RoboDrummer.vst3" "$src/CLAP/RoboDrummer.clap" "$stage/"

case "$platform" in
  windows-*) need "$src/Standalone/RoboDrummer.exe"; cp "$src/Standalone/RoboDrummer.exe" "$stage/" ;;
  mac-*)
    need "$src/Standalone/RoboDrummer.app"; cp -R "$src/Standalone/RoboDrummer.app" "$stage/"
    if [ -d "$src/AU/RoboDrummer.component" ]; then cp -R "$src/AU/RoboDrummer.component" "$stage/"; fi
    # Ad-hoc sign so Apple-silicon macs will run the (non-notarised) bundles.
    for b in "$stage"/*.vst3 "$stage"/*.clap "$stage"/*.component "$stage"/*.app; do
      if [ -e "$b" ]; then codesign --force --deep -s - "$b"; fi
    done
    ;;
  linux-*) need "$src/Standalone/RoboDrummer"; cp "$src/Standalone/RoboDrummer" "$stage/"; chmod +x "$stage/RoboDrummer" ;;
  *) echo "::error::Unknown platform $platform"; exit 1 ;;
esac
cp "$root/LICENSE" "$root/COPYRIGHT-TRADEMARK.md" "$root/docs/INSTALL.md" "$stage/"

cd "$out"
hash_file() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" > "$1.sha256"
  else shasum -a 256 "$1" > "$1.sha256"; fi
  cat "$1.sha256"
}

case "$platform" in
  mac-*) archive="$name.zip"; rm -f "$archive"
         ditto -c -k --sequesterRsrc --keepParent "$name" "$archive" ;;
  windows-*) archive="$name.zip"; rm -f "$archive"
         if command -v 7z >/dev/null 2>&1; then 7z a -tzip -bso0 "$archive" "$name"
         else powershell -NoProfile -Command "Compress-Archive -Path '$name' -DestinationPath '$archive'"; fi ;;
  linux-*) archive="$name.tar.gz"; tar -czf "$archive" "$name" ;;
esac
hash_file "$archive"

# Windows: also build the Inno Setup installer from the staged folder.
if [ "${platform%%-*}" = "windows" ]; then
  iscc="${ISCC:-}"
  if [ -z "$iscc" ]; then iscc="$(command -v ISCC.exe || true)"; fi
  if [ -z "$iscc" ] && [ -x "/c/Program Files (x86)/Inno Setup 6/ISCC.exe" ]; then
    iscc="/c/Program Files (x86)/Inno Setup 6/ISCC.exe"
  fi
  if [ -z "$iscc" ]; then echo "::error::ISCC.exe (Inno Setup) not found"; exit 1; fi
  win() { if command -v cygpath >/dev/null 2>&1; then cygpath -w "$1"; else echo "$1"; fi; }
  # MSYS_NO_PATHCONV stops git-bash mangling the /D switches.
  MSYS_NO_PATHCONV=1 "$iscc" "/DAppVersion=${version%%-*}" "/DStageDir=$(win "$out/$name")" \
    "/DOutDir=$(win "$out")" "/DOutName=$name-setup" "$(win "$root/installer/RoboDrummer.iss")"
  hash_file "$name-setup.exe"
fi
rm -rf "$name"
echo "Created $out/$archive"
