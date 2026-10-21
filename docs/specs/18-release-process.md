# 18. Release process (beta to release)

## Purpose & user value

One written path from a tagged beta to a released 1.0, so builds are reproducible, verifiable and installable by users who receive unsigned binaries. Companion specs: `12-host-matrix-qa.md` (host checks), `13-code-signing-notarization.md` (signing), `11-real-guitar-fixtures.md` (tracker evidence).

## Behavior (precise)

### Versioning scheme

Semantic Versioning with pre-release tags: `MAJOR.MINOR.PATCH[-beta.N | -rc.N]`.

* Current code: `project(RoboDrummer VERSION 0.3.0 ...)` in `CMakeLists.txt` (numeric only; JUCE derives `JucePlugin_VersionString` from it).
* This release line: `0.4.0-beta.1`, `0.4.0-beta.2`, ... then `0.4.0-rc.1` if needed, then `1.0.0` when the release criteria below are met. Fixes after a release: `1.0.1`.
* Git tag: `v0.4.0-beta.1` (annotated). Release name: `RoboDrummer 0.4.0-beta.1`. GitHub release marked **pre-release** for beta and rc.
* Source of truth: the numeric part stays in `project(RoboDrummer VERSION X.Y.Z)`; add `set(ROBODRUMMER_PRERELEASE "beta.1" CACHE STRING "")` so `ROBODRUMMER_FULL_VERSION` = `X.Y.Z-beta.1` is passed as a compile definition (shown in About) and to the installer. Windows file/product version stays numeric `X.Y.Z.0` (Windows resources cannot carry the tag); the installer `AppVersion` uses the full string. The existing workflow's regex `project\(RoboDrummer VERSION ([0-9.]+)` keeps working for the numeric part.
* Parameter and state compatibility rule: parameter IDs never change or disappear; additions are appended; state version bumps are documented in release notes (state version is 3 in 0.3.x). A change that would remap host automation (like growing `jamStyle`) is a MINOR bump at least and is called out in the notes.
* A `CHANGELOG`-style notes file per release: `docs/RELEASE_NOTES_<version>.md` (existing pattern, `RELEASE_NOTES_0.3.0.md`).

### Beta to release criteria

All must be true and recorded in `docs/QA.md` (dated, with commit SHA):

| # | Criterion | Evidence |
|---|---|---|
| 1 | Core suite and headless harness pass on Windows, Linux, macOS (0 failures) | CI run link |
| 2 | pluginval strictness 10 passes on Windows and Linux (blocking step; today it is `continue-on-error`) and `auval` passes on macOS (`12`) | CI artifacts |
| 3 | Host matrix per `12`: five primary hosts on Windows pass items 1 to 15 and 17 to 19; macOS and Linux minimums met | results table in QA.md |
| 4 | Real-guitar fixtures: >= 20 licensed clips, tempo error < 2 BPM and lock < 6 s on >= 90 % (`11`) | fixture report |
| 5 | Beta acceptance scenario (SPEC 37) repeats successfully on real guitar in the standalone and one DAW; recorded session notes | QA.md |
| 6 | Real-time evidence: allocation audit 0; analysis CPU <= 5 % of real time at 48 kHz / 512 (`10`); 30-minute soak with no crash/leak | harness output |
| 7 | Windows installer tested on a clean machine; macOS and Linux packages tested on clean accounts | QA.md rows |
| 8 | Signing and notarization done (`13`) for 1.0; betas may be unsigned | verification output |
| 9 | No open issue labelled `blocker` or `crash`; known limitations listed in notes | issue tracker |
| 10 | Docs current: README status, `INSTALL.md`, `PARAMETERS.md` regenerated, `COMPLIANCE.md` rows updated, `HELP.md`, release notes (`17`) | diff review |
| 11 | Licences: `LICENSE`, `COPYRIGHT-TRADEMARK.md`, third-party notices included in each package | package listing |
| 12 | At least two weeks and two betas with external testers, no new crash or state-loss bug in the last beta | issue history |

