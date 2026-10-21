#!/usr/bin/env bash
# Create or update the GitHub Release from downloaded artifacts and upload them.
# Env: GH_TOKEN GITHUB_SHA GITHUB_REPOSITORY VERSION BASE_VERSION TAG PRERELEASE EXPECTED
# usage: publish.sh <artifact_dir>
set -euo pipefail
dir="${1:?artifact dir}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$dir"

shopt -s nullglob
files=(RoboDrummer-*.zip RoboDrummer-*.tar.gz RoboDrummer-*-setup.exe RoboDrummer-*.sha256)
if [ "${#files[@]}" -eq 0 ]; then echo "::error::No artifacts to publish"; exit 1; fi

missing=""
for p in $EXPECTED; do
  if ! compgen -G "RoboDrummer-$VERSION-$p.*" >/dev/null; then missing="$missing $p"; fi
done
missing="${missing# }"

# Combined checksums: every payload file, hashed here so the list is always complete.
: > SHA256SUMS.txt
for f in RoboDrummer-*.zip RoboDrummer-*.tar.gz RoboDrummer-*-setup.exe; do
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
if [ -n "$missing" ]; then
  {
    echo
    echo "> **Missing platforms:** $missing (build failed or was skipped). Re-run the workflow for these to add them."
  } >> "$notes"
fi

repo="${GITHUB_REPOSITORY}"
if [ "$PRERELEASE" = "true" ]; then pre="--prerelease=true"; else pre="--prerelease=false"; fi
if gh release view "$TAG" --repo "$repo" >/dev/null 2>&1; then
  gh release edit "$TAG" --repo "$repo" --title "RoboDrummer $VERSION" --notes-file "$notes" "$pre"
else
  gh release create "$TAG" --repo "$repo" --target "$GITHUB_SHA" --title "RoboDrummer $VERSION" \
    --notes-file "$notes" "$pre"
fi
gh release upload "$TAG" --repo "$repo" --clobber "${files[@]}" SHA256SUMS.txt
echo "Published $TAG (missing: ${missing:-none})"
