# 13. Code signing and notarization

## Purpose & user value

Unsigned installers trigger Windows SmartScreen and macOS Gatekeeper warnings, and some hosts refuse to scan unsigned bundles on recent macOS. Signing makes installs one-click and proves the files were not altered. Until certificates exist, beta builds ship unsigned (documented in `docs/INSTALL.md`); this spec defines the signed pipeline so it can be switched on by adding secrets.

## Behavior (precise)

Signing is **optional in CI**: each signing step runs only when its secrets are present, so forks, pull requests and unsigned betas keep working. Release candidates for 1.0 must be signed (`18-release-process.md`).

### Windows

Artifacts to sign: `RoboDrummer.vst3` (the `.vst3` DLL inside `Contents/x86_64-win/`), `RoboDrummer.clap`, `RoboDrummer.exe` (standalone), and the Inno Setup installer `RoboDrummer-<version>-Windows-x64-Setup.exe` (sign the installer and the uninstaller via Inno `SignTool`).

Option A (recommended): **Azure Trusted Signing** (Artifact Signing). No hardware token, short-lived certificates, works on GitHub-hosted runners. Requirements: an Azure subscription, a Trusted Signing account and certificate profile (identity validation of the publisher, Circuit Drift Labs; availability depends on region and organisation eligibility), and an Entra app registration with the "Trusted Signing Certificate Profile Signer" role.