Betas need only criteria 1, 2 (pluginval on Linux/Windows), 6 and a smoke run (hosts items 1 to 11 in Reaper plus one more). 

### How builds are produced (separate parts)

Builds are split so that each part can fail and be re-run alone:

1. **Core CI** (`.github/workflows/core-ci.yml`): JUCE-free build and `ctest` on Ubuntu and Windows; JUCE shell build with harness on Ubuntu and Windows.
2. **Package jobs** (`.github/workflows/release-build.yml`, matrix `Windows-x64`, `macOS-Universal`, `Linux-x64`): configure (`-DROBODRUMMER_BUILD_JUCE=ON -DROBODRUMMER_BUILD_CLAP=ON`, macOS `arm64;x86_64`, deployment target 11.0), build, `ctest`, stage into `dist/RoboDrummer-<platform>/` (VST3, CLAP, AU on macOS, standalone, `LICENSE`, `COPYRIGHT-TRADEMARK.md`, `INSTALL.md`, `PARAMETERS.md`), upload artifact, pluginval step, Windows zip and Inno Setup installer (`installer/RoboDrummer.iss`).
3. **Signing parts** (`13`): per-platform steps after staging; skipped when secrets are absent.
4. **Release job** (to add, needs all package jobs, runs only on `v*` tags): downloads the platform artifacts, produces final archives, computes checksums, writes `SHA256SUMS` (and optional signature), fills the release notes template, creates the GitHub release (draft first, pre-release flag for beta/rc), then a maintainer reviews and publishes. Today only the Windows files are attached to a draft release on tags; extend to all three platforms.
5. The generic scripts under `shared/scripts/` (`build_release.py`, `bump_version.py`, `generate_release_notes.py`) come from a shared JUCE starter layout (`.env`, `Source/`) that this repository does not use and **no workflow calls them**; do not rely on them. If a script is wanted, add a small repo-specific one (`tools/release.py`) that: reads the version from `CMakeLists.txt`, validates the tag matches, builds the notes from `docs/RELEASE_NOTES_<version>.md`, and prints the checksum file.

Package names (extend the existing pattern):

| Platform | Files |
|---|---|
| Windows | `RoboDrummer-<version>-Windows-x64-Setup.exe`, `RoboDrummer-<version>-Windows-x64.zip` |
| macOS | `RoboDrummer-<version>-macOS-Universal.dmg` (signed + notarized for 1.0), or `.zip` for unsigned betas |
| Linux | `RoboDrummer-<version>-Linux-x64.tar.gz` |
| Common | `SHA256SUMS`, `SHA256SUMS.asc` (optional), `RELEASE_NOTES.md` |

### Checksums

* After final archives are built: `sha256sum RoboDrummer-* > SHA256SUMS` (Linux/macOS: `shasum -a 256`; Windows PowerShell: `Get-FileHash -Algorithm SHA256`).
* The release body lists each file's SHA-256. Users verify (below). Checksums are computed **after** signing so they match the final bytes.

### Release notes template (`docs/RELEASE_NOTES_<version>.md`)

```markdown
# RoboDrummer <version>

<one-sentence summary and release type: beta | rc | release>

## New
* ...

## Changed
* ...

## Fixed
* ...

## Compatibility
* State version: <n> (loads versions <list>)
* Parameters: <added / none removed>
* Automation or preset impact: <none | description>

## Known limitations
* ... (copy from docs/QA.md)

## Install
See docs/INSTALL.md. Builds are <signed | unsigned beta>. SHA-256 sums are in SHA256SUMS.

## Verification
CI run: <link>   Commit: <sha>   pluginval: <pass/fail>   Host matrix: <summary link>

## Feedback
Please report bugs at <issues URL> with host, OS, format and the About > Copy diagnostics text.
```

### How users install unsigned builds

All platforms: first verify the checksum.

