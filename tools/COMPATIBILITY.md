# Native default-output compatibility

The original raw audio/MIDI FNV golden was recorded with one Linux toolchain.
Native Windows, macOS and Debian/GCC 14 produce different floating-point audio
bits, so comparing them to that single number does not establish a regression.
The historical golden remains in the harness as recorded evidence.

`build_compat_reference.py` establishes the actual default-off invariant:

1. Read immutable pre-integration source commit
   `102dc33f6bc3202a114a28bb0482e24138b18f8d` (`22c67cc^`) with Git object verification.
2. Archive it into the ignored build cache, retaining the checkout and HEAD.
3. Generate a reference executable from the **same canonical renderer and input
   fixture** used by the current harness. No current application/core objects
   are reused; the historical application is built independently.
4. Use matching pinned JUCE/CLAP sources, compiler, configuration, architecture,
   deployment target and FP contract. SDK pin mismatches are rejected. Both
   consumers receive `-ffp-contract=off` or `/fp:precise`.
5. Capture each old/current run twice. Compare complete framed audio/MIDI bytes
   and packet lengths, and prove both implementations repeat deterministically.
   SHA-256 and byte counts are retained in `build/qa/compatibility.json`.

This is an exact behavior comparison, not a replacement hash calculated from
the current implementation. The normal CTest `default_output_reference`
fixture creates the verified reference before `plugin_harness`; Linux callers
need a display, normally Xvfb. `scripts/ci/juce_checks.sh` runs the same gate
before its harness/format validation. Python 3 and Git are required for this
test tooling. An initial reference build compiles the old application; later
runs reuse valid outputs.

The current harness can export its canonical capture with `--export-baseline
<file>`, or compare the full suite against a verified capture with
`--baseline-reference <file>`. CI only uses the pinned historical generator.
The default-off regression contract is separate from intentional, specified
tracker fixes; changed behavior must have its own characterization and review.

Local 2026-10-22 evidence (GCC 14.2, Debian 13, JUCE 9.0.3): all four captures
contain **22,716,916 bytes** with SHA-256
`6c3213a971be39899e531a27db9d2e441a3cb2b5b6821a3141583d2ad78bcb07`.
Both historical and current FNV are `0a443e02566a1168`, differing from the
original Linux golden `4a85caa7203610d1`. Propagating the FP interface to the
harness alone did not alter that local result; the historical comparison proves
the difference is present before the 0.4 integration on this toolchain.