Option B: **OV or EV Authenticode certificate** (PFX). EV gives immediate SmartScreen reputation; OV builds reputation over downloads. Since 2023 private keys for new publicly trusted code-signing certificates must live on hardware or a cloud HSM, so a plain PFX in a secret is only possible with an older certificate or a cloud-key service (for example the CA's cloud signing, or Azure Key Vault with `azuresigntool`).

Timestamping is mandatory (RFC 3161, SHA-256), so signatures stay valid after certificate expiry: `http://timestamp.acs.microsoft.com` for Azure; the CA's URL otherwise.

### macOS

Requirements: Apple Developer Program membership; **Developer ID Application** certificate (and **Developer ID Installer** if a `.pkg` is shipped); an app-specific password or App Store Connect API key for `notarytool`.

Steps per bundle (`RoboDrummer.vst3`, `RoboDrummer.clap`, `RoboDrummer.component`, `RoboDrummer.app`), universal arm64+x86_64 builds (already built by the workflow):

1. Sign inside-out with hardened runtime and timestamp:
   `codesign --force --deep --options runtime --timestamp --sign "Developer ID Application: <Name> (<TEAMID>)" <bundle>`
   (Use `--entitlements` only if required; the plug-in needs none: no network, no camera, no JIT. The standalone needs `com.apple.security.device.audio-input` only for sandboxed distribution, not for Developer ID outside the Mac App Store; hardened runtime requires the microphone usage string `NSMicrophoneUsageDescription` in the app's Info.plist, which JUCE adds when `MICROPHONE_PERMISSION_ENABLED` is set for the standalone. Verify before release.)
2. Verify: `codesign --verify --deep --strict --verbose=2 <bundle>` and `spctl -a -t exec -vv RoboDrummer.app`.
3. Package for notarization: `ditto -c -k --keepParent <bundle> <bundle>.zip` (or build a `.dmg`/`.pkg` and notarize that; a single `.dmg` containing all bundles is simplest).
4. Submit and wait: `xcrun notarytool submit <file> --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" --password "$APPLE_APP_PASSWORD" --wait` (or `--key/--key-id/--issuer` for an API key). On failure: `xcrun notarytool log <submission-id>`.
5. Staple: `xcrun stapler staple <bundle-or-dmg>` and `xcrun stapler validate <...>`. Plug-in bundles and `.app` can be stapled; the zip cannot, so distribute a stapled `.dmg` or `.pkg`.
6. Post-sign validation: `auval -v aumf RDrm CDrf` for the AU (`12-host-matrix-qa.md`).

### Linux

No code signing. Publish SHA-256 checksums (see `18-release-process.md`); optionally a detached GPG signature `SHA256SUMS.asc` made with a release key held by the maintainer.

### CI secrets (names)

Add as repository or `release` environment secrets (environment protection requires manual approval for tag builds):

| Secret | Used for |
|---|---|
| `AZURE_TENANT_ID`, `AZURE_CLIENT_ID`, `AZURE_CLIENT_SECRET` | Azure Trusted Signing authentication (or federated OIDC with `id-token: write`, preferred) |
| `AZURE_TS_ENDPOINT`, `AZURE_TS_ACCOUNT`, `AZURE_TS_PROFILE` | Trusted Signing endpoint, account name, certificate profile name |
| `WIN_CERT_PFX_BASE64`, `WIN_CERT_PASSWORD` | Option B only: PFX as base64 and its password |
| `MACOS_CERT_P12_BASE64`, `MACOS_CERT_PASSWORD` | Developer ID Application (and Installer) certificates in a p12 |
| `MACOS_KEYCHAIN_PASSWORD` | Password for the temporary CI keychain |
| `MACOS_SIGN_IDENTITY` | Full identity string, for example `Developer ID Application: Name (TEAMID)` |
| `APPLE_ID`, `APPLE_TEAM_ID`, `APPLE_APP_PASSWORD` | `notarytool` credentials (alternative: `APPLE_API_KEY_P8_BASE64`, `APPLE_API_KEY_ID`, `APPLE_API_ISSUER`) |
| `GPG_PRIVATE_KEY`, `GPG_PASSPHRASE` | Optional checksum signature |

Secrets are never echoed; steps use `env:` mapping, not command-line interpolation. Certificates are imported into a temporary keychain that is deleted in an `if: always()` step.

### Workflow steps (added to `.github/workflows/release-build.yml`)

Windows job, after "Stage plug-ins" and before zip and installer:

1. `azure/trusted-signing-action@v0.5.x` (pin to a commit) with `files-folder: dist/RoboDrummer-Windows-x64`, `files-folder-filter: dll,exe,vst3,clap`, `file-digest: SHA256`, `timestamp-rfc3161: http://timestamp.acs.microsoft.com`, `timestamp-digest: SHA256`, `if: env.AZURE_CLIENT_ID != ''`. For Option B use `signtool sign /fd SHA256 /tr <url> /td SHA256 /f cert.pfx /p ...` after decoding the secret.
2. Build the installer with Inno's `/Ssigntool=...` so the installer and uninstaller are signed (add `SignTool=signtool` and `SignedUninstaller=yes` under `[Setup]`, guarded by a define).
3. Verify: `signtool verify /pa /v <each file>` and `Get-AuthenticodeSignature`; fail the job if status is not `Valid` when signing is enabled.

macOS job, after "Stage plug-ins":

1. Create temporary keychain, import p12, `security set-key-partition-list ...`.
2. Sign each bundle (step 1 above), verify.
3. Build `RoboDrummer-<version>-macOS-Universal.dmg` (`hdiutil create`), sign the dmg, `notarytool submit --wait`, `stapler staple`.
4. Run `spctl -a -t open --context context:primary-signature -vv <dmg>` and `auval -v aumf RDrm CDrf`.
5. Upload the dmg; delete the keychain (`if: always()`).

Both jobs remain green when secrets are absent: the signing steps are skipped and the job summary states "UNSIGNED BUILD".

### What unsigned beta users must do

Documented in `docs/INSTALL.md` and `docs/specs/18-release-process.md`:

* **Windows:** SmartScreen "More info" then "Run anyway". Verify the SHA-256 of the download first. Antivirus false positives on unsigned installers are possible; report them rather than disabling protection.
* **macOS:** the downloaded files carry the quarantine attribute. Remove it once: `xattr -dr com.apple.quarantine <RoboDrummer.vst3 / .clap / .component / .app>`, or right-click the app, Open. On Apple Silicon unsigned arm64 code will not run without at least an ad-hoc signature: the workflow applies `codesign --force --deep --sign -` to every bundle for unsigned builds so they run after the quarantine flag is removed (add this step; it is not present today).
* **Linux:** nothing to sign; check the checksum.

## Parameters

None.

## Actions/MIDI mapping

None.

## State serialization

None.

## UI placement & labels & tooltips

SETTINGS -> About may show "Signed build" or "Unsigned beta build" (derived from a compile definition `RD_SIGNED_BUILD` set by CI). Optional.

## Threading/real-time

Not applicable.

## Test plan

1. Dry run on a private branch with a test certificate or Apple developer account: signature verification commands above must pass.
2. Clean-machine install of the signed Windows installer: no SmartScreen "unknown publisher" (EV) or publisher name shown (OV/Azure).
3. Clean macOS machine (no developer tools): download the dmg through a browser, copy the plug-ins and run the standalone; Gatekeeper accepts; Logic/Reaper scan the AU/VST3 without prompts.
4. Tamper test: modify one byte of a signed binary; `signtool verify` / `codesign --verify` must fail.

## Acceptance criteria

* All shipped binaries verify (`signtool verify /pa`, `codesign --verify --deep --strict`, `spctl`, `stapler validate`).
* No secret appears in logs (workflow log review).
* `docs/QA.md` row "Windows installer on a clean machine" and a new macOS equivalent move from **not run** to dated results.
* Known blocker 3 in QA.md ("Code signing / notarisation") is removed only when this passes.

## CDL compliance notes

CDL section 8 (privacy/security): signing adds no telemetry or network use in the product; notarization is a build-time step only.

## Integration hook points

* `.github/workflows/release-build.yml` (steps above), `installer/RoboDrummer.iss` (SignTool lines), `docs/INSTALL.md` (remove "not code-signed yet" wording once signed), `CMakeLists.txt` (optional `RD_SIGNED_BUILD` definition and the Info.plist microphone string check).
