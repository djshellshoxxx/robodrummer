#!/usr/bin/env python3
"""Native signing adapters. Credential-free builds are explicit pre-releases only.

Subprocess arguments may contain signing credentials; never print command lines
or CalledProcessError objects. Native tool failures are reported without them.
"""
import argparse
import base64
import binascii
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile

MAC_CERT = ("MACOS_CERT_P12_BASE64", "MACOS_CERT_PASSWORD", "MACOS_KEYCHAIN_PASSWORD", "MACOS_SIGN_IDENTITY")
MAC_APP = ("APPLE_ID", "APPLE_TEAM_ID", "APPLE_APP_PASSWORD")
MAC_API = ("APPLE_API_KEY_P8_BASE64", "APPLE_API_KEY_ID", "APPLE_API_ISSUER")
WIN_CERT = ("WIN_CERT_PFX_BASE64", "WIN_CERT_PASSWORD")
AZURE = ("AZURE_TENANT_ID", "AZURE_CLIENT_ID", "AZURE_CLIENT_SECRET", "AZURE_TS_ENDPOINT", "AZURE_TS_ACCOUNT", "AZURE_TS_PROFILE")
BUNDLES = ("RoboDrummer.vst3", "RoboDrummer.clap", "RoboDrummer.component", "RoboDrummer.app")


class SigningError(RuntimeError):
    pass


def configured(names):
    return any(os.environ.get(name) for name in names)


def require(names):
    if not all(os.environ.get(name) for name in names):
        raise SigningError("Incomplete signing configuration (required fields are missing)")


def windows_provider():
    if configured(AZURE):
        require(AZURE)
        if configured(WIN_CERT): raise SigningError("Ambiguous Windows signing configuration: choose one provider")
        return "azure"
    if configured(WIN_CERT + ("WIN_TIMESTAMP_URL",)):
        require(WIN_CERT)
        return "pfx"
    return None


def policy(platform, version):
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.]+)?", version):
        raise SigningError("Invalid signing version")
    if platform == "linux-x64":
        return "checksums-only"
    if platform == "windows-x64":
        if windows_provider() is not None: return "signed"
    elif platform in ("mac-arm64", "mac-x64"):
        if configured(MAC_CERT + MAC_APP + MAC_API):
            require(MAC_CERT)
            if configured(MAC_APP) and configured(MAC_API):
                raise SigningError("Ambiguous notarization configuration: choose one credential method")
            if configured(MAC_API):
                require(MAC_API)
            else:
                require(MAC_APP)
            return "signed"
    else:
        raise SigningError("Unsupported signing platform")
    if "-" not in version:
        raise SigningError("Stable packaging requires signing configuration")
    return "unsigned-beta"


def run(args, allow_failure=False):
    try:
        result = subprocess.run([str(arg) for arg in args], capture_output=True, text=True, check=False)
    except OSError:
        raise SigningError(f"Required signing tool is unavailable: {Path(str(args[0])).name}") from None
    if result.returncode and not allow_failure:
        raise SigningError(f"Signing tool failed: {Path(str(args[0])).name} (exit {result.returncode})")
    return result


def decode_secret(name, target):
    try:
        data = base64.b64decode(os.environ[name], validate=True)
        if not data:
            raise ValueError()
    except (ValueError, binascii.Error):
        raise SigningError("Invalid base64 certificate/key configuration") from None
    target.write_bytes(data)
    target.chmod(0o600)


def signtool():
    explicit = os.environ.get("SIGNTOOL")
    if explicit:
        return explicit
    found = shutil.which("signtool.exe") or shutil.which("signtool")
    if found:
        return found
    sdk = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Windows Kits/10/bin"
    candidates = sorted(sdk.glob("*/x64/signtool.exe"), reverse=True)
    if candidates:
        return str(candidates[0])
    raise SigningError("Windows SDK signtool is required for signed packaging")


