"""Test AU discovery ordering and failure propagation without macOS services."""

import os
from pathlib import Path
import plistlib
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class MacAudioUnitTests(unittest.TestCase):
    def validate(self, *, missing=False, validator_status=0, au_type="aumf"):
        with tempfile.TemporaryDirectory(prefix="AU discovery ") as temporary:
            temporary = Path(temporary)
            component = temporary / "RoboDrummer.component"
            (component / "Contents").mkdir(parents=True)
            with (component / "Contents/Info.plist").open("wb") as output:
                plistlib.dump({"AudioComponents": [{"type": au_type, "subtype": "RDrm", "manufacturer": "CDrf"}]}, output)
            bindir = temporary / "bin"
            bindir.mkdir()
            commands = {
                "ditto": 'printf "copy\\n" >> "$AU_TEST_LOG"\n',
                "codesign": 'printf "sign\\n" >> "$AU_TEST_LOG"\n',
                "killall": 'printf "reset %s\\n" "$*" >> "$AU_TEST_LOG"\ntouch "$AU_TEST_RESET"\n',
                "sleep": ':\n',
                "auval": '''printf "auval %s\\n" "$*" >> "$AU_TEST_LOG"
if [ "$1" = -a ]; then
  count=0; [ ! -f "$AU_TEST_COUNT" ] || count=$(cat "$AU_TEST_COUNT")
  count=$((count+1)); printf "%s" "$count" > "$AU_TEST_COUNT"
  if [ -f "$AU_TEST_RESET" ] && [ "$count" -ge 2 ] && [ "$AU_TEST_MISSING" = 0 ]; then
    printf "aumf RDrm CDrf - Circuit Drift Labs: RoboDrummer\\n"
  else
    printf "aufx othr Othr - Another component\\n"
  fi
else
  [ -f "$AU_TEST_RESET" ] || exit 21
  [ -f "$AU_TEST_COUNT" ] && [ "$(cat "$AU_TEST_COUNT")" -ge 2 ] || exit 22
  exit "$AU_TEST_VALIDATOR_STATUS"
fi
''',
            }
            for name, body in commands.items():
                executable = bindir / name
                executable.write_text("#!/bin/bash\nset -eu\n" + body)
                executable.chmod(0o755)
            env = dict(os.environ, PATH=f"{bindir}:{os.environ['PATH']}",
                       AU_TEST_LOG=str(temporary / "calls"), AU_TEST_RESET=str(temporary / "reset"),
                       AU_TEST_COUNT=str(temporary / "count"), AU_TEST_MISSING="1" if missing else "0",
                       AU_TEST_VALIDATOR_STATUS=str(validator_status))
            result = subprocess.run(
                ["bash", str(ROOT / "scripts/ci/macos_au_validation.sh"), str(component),
                 str(temporary / "qa"), str(temporary / "components")],
                env=env, text=True, capture_output=True, check=False,
            )
            log = temporary / "calls"
            return result, log.read_text() if log.exists() else ""

    def test_refresh_and_discovery_precede_validation(self):
        result, log = self.validate()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("reset -9 AudioComponentRegistrar", log)
        self.assertEqual(log.count("auval -a\n"), 2)
        self.assertTrue(log.endswith("auval -v aumf RDrm CDrf\n"), log)

    def test_missing_component_blocks_validation_after_bounded_discovery(self):
        result, log = self.validate(missing=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(log.count("auval -a\n"), 10)
        self.assertNotIn("auval -v", log)
        self.assertIn("not discovered", result.stdout + result.stderr)

    def test_discovery_does_not_hide_validator_failure(self):
        result, log = self.validate(validator_status=17)
        self.assertEqual(result.returncode, 17, result.stdout + result.stderr)
        self.assertIn("auval -v aumf RDrm CDrf", log)

    def test_wrong_component_identity_is_rejected_before_installation(self):
        result, log = self.validate(au_type="aufx")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(log, "")
        self.assertIn("identity", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
