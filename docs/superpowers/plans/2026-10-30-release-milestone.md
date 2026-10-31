# Native release validation completion plan

**Goal:** Complete the automated release qualification work already in PR #34, preserving its exact audio compatibility, assertion and format-validation gates.

**Basis:** docs/specs/18-release-process.md; docs/feature-audit/RELEASE_QA_2026-10-22.md; failed native run 39808693404 at d34f8c4.

**Architecture:** Keep production audio code unchanged. The processor harness waits for actual message-thread publication with a bounded deadline. A small macOS architecture adapter validates all four shipped formats using Apple's documented lipo argument order. PR CI runs the same native macOS validators so their regressions are caught before a release dispatch.

- [x] Inspect active branches/PRs and obtain actual failed job logs.
- [x] Set up isolated CMake/Ninja and verify the existing 53 core / 11 release-script tests.
- [ ] Add a failing executable regression for macOS architecture validation (including wrong architecture).
- [ ] Correct lipo invocation; preserve every binary check and strict failure propagation.
- [ ] Replace only the two failing fixed waits with bounded condition-based message pumping; preserve assertions.
- [ ] Add both native macOS architectures to PR validation and verify workflow syntax.
- [ ] Run local core, release-script, native Linux harness, exact historical compatibility and pluginval gates.
- [ ] Update PR #34 with the reviewed changes and inspect native CI results.
- [ ] Audit the complete repository by module, with independent review of core/analysis, DSP/sampler, plugin/threading/persistence, and build/release/security. Record reproducible findings, severity and platform limitations.

**Constraints:** Stable parameter IDs, state/preset formats, plugin identity and musical behavior stay intact. No allocations, locks or deletion on the audio thread. No main merge, tag or public release as part of this work. Native CI and device/DAW evidence must be reported separately.

## Native AU follow-up

Run 41173404802 passed all 2,333 arm64 harness checks, exact compatibility, architecture verification and signing, then auval could not discover the just-installed AU. Automation now follows spec 12's registrar refresh and bounded discovery sequence, validates and retains the actual bundle identity/plist, and still requires the unchanged auval validation to pass. Four executable regressions preserve missing-component, delayed-discovery, identity and validator-failure behavior. The fix remains subject to native CI.