```bash
# Linux / macOS: compare with the value in SHA256SUMS
shasum -a 256 -c SHA256SUMS --ignore-missing
# Windows (PowerShell): compare with the published value
Get-FileHash .\RoboDrummer-<version>-Windows-x64-Setup.exe -Algorithm SHA256
```

* **Windows:** run the Setup. SmartScreen may show "Windows protected your PC": choose More info, then Run anyway. Alternatively use the zip and copy `RoboDrummer.vst3` to `C:\Program Files\Common Files\VST3\` and `RoboDrummer.clap` to `C:\Program Files\Common Files\CLAP\`; right-click the downloaded zip, Properties, tick Unblock before extracting to avoid per-file blocks. Rescan plug-ins in the host.
* **macOS:** copy the bundles to `~/Library/Audio/Plug-Ins/VST3/`, `.../CLAP/`, `.../Components/` and `RoboDrummer.app` to `/Applications/`, then remove quarantine once:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/RoboDrummer.vst3 ~/Library/Audio/Plug-Ins/CLAP/RoboDrummer.clap ~/Library/Audio/Plug-Ins/Components/RoboDrummer.component /Applications/RoboDrummer.app
```

  For the app alternatively right-click, Open, Open. On Apple Silicon the bundles must carry at least an ad-hoc signature (CI adds it for unsigned builds, `13`). If an AU does not appear, run `killall -9 AudioComponentRegistrar` and rescan.
* **Linux:** extract, then copy `RoboDrummer.vst3` to `~/.vst3/`, `RoboDrummer.clap` to `~/.clap/`, and the standalone to `~/.local/bin/` (command in `INSTALL.md`). Mark the standalone executable (`chmod +x`).
* All platforms: load RoboDrummer as an instrument/MIDI-capable track with the guitar routed to its input; AU is classed as a MIDI-controlled effect (see `12`).

### Release steps (maintainer checklist)

1. Create `release/<version>` branch from the integration branch; bump `project(... VERSION X.Y.Z)` and `ROBODRUMMER_PRERELEASE`; regenerate `docs/PARAMETERS.md`; write `docs/RELEASE_NOTES_<version>.md`; update README status and QA.md.
2. Push; wait for Core CI and package jobs green.
3. Run manual checks (`12`, SPEC 37 scenario); record results in QA.md.
4. Tag `vX.Y.Z[-pre]` on the release commit; the tag workflow builds, signs (if configured), checksums and creates a **draft** release.
5. Download the draft's files on clean machines; verify checksums and an install per OS.
6. Publish (clear the draft flag; pre-release flag for beta/rc). Merge the release branch back.
7. For regressions: new patch tag, never re-upload files under an existing tag.

## Parameters

None.

## Actions/MIDI mapping

None.

## State serialization

None. Release notes state the state version.

## UI placement & labels & tooltips

About shows the full version string including the tag (`17`).

## Threading/real-time

Not applicable. The release gate includes the real-time evidence (criterion 6).

## Test plan

Dry run on a throwaway tag (`v0.0.0-test.1`) in a fork: all jobs run, draft release produced, checksums verify, installers launch. Repeat after any workflow change.

## Acceptance criteria

* A tag produces, without manual steps, per-platform packages, `SHA256SUMS` and a draft release with the notes.
* The same commit built twice yields packages with identical contents (file lists and sizes; binary identity is not required).
* A tester can install an unsigned beta on each OS using only the steps above.

## CDL compliance notes

CDL 9 (documentation) and 10 (required links): release notes, install guide and standard links ship in each package. CDL 8: no update checks or telemetry are added; users update manually.

## Integration hook points

`CMakeLists.txt` (`ROBODRUMMER_PRERELEASE`, definitions), `.github/workflows/release-build.yml` (release job, blocking pluginval, checksum step, AU ad-hoc signing for unsigned builds, Linux archive), `installer/RoboDrummer.iss` (`AppVersion` from the full version), `docs/INSTALL.md` (checksum step, AU class note), `docs/QA.md` (release record).
