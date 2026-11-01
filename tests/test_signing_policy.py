"""Signing command adapters with controlled tools; no real certificates or services."""
import base64
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts/ci/signing.py"
SECRET_NAMES = (
    "WIN_CERT_PFX_BASE64", "WIN_CERT_PASSWORD", "WIN_TIMESTAMP_URL", "SIGNTOOL",
    "MACOS_CERT_P12_BASE64", "MACOS_CERT_PASSWORD", "MACOS_KEYCHAIN_PASSWORD", "MACOS_SIGN_IDENTITY",
    "APPLE_ID", "APPLE_TEAM_ID", "APPLE_APP_PASSWORD", "APPLE_API_KEY_P8_BASE64", "APPLE_API_KEY_ID", "APPLE_API_ISSUER",
    "AZURE_TENANT_ID", "AZURE_CLIENT_ID", "AZURE_CLIENT_SECRET", "AZURE_TS_ENDPOINT", "AZURE_TS_ACCOUNT", "AZURE_TS_PROFILE", "AZURE_SIGNING_DLIB",
)


class SigningTests(unittest.TestCase):
    def run_script(self, *args, extra=None):
        env = {k: v for k, v in os.environ.items() if k not in SECRET_NAMES}
        env.update(extra or {})
        return subprocess.run(["python3", "-B", str(SCRIPT), *args], env=env, text=True, capture_output=True)

    def test_only_prereleases_may_be_unsigned(self):
        for platform in ("windows-x64", "mac-arm64", "mac-x64"):
            beta = self.run_script("policy", platform, "0.4.0-beta.1")
            self.assertEqual(beta.returncode, 0, beta.stderr)
            self.assertEqual(beta.stdout.strip(), "unsigned-beta")
            stable = self.run_script("policy", platform, "0.4.0")
            self.assertNotEqual(stable.returncode, 0)
            self.assertIn("requires signing", stable.stderr)
        linux = self.run_script("policy", "linux-x64", "0.4.0")
        self.assertEqual(linux.returncode, 0, linux.stderr)
        self.assertEqual(linux.stdout.strip(), "checksums-only")

    def test_partial_credentials_fail_even_for_beta(self):
        for platform, extra in (
            ("windows-x64", {"WIN_CERT_PFX_BASE64": "certificate"}),
            ("windows-x64", {"AZURE_CLIENT_ID": "partial-azure"}),
            ("mac-arm64", {"MACOS_SIGN_IDENTITY": "Developer ID Application: Fixture"}),
            ("mac-x64", {"APPLE_ID": "fixture@example.test"}),
        ):
            result = self.run_script("policy", platform, "0.4.0-beta.1", extra=extra)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("configuration", result.stderr)
            for value in extra.values():
                self.assertNotIn(value, result.stderr)

    def tools(self, temp):
        bindir = temp / "bin"
        bindir.mkdir()
        tool = bindir / "tool.py"
        tool.write_text('''#!/usr/bin/env python3
import json, os, pathlib, sys
name=pathlib.Path(sys.argv[0]).name
with open(os.environ["TOOL_LOG"],"a") as log: log.write(json.dumps([name,*sys.argv[1:]])+"\\n")
if name=="codesign" and "-dv" in sys.argv:
    print("Authority=Developer ID Application: Existing" if os.environ.get("EXISTING_DEVELOPER_ID")=="1" else "Signature=adhoc",file=sys.stderr)
if name=="xcrun" and "notarytool" in sys.argv:
    print(json.dumps({"status":os.environ.get("NOTARY_STATUS","Accepted")}))
if name=="security" and "create-keychain" in sys.argv:
    pathlib.Path(sys.argv[-1]).touch()
if name=="security" and "delete-keychain" in sys.argv:
    pathlib.Path(sys.argv[-1]).unlink(missing_ok=True)
if os.environ.get("FAIL_TOOL")==name and os.environ.get("FAIL_ARG","") in sys.argv: sys.exit(19)
''')
        for name in ("codesign", "security", "xcrun", "spctl", "ditto", "signtool"):
            path = bindir / name
            path.symlink_to(tool)
        tool.chmod(0o755)
        return {"PATH": str(bindir) + os.pathsep + os.environ["PATH"], "TOOL_LOG": str(temp / "tools.jsonl"), "RUNNER_TEMP": str(temp)}

    @staticmethod
    def mac_stage(temp):
        stage = temp / "stage"
        stage.mkdir()
        for name in ("RoboDrummer.vst3", "RoboDrummer.clap", "RoboDrummer.component", "RoboDrummer.app"):
            (stage / name).mkdir()
        return stage

    @staticmethod
    def mac_credentials():
        return dict(MACOS_CERT_P12_BASE64=base64.b64encode(b"fixture-certificate").decode(), MACOS_CERT_PASSWORD="fixture-password",
                    MACOS_KEYCHAIN_PASSWORD="fixture-keychain-password", MACOS_SIGN_IDENTITY="Developer ID Application: Fixture (FIXTURE)",
                    APPLE_ID="fixture@example.test", APPLE_TEAM_ID="FIXTURE", APPLE_APP_PASSWORD="fixture-app-password")

    def test_unsigned_beta_preserves_existing_developer_id(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            env = self.tools(temp)
            env["EXISTING_DEVELOPER_ID"] = "1"
            stage = self.mac_stage(temp)
            result = self.run_script("stage", "mac-arm64", "0.4.0-beta.1", str(stage), extra=env)
            self.assertEqual(result.returncode, 0, result.stderr)
            commands = [json.loads(line) for line in (temp / "tools.jsonl").read_text().splitlines()]
            self.assertFalse(any("--sign" in call and "-" in call for call in commands))
            self.assertEqual(sum("--verify" in call for call in commands), 4)
            report = json.loads((stage / "SIGNING.json").read_text())
            self.assertEqual(report["preserved_developer_id_bundles"], 4)
            self.assertFalse(report["notarized"])

    def test_mac_notarization_and_cleanup_on_success_or_failure(self):
        for status, expected in (("Accepted", 0), ("Invalid", 1)):
            with self.subTest(status=status), tempfile.TemporaryDirectory() as directory:
                temp = Path(directory)
                env = self.tools(temp) | self.mac_credentials() | {"NOTARY_STATUS": status}
                stage = self.mac_stage(temp)
                result = self.run_script("stage", "mac-x64", "0.4.0", str(stage), extra=env)
                self.assertEqual(result.returncode, expected, result.stderr)
                commands = [json.loads(line) for line in (temp / "tools.jsonl").read_text().splitlines()]
                self.assertTrue(any(call[0:2] == ["security", "delete-keychain"] for call in commands))
                self.assertFalse(list(temp.glob("rd-signing-*.keychain-db")))
                if expected == 0:
                    self.assertTrue(json.loads((stage / "SIGNING.json").read_text())["notarized"])
                    self.assertTrue(any("staple" in call for call in commands))
                else:
                    self.assertFalse((stage / "SIGNING.json").exists())
                for key in ("MACOS_CERT_PASSWORD", "MACOS_KEYCHAIN_PASSWORD", "APPLE_APP_PASSWORD"):
                    self.assertNotIn(env[key], result.stdout + result.stderr)

    def test_always_cleanup_removes_interrupted_certificate_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            env = self.tools(temp)
            for prefix in ("rd-signing-input-", "rd-signing-windows-"):
                stale = temp / (prefix + "interrupted")
                stale.mkdir()
                (stale / "certificate").write_bytes(b"fixture-secret-data")
            (temp / "rd-signing-123.keychain-db").touch()
            result = self.run_script("cleanup", extra=env)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse(list(temp.glob("rd-signing-*")))

    def test_signing_or_verification_failure_blocks_package(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            env = self.tools(temp) | self.mac_credentials() | {"FAIL_TOOL": "codesign", "FAIL_ARG": "--verify"}
            stage = self.mac_stage(temp)
            result = self.run_script("stage", "mac-arm64", "0.4.0", str(stage), extra=env)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((stage / "SIGNING.json").exists())
            self.assertFalse(list(temp.glob("rd-signing-*.keychain-db")))

    def test_azure_adapter_uses_explicit_dlib_and_verifies(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            env = self.tools(temp) | dict(AZURE_TENANT_ID="fixture-tenant", AZURE_CLIENT_ID="fixture-client", AZURE_CLIENT_SECRET="fixture-secret",
                                         AZURE_TS_ENDPOINT="https://fixture.codesigning.azure.net", AZURE_TS_ACCOUNT="fixture-account", AZURE_TS_PROFILE="fixture-profile",
                                         AZURE_SIGNING_DLIB=str(temp / "Azure.CodeSigning.Dlib.dll"), SIGNTOOL=str(temp / "bin/signtool"))
            Path(env["AZURE_SIGNING_DLIB"]).write_bytes(b"fixture provider")
            binary = temp / "installer.exe"
            binary.write_bytes(b"fixture")
            result = self.run_script("windows-file", "0.4.0", str(binary), extra=env)
            self.assertEqual(result.returncode, 0, result.stderr)
            calls = [json.loads(line) for line in (temp / "tools.jsonl").read_text().splitlines()]
            self.assertIn("/dlib", calls[0])
            self.assertIn("/dmdf", calls[0])
            self.assertIn("http://timestamp.acs.microsoft.com", calls[0])
            self.assertEqual(calls[1][1:3], ["verify", "/pa"])
            self.assertNotIn(env["AZURE_CLIENT_SECRET"], result.stdout + result.stderr)

    def test_windows_timestamp_and_verification_are_required(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            env = self.tools(temp) | {"WIN_CERT_PFX_BASE64": base64.b64encode(b"fixture-pfx").decode(), "WIN_CERT_PASSWORD": "fixture-password", "SIGNTOOL": str(temp / "bin/signtool")}
            binary = temp / "installer.exe"
            binary.write_bytes(b"fixture")
            result = self.run_script("windows-file", "0.4.0", str(binary), extra=env)
            self.assertEqual(result.returncode, 0, result.stderr)
            calls = [json.loads(line) for line in (temp / "tools.jsonl").read_text().splitlines()]
            self.assertIn("/tr", calls[0])
            self.assertIn("SHA256", calls[0])
            self.assertEqual(calls[1][1:3], ["verify", "/pa"])
            result = self.run_script("windows-file", "0.4.0", str(binary), extra=env | {"FAIL_TOOL": "signtool", "FAIL_ARG": "verify"})
            self.assertNotEqual(result.returncode, 0)
            self.assertNotIn(env["WIN_CERT_PASSWORD"], result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