def verify_windows_file(path):
    run([signtool(), "verify", "/pa", "/v", path])


def sign_windows_file(version, path):
    mode = policy("windows-x64", version)
    if mode == "unsigned-beta":
        return
    provider = windows_provider()
    with tempfile.TemporaryDirectory(prefix="rd-signing-windows-", dir=os.environ.get("RUNNER_TEMP")) as directory:
        directory = Path(directory)
        default_timestamp = "http://timestamp.acs.microsoft.com" if provider == "azure" else "http://timestamp.digicert.com"
        timestamp = os.environ.get("WIN_TIMESTAMP_URL") or default_timestamp
        if not timestamp.startswith(("http://", "https://")):
            raise SigningError("A valid RFC3161 timestamp URL is required")
        command = [signtool(), "sign", "/fd", "SHA256", "/tr", timestamp, "/td", "SHA256"]
        if provider == "azure":
            dlib = Path(os.environ.get("AZURE_SIGNING_DLIB", ""))
            if not dlib.is_file(): raise SigningError("Azure signing configuration requires an installed Azure.CodeSigning.Dlib.dll")
            metadata = directory / "metadata.json"
            metadata.write_text(json.dumps({"Endpoint": os.environ["AZURE_TS_ENDPOINT"], "CodeSigningAccountName": os.environ["AZURE_TS_ACCOUNT"], "CertificateProfileName": os.environ["AZURE_TS_PROFILE"]}))
            command += ["/dlib", dlib, "/dmdf", metadata]
        else:
            certificate = directory / "certificate.pfx"
            decode_secret("WIN_CERT_PFX_BASE64", certificate)
            command += ["/f", certificate, "/p", os.environ["WIN_CERT_PASSWORD"]]
        run([*command, path])
        verify_windows_file(path)


def verify_mac_bundle(bundle):
    run(["codesign", "--verify", "--deep", "--strict", "--verbose=2", bundle])


def unsigned_mac(stage):
    preserved = 0
    for name in BUNDLES:
        bundle = stage / name
        details = run(["codesign", "-dv", "--verbose=4", bundle], allow_failure=True)
        if "Authority=Developer ID Application:" in details.stdout + details.stderr:
            # Never convert an existing Developer ID signature to an ad-hoc one.
            verify_mac_bundle(bundle)
            preserved += 1
        else:
            run(["codesign", "--force", "--deep", "--sign", "-", bundle])
            verify_mac_bundle(bundle)
    return preserved


def signed_mac(stage):
    temp_root = Path(os.environ.get("RUNNER_TEMP", tempfile.gettempdir()))
    temp_root.mkdir(parents=True, exist_ok=True)
    keychain = temp_root / f"rd-signing-{os.getpid()}.keychain-db"
    try:
        with tempfile.TemporaryDirectory(prefix="rd-signing-input-", dir=temp_root) as directory:
            directory = Path(directory)
            certificate = directory / "certificate.p12"
            decode_secret("MACOS_CERT_P12_BASE64", certificate)
            password = os.environ["MACOS_KEYCHAIN_PASSWORD"]
            run(["security", "create-keychain", "-p", password, keychain])
            run(["security", "unlock-keychain", "-p", password, keychain])
            run(["security", "import", certificate, "-k", keychain, "-P", os.environ["MACOS_CERT_PASSWORD"], "-T", "/usr/bin/codesign"])
            run(["security", "set-key-partition-list", "-S", "apple-tool:,apple:,codesign:", "-s", "-k", password, keychain])
            for name in BUNDLES:
                bundle = stage / name
                run(["codesign", "--force", "--deep", "--options", "runtime", "--timestamp", "--keychain", keychain,
                     "--sign", os.environ["MACOS_SIGN_IDENTITY"], bundle])
                verify_mac_bundle(bundle)
            archive = directory / "notarization.zip"
            run(["ditto", "-c", "-k", "--keepParent", stage, archive])
            credentials = []
            if configured(MAC_API):
                key = directory / "notary.p8"
                decode_secret("APPLE_API_KEY_P8_BASE64", key)
                credentials = ["--key", key, "--key-id", os.environ["APPLE_API_KEY_ID"], "--issuer", os.environ["APPLE_API_ISSUER"]]
            else:
                credentials = ["--apple-id", os.environ["APPLE_ID"], "--team-id", os.environ["APPLE_TEAM_ID"], "--password", os.environ["APPLE_APP_PASSWORD"]]
            submission = run(["xcrun", "notarytool", "submit", archive, *credentials, "--wait", "--output-format", "json"])
            try:
                accepted = json.loads(submission.stdout).get("status") == "Accepted"
            except (ValueError, AttributeError):
                accepted = False
            if not accepted:
                raise SigningError("Notarization was not accepted")
            # Apple's stapler supports apps/packages/disk images, not arbitrary
            # plug-in bundle extensions. The accepted zip covers every plug-in;
            # staple the app before creating the final distributed zip.
            app = stage / "RoboDrummer.app"
            run(["xcrun", "stapler", "staple", app])
            run(["xcrun", "stapler", "validate", app])
            run(["spctl", "--assess", "--type", "exec", "--verbose=2", app])
            for name in BUNDLES:
                verify_mac_bundle(stage / name)
    finally:
        # Also covered by the workflow's always-cleanup step after interruption.
        if keychain.exists():
            run(["security", "delete-keychain", keychain])


