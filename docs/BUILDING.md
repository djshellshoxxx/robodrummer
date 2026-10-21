# Building RoboDrummer

## Requirements

- CMake 3.22 or newer
- A C++20 compiler
- For the plug-in shell: network access (JUCE and clap-juce-extensions are fetched by CMake) and, on Linux, the JUCE system libraries (`libasound2-dev libx11-dev libxext-dev libxinerama-dev libxrandr-dev libxcursor-dev libxi-dev libfreetype-dev libfontconfig1-dev libglu1-mesa-dev`).

## Part 1: portable core and tests (no JUCE)

```bash
cmake -S . -B build-core -DCMAKE_BUILD_TYPE=Release -DROBODRUMMER_BUILD_JUCE=OFF
cmake --build build-core --config Release
ctest --test-dir build-core -C Release --output-on-failure
```

Core musical behavior stays independently testable without a DAW. The JUCE wrapper translates host audio/MIDI/transport data into the core contracts.

## Part 2: plug-in shell (VST3, CLAP, Standalone)

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DROBODRUMMER_BUILD_JUCE=ON -DROBODRUMMER_BUILD_CLAP=ON
cmake --build build --target RoboDrummer_VST3 RoboDrummer_CLAP RoboDrummer_Standalone
```

Outputs land in `build/RoboDrummer_artefacts/Release/{VST3,CLAP,Standalone}`. On macOS add `RoboDrummer_AU` when the project enables it, and set `-DCMAKE_OSX_ARCHITECTURES=arm64` or `x86_64` and `-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0`. Use `-DCMAKE_CXX_COMPILER_LAUNCHER=ccache` for faster rebuilds.

## Part 3: headless harness and pluginval (Linux)

Needs `xvfb`. Runs the harness (if the `RoboDrummerHarness` target exists) and pluginval at strictness 5 against the built VST3:

```bash
bash scripts/ci/juce_checks.sh build
```

## Part 4: package locally

```bash
bash scripts/ci/package.sh linux-x64 0.4.0-beta.1 build dist
```

Platforms: `windows-x64`, `mac-arm64`, `mac-x64`, `linux-x64`. This writes `dist/RoboDrummer-<version>-<platform>.zip|tar.gz` plus a `.sha256` file. macOS packaging ad-hoc signs bundles and zips with `ditto`; Windows packaging also runs Inno Setup (`scripts/ci/RoboDrummer.iss`) and needs `ISCC.exe`.

## CI layout

| Workflow | Triggers | What it does |
|---|---|---|
| `core-ci.yml` | PRs, pushes to `main` and `integrate/**` | Job `core`: build and run ctest once on Ubuntu, Windows and macOS. Job `juce-compile`: Linux build of VST3, CLAP, Standalone and harness (ccache), then harness and pluginval once under xvfb. |
| `release-build.yml` | Manual dispatch, `v*` tags | Independent per-platform package jobs (`windows-x64`, `mac-arm64`, `mac-x64`, `linux-x64`) that only build and package, then a `release` job that publishes whatever succeeded. No tests unless `run_qa` is set. |

Tests are not repeated in release builds on purpose. Release from a commit that already passed `core-ci.yml`, or tick `run_qa` to re-run the core tests first.

## Cutting a release

Dispatch from the command line (builds the commit at the tip of the chosen ref; add `--ref <branch>` to pick a branch):

```bash
gh workflow run release-build.yml -f version=0.4.0-beta.1 -f platforms=all
```

Inputs:

- `version` (required): no leading `v`, e.g. `0.4.0-beta.1`. The release tag becomes `v0.4.0-beta.1`.
- `prerelease` (default `true`): marks the GitHub Release as a pre-release.
- `platforms` (default `all`): or a comma list such as `windows,linux`. Tokens: `windows`, `mac-arm64`, `mac-x64`, `linux`, and `mac` for both Macs.
- `run_qa` (default `false`): run the core test suite before publishing.

Example for a single platform retry: `gh workflow run release-build.yml -f version=0.4.0-beta.1 -f platforms=mac-arm64`. Re-running adds or replaces assets on the existing release (`--clobber`).

Pushing a tag like `v0.4.0` also runs the workflow for all platforms; versions containing a `-` are marked pre-release.

Release notes come from `docs/RELEASE_NOTES_<base version>.md` (for `0.4.0-beta.1` that is `docs/RELEASE_NOTES_0.4.0.md`) when it exists, otherwise a generated template. Any platform whose job failed is listed as missing in the notes, and the other platforms are still published.

Assets per release: `RoboDrummer-<version>-windows-x64.zip`, `RoboDrummer-<version>-windows-x64-setup.exe`, `RoboDrummer-<version>-mac-arm64.zip`, `RoboDrummer-<version>-mac-x64.zip`, `RoboDrummer-<version>-linux-x64.tar.gz`, a `.sha256` for each, and `SHA256SUMS.txt`.
