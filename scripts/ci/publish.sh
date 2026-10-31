#!/usr/bin/env bash
# Create an immutable draft only after the complete platform matrix is present.
# Env: GH_TOKEN GITHUB_SHA GITHUB_REPOSITORY VERSION BASE_VERSION TAG PRERELEASE EXPECTED
# usage: publish.sh <artifact_dir>
set -euo pipefail
dir="${1:?artifact dir}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$dir"

if [ "$EXPECTED" != "windows-x64 mac-arm64 mac-x64 linux-x64" ]; then
  echo "::error::Only a complete Windows, macOS and Linux matrix can create a release"
  exit 1
fi
files=(
  "RoboDrummer-$VERSION-windows-x64.zip"
  "RoboDrummer-$VERSION-windows-x64-setup.exe"
  "RoboDrummer-$VERSION-mac-arm64.zip"
  "RoboDrummer-$VERSION-mac-x64.zip"
  "RoboDrummer-$VERSION-linux-x64.tar.gz"
)
for f in "${files[@]}"; do
  if [ ! -s "$f" ]; then echo "::error::Missing or empty release payload: $f"; exit 1; fi
done

# Combined checksums: every payload file, hashed here so the list is always complete.
: > SHA256SUMS.txt
for f in "${files[@]}"; do
  sha256sum "$f" >> SHA256SUMS.txt
done

notes="notes.md"
nf="$root/docs/RELEASE_NOTES_${BASE_VERSION}.md"
if [ -f "$nf" ]; then
  cp "$nf" "$notes"
else
  {
    echo "# RoboDrummer $VERSION"
    echo
    if [ "$PRERELEASE" = "true" ]; then echo "Pre-release build for testing. Expect rough edges and please report issues."; echo; fi
    echo "## Install"
    echo "See INSTALL.md inside each package. Builds are not notarised or signed with a paid certificate:"
    echo "Windows may show SmartScreen, macOS needs right-click Open (or \`xattr -dr com.apple.quarantine\`)."
    echo
    echo "## Downloads"
    echo "- Windows: \`-windows-x64.zip\` or the \`-setup.exe\` installer"
    echo "- macOS Apple silicon: \`-mac-arm64.zip\`"
    echo "- macOS Intel: \`-mac-x64.zip\`"
    echo "- Linux: \`-linux-x64.tar.gz\`"
    echo
    echo "Verify downloads with \`SHA256SUMS.txt\`."
  } > "$notes"
fi
{
  echo
  echo '## SHA-256 checksums'
  echo '```text'
  cat SHA256SUMS.txt
  echo '```'
} >> "$notes"

repo="${GITHUB_REPOSITORY}"
if [ "$PRERELEASE" = "true" ]; then pre="--prerelease=true"; else pre="--prerelease=false"; fi
if gh release view "$TAG" --repo "$repo" >/dev/null 2>&1; then
  echo "::error::Release $TAG already exists; use a new patch or pre-release version instead of replacing artifacts"
  exit 1
fi
gh release create "$TAG" --repo "$repo" --target "$GITHUB_SHA" --title "RoboDrummer $VERSION" \
  --notes-file "$notes" --draft "$pre" "${files[@]}" SHA256SUMS.txt
echo "Created draft $TAG with the complete platform matrix; clean-machine qualification is required before publication"