def sign_stage(platform, version, stage):
    mode = policy(platform, version)
    report = {"platform": platform, "version": version, "mode": mode, "notarized": False, "preserved_developer_id_bundles": 0}
    target = stage / "SIGNING.json"
    target.unlink(missing_ok=True)
    if platform.startswith("mac-"):
        if mode == "signed":
            signed_mac(stage)
            report["notarized"] = True
        else:
            report["preserved_developer_id_bundles"] = unsigned_mac(stage)
    elif platform == "windows-x64" and mode == "signed":
        for relative in ("RoboDrummer.vst3/Contents/x86_64-win/RoboDrummer.vst3", "RoboDrummer.clap", "RoboDrummer.exe"):
            sign_windows_file(version, stage / relative)
    target.write_text(json.dumps(report, indent=2) + "\n")
    print(f"Packaging signing status: {mode}")


def cleanup_keychains():
    root = Path(os.environ.get("RUNNER_TEMP", tempfile.gettempdir()))
    for keychain in root.glob("rd-signing-*.keychain-db"):
        run(["security", "delete-keychain", keychain])
    for prefix in ("rd-signing-input-", "rd-signing-windows-"):
        for directory in root.glob(prefix + "*"):
            if directory.is_symlink(): directory.unlink()
            elif directory.is_dir(): shutil.rmtree(directory)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    for action in ("policy", "stage"):
        command = sub.add_parser(action)
        command.add_argument("platform")
        command.add_argument("version")
        if action == "stage": command.add_argument("directory", type=Path)
    for action in ("windows-file", "verify-windows-file"):
        command = sub.add_parser(action)
        command.add_argument("version")
        command.add_argument("file", type=Path)
    sub.add_parser("cleanup")
    args = parser.parse_args()
    try:
        if args.action == "policy": print(policy(args.platform, args.version))
        elif args.action == "stage": sign_stage(args.platform, args.version, args.directory)
        elif args.action == "windows-file": sign_windows_file(args.version, args.file)
        elif args.action == "verify-windows-file":
            if policy("windows-x64", args.version) == "signed": verify_windows_file(args.file)
        else: cleanup_keychains()
    except (SigningError, OSError) as error:
        # OSError filenames may include certificate temporary paths, never secrets.
        print(f"::error::{error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    def interrupted(number, _frame):
        raise SystemExit(128 + number)
    signal.signal(signal.SIGTERM, interrupted)
    sys.exit(main())
