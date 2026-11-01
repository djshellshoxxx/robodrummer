# RoboDrummer QA record

The source is under validation; **BETA READY and RELEASE READY are not claimed**. Automated results do not replace real guitar, DAW, device, screen-reader or physical clean-machine acceptance.

## Current automated evidence — 2026-10-31

Consolidated source includes bounded MIDI/seek callbacks, safe sample import and ownership, analysis/preset fixes, native UI contracts and fixture/packaging infrastructure. Final PR checks and merge SHA will be recorded separately from older evidence.

| Check | Observed result |
|---|---|
| Release portable core plus fixture integration | 69/69 passed; actual production-runner fixture methods execute through CTest. Missing optional real corpus explicitly reports SKIPPED. |
| Core ASan, LeakSanitizer and UBSan | 68/68 passed before the additive reduced-motion preference test and fixture schema follow-up; those final additions require focused rerun. Leak detection remains enabled. |
| Python release, signing, archive, fixture and platform adapters | 58/58 passed with the production fixture runner supplied; no skipped engine methods. |
| Actual processor harness and native UI contracts | 55,077 checks, zero failures; includes concurrent/reentrant host state, long/cumulative SysEx, replaced prebuffered host MIDI, command/critical overflow, huge PPQ seeks, sampler retirement, float WAV/AIFF imports, preset slices, parameter text, native popup actions, all nine tabs and duplicate accessibility providers. |
| 30-minute simulated soak | Passed in 15.5 seconds, drummer tempo 112.6–120.2 BPM. This is simulated audio duration. |
| Historical default audio/MIDI | Full harness passed the unchanged pinned reference from source `102dc33f6bc3202a114a28bb0482e24138b18f8d`. Four-capture final native comparison remains a CI gate. |
| Native formats | Linux VST3, CLAP, Standalone and harness built. Final Windows and both native macOS architectures must pass current-source PR CI before merge. |
| Linux VST3 pluginval 1.0.3, strictness 10 | Passed on the rebuilt Linux plugin; exit 0. |
| CLAP validator 0.4.1 | 44 rows: 31 success, 9 upstream skips, 3 failed, 1 warning. See the explicit contract blocker below. This is separate from VST3 pluginval. |
| Workflow/script checks | Both workflows pass actionlint 1.7.12; signing/archive adapter regressions pass. Native archives/checksums and isolated Windows install/uninstall run in artifact-only PR CI. |

The Linux callback audit interposes `malloc/calloc/realloc/free` and counts only the armed audio thread. Other platforms count `new/delete`; that does not instrument every framework C allocation. Audits cover both allocations and frees, with a self-test proving hooks count once. Host-owned MIDI storage must be prebuffered to 8,192 bytes; see [architecture contracts](ARCHITECTURE.md#bounded-host-adapters-and-ownership-contracts). Allocation assertions remain enabled and the historical reference is unchanged.

## Explicit CLAP state-contract blocker

The display/value conversion failures and missing host parameter rescan have been repaired. The remaining three upstream state-reproducibility cases expect every writable parameter to restore its saved value. Established RoboDrummer behavior and its regression tests force Practice Mode and the practice-loop runtime switch off on state load, preventing a saved session from starting practice. The current report exposes Practice Mode On → Off.

CLAP has no transient-parameter flag that communicates this policy. Marking the control readonly/nonautomatable, changing restore behavior or excluding validator tests would change the contract or conceal the result. Preserve the raw failing report and nonzero exit. Clean CLAP state conformance needs a separately specified compatibility decision; it is **not a credential or environment blocker**. Native PR CI runs this separate diagnostic with its failure visible and retains the raw report/logs; it is explicitly nonblocking because no compatible transient-state policy is specified. A green native job does not mean these CLAP cases passed. The command itself keeps its nonzero exit:

```bash
python3 -B scripts/ci/clap_validate.py build/RoboDrummer_artefacts/Release/CLAP/RoboDrummer.clap --output build/qa/clap-validator.json
```

## Fixture and launch gates

`tools/generate_synthetic_fixtures.py` creates original, explicitly synthetic clips. `tools/check_fixtures.py` validates PCM, schema, license, manifest/hash, rate/block coverage and annotations before measuring production analysis. Raw and octave-folded tempo error are separate. Synthetic pluck/ramp/jump ambiguities remain visible failures; successful ingestion is not tracker qualification. A missing optional corpus skips locally; `ROBODRUMMER_FIXTURES_REQUIRED=1` makes missing/corrupt data fail. No authorized real recording corpus is available.

| Remaining acceptance | Status |
|---|---|
| Licensed real-guitar corpus, consent, independent annotations and spec 11 qualification | Blocked on supplied/verified recordings and reviewers. Synthetic results cannot satisfy it. |
| Real DAW load/save/automate/resize/close across the supported host matrix | Human/device qualification outstanding. |
| Physical audio/MIDI device latency, long sessions, OS screen readers and host key forwarding | Human/device qualification outstanding; native peers/accessibility providers are automated coverage only. |
| Paid Windows/macOS signatures and macOS notarization | Credentials/certificates unavailable. Adapters fail on partial configuration; stable packages cannot silently become unsigned. CI prerelease packages are explicitly unsigned/ad-hoc and are artifacts only. |
| Windows installer on a physical clean machine | Outstanding; isolated hosted-runner install/uninstall is a separate automated check. |

Analysis remains on the audio thread under ADR 10. ADR 16 transient enhancement and spec 14 learned models retain their real-guitar gates. Historical [capture QA](feature-audit/CAPTURE_QA_2026-10-22.md) and prior release CI do not qualify later source changes.
