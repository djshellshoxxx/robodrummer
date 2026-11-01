#!/usr/bin/env python3
"""Extract a final native package and verify payloads, architecture and checksums."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import struct
import sys
import tarfile
import tempfile
import zipfile

from signing import BUNDLES, SigningError, run, verify_mac_bundle, verify_windows_file

DOCS = ("LICENSE", "COPYRIGHT-TRADEMARK.md", "INSTALL.md", "PARAMETERS.md", "HELP.md",
        "third-party-notices/JUCE-LICENSE.md", "third-party-notices/JUCE.spdx.json", "third-party-notices/CLAP-LICENSE",
        "third-party-notices/clap-juce-extensions-LICENSE.md", "third-party-notices/clap-helpers-LICENSE")


class PackageError(RuntimeError):
    pass


def checksum(path):
    manifest = path.with_name(path.name + ".sha256")
    try:
        fields = manifest.read_text().strip().split()
        if len(fields) != 2 or fields[1].lstrip("*") != path.name or not re.fullmatch("[a-fA-F0-9]{64}", fields[0]):
            raise ValueError()
        digest = hashlib.sha256()
        with path.open("rb") as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b""): digest.update(chunk)
        if digest.hexdigest() != fields[0].lower(): raise ValueError()
    except (OSError, ValueError):
        raise PackageError(f"Missing or invalid final checksum: {path.name}") from None


def member_path(name, root, prefix):
    parts = PurePosixPath(name).parts
    if not parts or "\\" in name or ":" in name or name.startswith("/") or ".." in parts:
        raise PackageError("Unsafe archive member path")
    if parts[0] == "__MACOSX": return None  # ditto resource-fork metadata, never an executable payload
    if parts[0] != prefix: raise PackageError("Archive member is outside the package root")
    return root.joinpath(*parts)


def extract(archive, root, prefix):
    seen = set()
    total = 0
    if archive.name.endswith(".zip"):
        with zipfile.ZipFile(archive) as source:
            entries = source.infolist()
            for entry in entries:
                target = member_path(entry.filename, root, prefix)
                mode = entry.external_attr >> 16
                if stat.S_ISLNK(mode): raise PackageError("Unsafe archive symbolic link")
                total += entry.file_size
                if total > 4 * 1024**3: raise PackageError("Package exceeds extraction budget")
                if target is None: continue
                if entry.is_dir(): target.mkdir(parents=True, exist_ok=True); continue
                if target in seen: raise PackageError("Duplicate archive payload")
                seen.add(target)
                target.parent.mkdir(parents=True, exist_ok=True)
                with source.open(entry) as data, target.open("wb") as output:
                    while chunk := data.read(1024 * 1024): output.write(chunk)
                target.chmod((mode & 0o777) or 0o644)
    else:
        with tarfile.open(archive, "r:gz") as source:
            for entry in source:
                target = member_path(entry.name, root, prefix)
                if not (entry.isdir() or entry.isfile()): raise PackageError("Unsafe archive link or special file")
                total += entry.size
                if total > 4 * 1024**3: raise PackageError("Package exceeds extraction budget")
                if target is None: continue
                if entry.isdir(): target.mkdir(parents=True, exist_ok=True); continue
                if target in seen: raise PackageError("Duplicate archive payload")
                seen.add(target)
                target.parent.mkdir(parents=True, exist_ok=True)
                with source.extractfile(entry) as data, target.open("wb") as output:
                    while chunk := data.read(1024 * 1024): output.write(chunk)
                target.chmod(entry.mode & 0o777)
    return root / prefix


def required(path):
    if not path.is_file() or path.stat().st_size == 0:
        raise PackageError(f"Missing or empty package payload: {path.name}")
    return path


def pe_identity(data):
    if len(data) < 64 or data[:2] != b"MZ": raise PackageError("Expected a Windows PE payload")
    offset = struct.unpack_from("<I", data, 0x3c)[0]
    if offset + 26 > len(data) or data[offset:offset + 4] != b"PE\0\0": raise PackageError("Invalid Windows PE header")
    return struct.unpack_from("<H", data, offset + 4)[0], struct.unpack_from("<H", data, offset + 24)[0]


def installer_architecture(path):
    # Inno's default bootstrap is x86 even when every installed binary is x64.
    # Accept that supported bootstrap without weakening the shipped formats.
    if pe_identity(required(path).read_bytes()) not in ((0x14c, 0x10b), (0x8664, 0x20b)):
        raise PackageError("Expected a supported PE32 x86 or PE32+ x64 installer bootstrap")


def architecture(path, platform):
    data = required(path).read_bytes()
    if platform == "linux-x64":
        if len(data) < 64 or data[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<H", data, 18)[0] != 62:
            raise PackageError("Expected a native x86_64 ELF payload")
    elif platform == "windows-x64":
        if pe_identity(data) != (0x8664, 0x20b):
            raise PackageError("Expected a native x86_64 PE32+ payload")
    else:
        expected = 0x100000c if platform == "mac-arm64" else 0x1000007
        if len(data) >= 32 and data[:4] == b"\xcf\xfa\xed\xfe":
            cpus = [struct.unpack_from("<I", data, 4)[0]]
        elif len(data) >= 8 and data[:4] in (b"\xca\xfe\xba\xbe", b"\xca\xfe\xba\xbf"):
            count = struct.unpack_from(">I", data, 4)[0]
            stride = 32 if data[:4] == b"\xca\xfe\xba\xbf" else 20
            if count > 32 or 8 + count * stride > len(data): raise PackageError("Invalid universal Mach-O header")
            cpus = []
            for index in range(count):
                entry = 8 + index * stride
                cpu = struct.unpack_from(">I", data, entry)[0]
                offset, size = struct.unpack_from(">QQ" if stride == 32 else ">II", data, entry + 8)
                if offset < 8 + count * stride or size < 32 or offset + size > len(data): raise PackageError("Invalid universal Mach-O slice")
                if data[offset:offset + 4] != b"\xcf\xfa\xed\xfe" or struct.unpack_from("<I", data, offset + 4)[0] != cpu: raise PackageError("Universal Mach-O architecture disagrees with its slice")
                cpus.append(cpu)
        else: raise PackageError("Expected a Mach-O payload")
        if expected not in cpus: raise PackageError("Mach-O payload lacks the requested native architecture")


def validate(platform, version, directory, native=False):
    if platform not in ("linux-x64", "windows-x64", "mac-arm64", "mac-x64"):
        raise PackageError("Unsupported package platform")
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.]+)?", version):
        raise PackageError("Invalid package version")
    prefix = f"RoboDrummer-{version}-{platform}"
    archive = directory / (prefix + (".tar.gz" if platform == "linux-x64" else ".zip"))
    checksum(archive)
    with tempfile.TemporaryDirectory(prefix="rd-package-check-") as temp:
        package = extract(archive, Path(temp), prefix)
        for name in DOCS: required(package / name)
        try:
            report = json.loads(required(package / "SIGNING.json").read_text())
        except (ValueError, AttributeError):
            raise PackageError("Invalid package signing report") from None
        if not isinstance(report, dict): raise PackageError("Invalid package signing report")
        if report.get("platform") != platform or report.get("version") != version:
            raise PackageError("Package signing report does not match its version/platform")
        expected_modes = ("checksums-only",) if platform == "linux-x64" else ("signed", "unsigned-beta")
        if report.get("mode") not in expected_modes: raise PackageError("Invalid package signing mode")
        signed = report.get("mode") == "signed"
        if platform != "linux-x64" and "-" not in version and not signed:
            raise PackageError("Stable package is unsigned")
        if platform.startswith("mac-"):
            binaries = [package / name / "Contents/MacOS/RoboDrummer" for name in BUNDLES]
            for name in BUNDLES: required(package / name / "Contents/Info.plist")
            if signed and report.get("notarized") is not True: raise PackageError("Signed macOS package lacks accepted notarization")
        elif platform == "windows-x64":
            binaries = [package / name for name in ("RoboDrummer.vst3/Contents/x86_64-win/RoboDrummer.vst3", "RoboDrummer.clap", "RoboDrummer.exe")]
            installer = directory / (prefix + "-setup.exe")
            checksum(installer)
            binaries.append(installer)
        else:
            binaries = [package / name for name in ("RoboDrummer.vst3/Contents/x86_64-linux/RoboDrummer.so", "RoboDrummer.clap", "RoboDrummer")]
        for binary in binaries:
            if platform == "windows-x64" and binary == installer: installer_architecture(binary)
            else: architecture(binary, platform)
        if native:
            if platform.startswith("mac-"):
                for name in BUNDLES: verify_mac_bundle(package / name)
                arch = "arm64" if platform == "mac-arm64" else "x86_64"
                for binary in binaries: run(["lipo", binary, "-verify_arch", arch])
                if signed:
                    run(["xcrun", "stapler", "validate", package / "RoboDrummer.app"])
                    run(["spctl", "--assess", "--type", "exec", "--verbose=2", package / "RoboDrummer.app"])
            elif platform == "windows-x64" and signed:
                for binary in binaries: verify_windows_file(binary)
            elif platform == "linux-x64" and not os.access(package / "RoboDrummer", os.X_OK):
                raise PackageError("Extracted standalone is not executable")
    signature_check = "not-run"
    if native:
        signature_check = "verified" if platform.startswith("mac-") or (platform == "windows-x64" and signed) else "not-required"
    print(f"Validated final {platform} archive, required payloads, architecture and checksums; native signatures: {signature_check}")
    return {"status": "passed", "platform": platform, "version": version, "archive": archive.name,
            "archive_sha256": hashlib.sha256(archive.read_bytes()).hexdigest(), "signing_mode": report["mode"],
            "native_signature_checks": signature_check}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("platform")
    parser.add_argument("version")
    parser.add_argument("directory", type=Path)
    parser.add_argument("--native", action="store_true")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    try:
        report = validate(args.platform, args.version, args.directory, args.native)
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(json.dumps(report, indent=2) + "\n")
    except (PackageError, SigningError, OSError, zipfile.BadZipFile, tarfile.TarError, struct.error) as error:
        print(f"::error::{error}", file=sys.stderr)
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(json.dumps({"status": "failed", "error": str(error)}, indent=2) + "\n")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
