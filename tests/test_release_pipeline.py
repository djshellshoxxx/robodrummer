"""Exercise release planning/publication without network or real release writes."""

import os
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
PLATFORMS = "windows-x64 mac-arm64 mac-x64 linux-x64"


class ReleasePipelineTests(unittest.TestCase):
    def plan(self, **overrides):
        env = dict(os.environ, EVENT_NAME="push", REF_NAME="v0.4.0-beta.1")
        env.update(overrides)
        env.pop("GITHUB_OUTPUT", None)
        return subprocess.run(
            ["bash", "scripts/ci/plan.sh"], cwd=ROOT, env=env,
            text=True, capture_output=True, check=False,
        )

    def test_tagged_releases_always_run_qa(self):
        result = self.plan()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("run_qa=true", result.stdout)

    def test_version_must_match_compiled_plugin(self):
        result = self.plan(REF_NAME="v9.9.9-beta.1")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("CMake", result.stdout + result.stderr)

    def test_beta_cannot_be_marked_stable(self):
        result = self.plan(
            EVENT_NAME="workflow_dispatch", IN_VERSION="0.4.0-beta.1",
            IN_PRERELEASE="false", IN_PLATFORMS="all",
        )
        self.assertNotEqual(result.returncode, 0)

    def test_mac_targets_include_au(self):
        result = subprocess.run(
            ["bash", "scripts/ci/targets.sh", "mac"], cwd=ROOT,
            text=True, capture_output=True, check=True,
        )
        self.assertIn("RoboDrummer_AU", result.stdout.split())

    def publish(self, payloads, expected=PLATFORMS, existing_release=False):
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            artifacts = temp / "artifacts"
            artifacts.mkdir()
            for name in payloads:
                (artifacts / name).write_bytes(b"test release payload")
            bindir = temp / "bin"
            bindir.mkdir()
            log = temp / "gh.log"
            gh = bindir / "gh"
            gh.write_text(
                '#!/bin/bash\nprintf "%s\\n" "$*" >> "$GH_TEST_LOG"\n'
                'if [ "$1 $2" = "release view" ]; then '
                'exit "$GH_TEST_EXISTING_STATUS"; fi\n'
            )
            gh.chmod(0o755)
            env = dict(
                os.environ, PATH=f"{bindir}:{os.environ['PATH']}",
                GH_TEST_LOG=str(log), GH_TEST_EXISTING_STATUS="0" if existing_release else "1",
                GITHUB_REPOSITORY="example/robodrummer", GITHUB_SHA="1" * 40,
                VERSION="0.4.0-beta.1", BASE_VERSION="0.4.0",
                TAG="v0.4.0-beta.1", PRERELEASE="true", EXPECTED=expected,
            )
            result = subprocess.run(
                ["bash", str(ROOT / "scripts/ci/publish.sh"), str(artifacts)],
                cwd=ROOT, env=env, text=True, capture_output=True, check=False,
            )
            calls = log.read_text() if log.exists() else ""
            sums = (artifacts / "SHA256SUMS.txt")
            return result, calls, sums.read_text() if sums.exists() else ""

    @staticmethod
    def complete_payloads():
        prefix = "RoboDrummer-0.4.0-beta.1-"
        return [prefix + name for name in (
            "windows-x64.zip", "windows-x64-setup.exe", "mac-arm64.zip",
            "mac-x64.zip", "linux-x64.tar.gz",
        )]

    def test_incomplete_matrix_never_calls_github(self):
        result, calls, _ = self.publish([self.complete_payloads()[0]])
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(calls, "")

    def test_missing_windows_installer_blocks_publication(self):
        payloads = self.complete_payloads()
        payloads.remove("RoboDrummer-0.4.0-beta.1-windows-x64-setup.exe")
        result, calls, _ = self.publish(payloads)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(calls, "")

    def test_subset_cannot_publish_incomplete_release(self):
        result, calls, _ = self.publish([self.complete_payloads()[0]], "windows-x64")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(calls, "")

    def test_complete_matrix_creates_draft_with_checksums(self):
        payloads = self.complete_payloads()
        result, calls, sums = self.publish(payloads)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("release create", calls)
        self.assertIn("--draft", calls)
        self.assertNotIn("--clobber", calls)
        self.assertEqual(len(sums.splitlines()), len(payloads))
        for payload in payloads:
            self.assertIn(payload, sums)

    def test_existing_release_is_immutable(self):
        result, calls, _ = self.publish(self.complete_payloads(), existing_release=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("release edit", calls)
        self.assertNotIn("release upload", calls)

    def test_linux_package_contains_formats_help_parameters_and_notices(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            build, out = temp / "build", temp / "out"
            artifacts = build / "RoboDrummer_artefacts/Release"
            for name in (
                "VST3/RoboDrummer.vst3/Contents/x86_64-linux/RoboDrummer.so",
                "CLAP/RoboDrummer.clap", "Standalone/RoboDrummer",
            ):
                path = artifacts / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"fixture build output")
            notices = build / "third-party-notices"
            notices.mkdir()
            for name in ("JUCE-LICENSE.md", "CLAP-LICENSE"):
                (notices / name).write_text("fixture dependency notice")
            result = subprocess.run(
                ["bash", "scripts/ci/package.sh", "linux-x64", "0.4.0-beta.1", str(build), str(out)],
                cwd=ROOT, text=True, capture_output=True, check=False,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            archive = out / "RoboDrummer-0.4.0-beta.1-linux-x64.tar.gz"
            with tarfile.open(archive) as package:
                names = package.getnames()
                prefix = "RoboDrummer-0.4.0-beta.1-linux-x64/"
                for name in (
                    "RoboDrummer.vst3/Contents/x86_64-linux/RoboDrummer.so",
                    "RoboDrummer.clap", "RoboDrummer", "LICENSE", "INSTALL.md",
                    "PARAMETERS.md", "HELP.md", "third-party-notices/JUCE-LICENSE.md",
                    "third-party-notices/CLAP-LICENSE",
                ):
                    self.assertIn(prefix + name, names)
            verify = subprocess.run(
                ["sha256sum", "--check", archive.name + ".sha256"], cwd=out,
                text=True, capture_output=True, check=False,
            )
            self.assertEqual(verify.returncode, 0, verify.stdout + verify.stderr)

    def test_empty_plugin_bundle_cannot_be_packaged(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            build, out = temp / "build", temp / "out"
            artifacts = build / "RoboDrummer_artefacts/Release"
            (artifacts / "VST3/RoboDrummer.vst3").mkdir(parents=True)
            for name in ("CLAP/RoboDrummer.clap", "Standalone/RoboDrummer"):
                path = artifacts / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"fixture build output")
            result = subprocess.run(
                ["bash", "scripts/ci/package.sh", "linux-x64", "0.4.0-beta.1", str(build), str(out)],
                cwd=ROOT, text=True, capture_output=True, check=False,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("RoboDrummer.so", result.stdout + result.stderr)
            self.assertFalse(list(out.glob("*.tar.gz")))


if __name__ == "__main__":
    unittest.main()
