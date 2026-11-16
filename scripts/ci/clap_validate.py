#!/usr/bin/env python3
"""Pinned CLAP validation, reported separately from VST3 pluginval.

No tests are excluded. An upstream success exit cannot hide failed report rows.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import platform as host_platform
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
import zipfile

VERSION = "0.4.1"
ASSETS = {
    "linux-x64": ("ubuntu-22.04", "49edadcfb407ea0dd946ce418300e853fbd2660fa4b0d00e4f19ff8eef24ad90"),
    "windows-x64": ("windows", "d935c3af0a45c3911ea2e900f4aa5d6709dac82bb485f0c4ce28648ab2cd0c10"),
    "mac-arm64": ("macos-15-aarch64", "719a0248ea431718bb7c92c8a0b9a78afa0bb23d692cf452e80973fe8b89282d"),
    "mac-x64": ("macos-15-x86_64", "c40d28171e581313bc5053e8de887035dcd2942cd733569e80a466cb11a03c25"),
}


def report_from_output(output):
    # Some plug-in wrappers print diagnostics before the validator's JSON.
    for offset, char in enumerate(output):
        if char != "{": continue
        try: report, _ = json.JSONDecoder().raw_decode(output[offset:])
        except ValueError: continue
        if isinstance(report, dict) and isinstance(report.get("results"), list): return report
    raise ValueError("CLAP validator produced no structured result report")


def validate_report(report):
    rows = report.get("results")
    if not isinstance(rows, list) or not rows: raise ValueError("CLAP validator report is empty or malformed")
    known_statuses = {"success", "failed", "skipped", "warning"}
    for row in rows:
        if not isinstance(row, dict): raise ValueError("Malformed CLAP validator result row")
        test, status = row.get("test"), row.get("status")
        if not isinstance(test, dict) or len(test) != 1: raise ValueError("Malformed CLAP validator test identity")
        kind, identity = next(iter(test.items()))
        if kind not in ("plugin-library", "plugin-instance") or not isinstance(identity, dict): raise ValueError("Unknown CLAP validator test identity")
        if any(not isinstance(identity.get(field), str) or not identity[field] for field in ("test", "path")):
            raise ValueError("Missing CLAP validator test identity fields")
        if kind == "plugin-instance" and (not isinstance(identity.get("plugin_id"), str) or not identity["plugin_id"]):
            raise ValueError("Missing CLAP validator plug-in identity")
        if not isinstance(status, dict) or not isinstance(status.get("code"), str) or status["code"] not in known_statuses: raise ValueError("Missing or unknown CLAP validator result status")
        if "details" not in status: raise ValueError("Missing CLAP validator status details")
        if status.get("details") is not None and not isinstance(status["details"], str): raise ValueError("Malformed CLAP validator status details")
        duration = row.get("duration")
        if not isinstance(duration, dict) or any(type(duration.get(field)) is not int for field in ("secs", "nanos")) or duration["secs"] < 0 or not 0 <= duration["nanos"] < 1_000_000_000:
            raise ValueError("Malformed CLAP validator result duration")
    return rows


def download(platform, directory):
    suffix, digest = ASSETS[platform]
    name = f"clap-validator-{VERSION}-127-g152b982-{suffix}.zip"
    url = f"https://github.com/free-audio/clap-validator/releases/download/{VERSION}/{name}"
    with urllib.request.urlopen(url, timeout=60) as response: data = response.read()
    if hashlib.sha256(data).hexdigest() != digest: raise ValueError("Pinned CLAP validator checksum mismatch")
    executable = "clap-validator.exe" if platform == "windows-x64" else "clap-validator"
    binary = None
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        for entry in archive.infolist():
            if PurePosixPath(entry.filename).name == executable:
                binary = archive.read(entry)
                break
            if entry.filename.endswith(".tar.gz"):
                with tarfile.open(fileobj=io.BytesIO(archive.read(entry)), mode="r:gz") as nested:
                    for member in nested:
                        if member.isfile() and PurePosixPath(member.name).name == executable:
                            binary = nested.extractfile(member).read()
                            break
    if not binary: raise ValueError("Pinned CLAP validator asset has no expected executable")
    path = directory / executable
    path.write_bytes(binary)
    path.chmod(0o755)
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("plugin", type=Path)
    parser.add_argument("--platform", choices=ASSETS)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--validator", type=Path, help="Use an explicitly supplied binary (version checked; download digest not asserted)")
    args = parser.parse_args()
    selected = args.platform
    if selected is None:
        selected = "windows-x64" if sys.platform == "win32" else ("mac-arm64" if host_platform.machine() == "arm64" else "mac-x64") if sys.platform == "darwin" else "linux-x64"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    try:
        with tempfile.TemporaryDirectory(prefix="rd-clap-validator-") as temp:
            validator = args.validator.resolve() if args.validator else download(selected, Path(temp))
            version = subprocess.run([str(validator), "--version"], capture_output=True, text=True, check=False)
            if version.returncode or version.stdout.strip() != f"clap-validator {VERSION}": raise ValueError("Unexpected CLAP validator version")
            result = subprocess.run([str(validator), "validate", "--json", "--jobs", "2", str(args.plugin.resolve())], capture_output=True, text=True, check=False)
            args.output.with_suffix(".stdout.log").write_text(result.stdout)
            args.output.with_suffix(".stderr.log").write_text(result.stderr)
            report = report_from_output(result.stdout)
            report["validator_version"] = VERSION
            report["download_sha256_verified"] = args.validator is None
            report["exit_code"] = result.returncode
            try:
                rows = validate_report(report)
            except ValueError as error:
                report["schema_valid"] = False
                report["validation_error"] = str(error)
                args.output.write_text(json.dumps(report, indent=2) + "\n")
                raise
            report["schema_valid"] = True
            args.output.write_text(json.dumps(report, indent=2) + "\n")
            failures = sum(row.get("status", {}).get("code") == "failed" for row in rows)
            print(f"CLAP validator {VERSION}: {len(rows)} result rows, {failures} failures (separate from VST3 validation)")
            return 1 if result.returncode or failures else 0
    except (OSError, ValueError, zipfile.BadZipFile, tarfile.TarError) as error:
        print(f"::error::{error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
