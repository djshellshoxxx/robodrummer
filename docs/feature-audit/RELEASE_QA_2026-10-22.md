# Release qualification — 2026-10-22

Baseline: `43eca3c76a4b5b4ad4ae7436e155c65bb79e3bbd` (main).

The product feature audit remains in `FEATURE_AUDIT_0.4.0.md`. This follow-up
addresses the automated release path required by spec 18; it does not replace
the real-guitar, DAW-host, clean-machine or signing qualification protocols.

## Findings and changes

* Release tags previously disabled QA by default. Native release builds now run
  their core suite and processor harness. Windows and Linux also require
  pluginval strictness 10; both macOS architectures require auval. Reports are
  retained as CI artifacts. Intel macOS uses a native Intel runner, allowing its
  harness and Audio Unit to execute rather than only cross-compile.
  FetchContent caches are separated by architecture, and macOS validation
  checks the architecture of every shipped binary with lipo.
* Failed or skipped platform jobs could produce a public release containing
  only the successful platforms. Releases now require Windows x64, macOS arm64,
  macOS x64 and Linux x64, including the Windows installer. Subset dispatches
  remain useful build diagnostics and cannot create releases.
  Packaging checks the actual nonempty binaries inside format bundles, so an
  empty VST3/CLAP/AU directory cannot masquerade as a completed build.
* Release versions could disagree with the compiled CMake version, and beta
  versions could be marked stable. The planner rejects both inconsistencies.
* Re-running publication replaced previously distributed assets. Publication
  now creates a new draft, with all payloads and their checksums, and refuses to
  change an existing release. Clean-machine qualification precedes publishing
  the draft, as required by spec 18.
* Packages omitted the parameter reference and third-party notices. They now
  include HELP, PARAMETERS and the license/SBOM files from the exact JUCE and
  CLAP dependency sources used by CMake. Windows installation retains them.
* The macOS standalone lacked an explicit microphone usage description.
  CMake now supplies it, and the native macOS validation checks the generated
  Info.plist. This is required for the guitar-input workflow.
* Windows installer display versions now retain the beta/rc suffix; Windows
  file-version resources still use the numeric version.

## Local evidence

Debian 13 x86_64, GCC 14.2, CMake/Ninja Release build:

* Core suite: **53/53 passed**, assertions enabled, 4.03 seconds.
* Release script regression suite: **11/11 passed**. It exercises missing
  platform/installer rejection, immutable draft creation, version/label/QA
  rules, and archive contents/checksums without making release writes.
* `bash -n scripts/ci/*.sh`: passed.
* actionlint **1.7.12**: both workflows passed. The downloaded tool was verified
  against its upstream SHA-256 release manifest. GitHub's runner-images table
  confirms the `macos-15-intel` native x64 label.

Native plug-in builds, validators and current CI results are recorded below
when completed; this record alone does not claim they passed.

## Remaining product qualification

* No licensed real-guitar corpus is present. Spec 11's real recordings and
  measured acceptance thresholds remain unverified.
* The spec 12 manual DAW host matrix, real-guitar session and clean-machine
  installer checks remain unrun in this cloud Linux machine.
* Paid Windows signing and macOS Developer ID/notarization are not configured.
  Unsigned betas remain explicitly documented; stable-release criteria apply.
* Existing proprietary project licensing and JUCE notices are preserved. The
  owner confirmed a valid commercial JUCE license covering RoboDrummer
  distribution on 2026-10-22.
