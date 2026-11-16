"""Report/exit propagation with a controlled CLI, never a CLAP qualification run."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ClapValidatorTests(unittest.TestCase):
    def run_fixture(self, status, exit_code=0, version="0.4.1", rows=None):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            tool = root / "validator"
            if rows is None:
                rows = [{"test": {"plugin-library": {"test": "fixture-test", "path": "fixture.clap"}}, "status": {"code": status, "details": None}, "duration": {"secs": 0, "nanos": 0}}]
            tool.write_text('#!/usr/bin/env python3\nimport json,sys\n'
                            f'if "--version" in sys.argv: print("clap-validator {version}"); sys.exit(0)\n'
                            'print("wrapper diagnostic before JSON")\n'
                            f'print({json.dumps(json.dumps({"results": rows}))})\n'
                            f'sys.exit({exit_code})\n')
            tool.chmod(0o755)
            output = root / "report.json"
            result = subprocess.run(["python3", "-B", str(ROOT / "scripts/ci/clap_validate.py"), "fixture.clap", "--validator", str(tool), "--output", str(output)], text=True, capture_output=True)
            return result, json.loads(output.read_text()) if output.exists() else None

    def test_wrapped_report_is_retained(self):
        result, report = self.run_fixture("success")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(report["download_sha256_verified"])
        self.assertEqual(report["validator_version"], "0.4.1")

    def test_failed_report_or_native_exit_cannot_be_suppressed(self):
        for status, code in (("failed", 0), ("success", 17)):
            result, _ = self.run_fixture(status, code)
            self.assertNotEqual(result.returncode, 0)

    def test_missing_unknown_and_empty_result_schemas_fail_closed(self):
        for rows in ([{}], [], [{"test": {"plugin-library": {"test": "fixture", "path": "fixture.clap"}}}]):
            with self.subTest(rows=rows):
                result, _ = self.run_fixture("success", rows=rows)
                self.assertNotEqual(result.returncode, 0)
        result, _ = self.run_fixture("unknown-status")
        self.assertNotEqual(result.returncode, 0)

    def test_wrong_version_is_rejected(self):
        result, report = self.run_fixture("success", version="9.9.9")
        self.assertNotEqual(result.returncode, 0)
        self.assertIsNone(report)


if __name__ == "__main__":
    unittest.main()
