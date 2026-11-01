"""Validate extracted payloads and final checksums without native SDK tools."""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tarfile
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts/ci/validate_packages.py"
DOCS = ("LICENSE", "COPYRIGHT-TRADEMARK.md", "INSTALL.md", "PARAMETERS.md", "HELP.md",
        "third-party-notices/JUCE-LICENSE.md", "third-party-notices/JUCE.spdx.json", "third-party-notices/CLAP-LICENSE",
        "third-party-notices/clap-juce-extensions-LICENSE.md", "third-party-notices/clap-helpers-LICENSE")


def elf(machine=62):
    data = bytearray(64)
    data[:6] = b"\x7fELF\x02\x01"
    struct.pack_into("<HH", data, 16, 3, machine)
    return bytes(data)


def pe(machine=0x8664, magic=0x20b):
    data = bytearray(256)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3c, 64)
    data[64:68] = b"PE\0\0"
    struct.pack_into("<H", data, 68, machine)
    struct.pack_into("<H", data, 88, magic)
    return bytes(data)


def macho(cpu):
    return struct.pack("<IIIIIIII", 0xfeedfacf, cpu, 0, 2, 0, 0, 0, 0)


class PackageValidationTests(unittest.TestCase):
    def fixture(self, root, platform, version="0.4.0-beta.1", mutate=None):
        prefix = f"RoboDrummer-{version}-{platform}"
        stage = root / prefix
        stage.mkdir()
        files = {name: b"fixture documentation\n" for name in DOCS}
        if platform == "linux-x64":
            files.update({name: elf() for name in ("RoboDrummer.vst3/Contents/x86_64-linux/RoboDrummer.so", "RoboDrummer.clap", "RoboDrummer")})
        elif platform == "windows-x64":
            files.update({name: pe() for name in ("RoboDrummer.vst3/Contents/x86_64-win/RoboDrummer.vst3", "RoboDrummer.clap", "RoboDrummer.exe")})
        else:
            cpu = 0x100000c if platform == "mac-arm64" else 0x1000007
            for bundle in ("vst3", "clap", "component", "app"):
                files[f"RoboDrummer.{bundle}/Contents/MacOS/RoboDrummer"] = macho(cpu)
                files[f"RoboDrummer.{bundle}/Contents/Info.plist"] = b"fixture plist"
        files["SIGNING.json"] = json.dumps({"platform": platform, "version": version, "mode": "checksums-only" if platform == "linux-x64" else "unsigned-beta", "notarized": False}).encode()
        if mutate: mutate(files)
        for name, content in files.items():
            path = stage / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
        archive = root / (prefix + (".tar.gz" if platform == "linux-x64" else ".zip"))
        if platform == "linux-x64":
            with tarfile.open(archive, "w:gz") as output: output.add(stage, arcname=prefix)
        else:
            with zipfile.ZipFile(archive, "w") as output:
                for path in stage.rglob("*"):
                    if path.is_file(): output.write(path, arcname=str(path.relative_to(root)))
        self.checksum(archive)
        if platform == "windows-x64":
            installer = root / (prefix + "-setup.exe")
            installer.write_bytes(pe())
            self.checksum(installer)
        return archive

    @staticmethod
    def checksum(path):
        path.with_name(path.name + ".sha256").write_text(hashlib.sha256(path.read_bytes()).hexdigest() + "  " + path.name + "\n")

    def validate(self, root, platform, version="0.4.0-beta.1"):
        return subprocess.run(["python3", "-B", str(SCRIPT), platform, version, str(root)], text=True, capture_output=True)

    def test_all_four_archives_extract_and_validate(self):
        for platform in ("linux-x64", "windows-x64", "mac-arm64", "mac-x64"):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                self.fixture(root, platform)
                result = self.validate(root, platform)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_missing_empty_and_wrong_arch_payloads_fail(self):
        mutations = (
            lambda files: files.pop("third-party-notices/CLAP-LICENSE"),
            lambda files: files.update({"RoboDrummer.clap": b""}),
            lambda files: files.update({"RoboDrummer.clap": elf(183)}),
        )
        for mutate in mutations:
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                self.fixture(root, "linux-x64", mutate=mutate)
                result = self.validate(root, "linux-x64")
                self.assertNotEqual(result.returncode, 0)

    def test_every_dependency_license_and_sbom_is_required(self):
        for record in DOCS:
            if not record.startswith("third-party-notices/"): continue
            with self.subTest(record=record), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                self.fixture(root, "linux-x64", mutate=lambda files: files.pop(record))
                result = self.validate(root, "linux-x64")
                self.assertNotEqual(result.returncode, 0, "missing dependency record was accepted: " + record)

    def test_tampering_after_checksum_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = self.fixture(root, "linux-x64")
            with archive.open("ab") as stream: stream.write(b"tampered")
            result = self.validate(root, "linux-x64")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("checksum", result.stderr.lower())

    def test_path_traversal_is_rejected_before_extraction(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = self.fixture(root, "windows-x64")
            with zipfile.ZipFile(archive, "a") as output: output.writestr("../escaped", b"bad")
            self.checksum(archive)
            result = self.validate(root, "windows-x64")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("unsafe", result.stderr.lower())
            self.assertFalse((root.parent / "escaped").exists())

    def test_x86_bootstrap_is_allowed_only_for_the_installer(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root, "windows-x64")
            installer = next(root.glob("*-setup.exe"))
            installer.write_bytes(pe(0x14c, 0x10b))
            self.checksum(installer)
            result = self.validate(root, "windows-x64")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for payload in ("RoboDrummer.vst3/Contents/x86_64-win/RoboDrummer.vst3", "RoboDrummer.clap", "RoboDrummer.exe"):
            with self.subTest(payload=payload), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                self.fixture(root, "windows-x64", mutate=lambda files: files.update({payload: pe(0x14c, 0x10b)}))
                result = self.validate(root, "windows-x64")
                self.assertNotEqual(result.returncode, 0, "x86 payload was accepted as a native x64 format")

    def test_stable_unsigned_and_missing_installer_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root, "windows-x64", version="0.4.0")
            result = self.validate(root, "windows-x64", version="0.4.0")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("unsigned", result.stderr.lower())
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root, "windows-x64")
            next(root.glob("*-setup.exe")).unlink()
            result = self.validate(root, "windows-x64")
            self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
