"""Run native Windows gate orchestration with controlled tool failures."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class WindowsNativeValidationTests(unittest.TestCase):
    def run_gates(self, failure=""):
        with tempfile.TemporaryDirectory(prefix="Windows native gates ") as temporary:
            root = Path(temporary)
            scripts = root / "scripts/ci"
            scripts.mkdir(parents=True)
            shutil.copy2(ROOT / "scripts/ci/windows_native_checks.sh", scripts)
            (scripts / "targets.sh").write_text(
                '#!/bin/bash\n[[ "$1" == windows ]] || exit 3\n'
                '[[ "$FAIL_GATE" != target-policy ]] || exit 20\n'
                'echo "RoboDrummer_VST3 RoboDrummer_CLAP RoboDrummer_Standalone"\n'
            )
            (scripts / "juce_checks.sh").write_text(
                '#!/bin/bash\nset -eu\n'
                '[[ "$2" == windows ]] || exit 4\n'
                'echo "native validation" >> "$GATE_LOG"\n'
                'echo "retained processor/compatibility/pluginval report" > "$1/qa/harness.txt"\n'
                '[[ "$FAIL_GATE" != native ]] || exit 25\n'
            )
            bindir = root / "bin"
            bindir.mkdir()
            for name, body in {
                "cmake": 'echo "cmake $*" >> "$GATE_LOG"\n'
                         'if [[ "$*" == *--target* ]]; then '
                         '[[ "$FAIL_GATE" != targets ]] || exit 21; '
                         'else [[ "$FAIL_GATE" != core-build ]] || exit 22; fi\n',
                "ctest": 'echo "ctest $*" >> "$GATE_LOG"\n'
                         'echo "core result"\n[[ "$FAIL_GATE" != core-tests ]] || exit 23\n',
            }.items():
                executable = bindir / name
                executable.write_text("#!/bin/bash\nset -eu\n" + body)
                executable.chmod(0o755)
            log = root / "gates.log"
            build = root / "native build"
            build.mkdir()
            result = subprocess.run(
                ["bash", str(scripts / "windows_native_checks.sh"), str(build)],
                cwd=root, env=dict(os.environ, PATH=f"{bindir}:{os.environ['PATH']}",
                                   GATE_LOG=str(log), FAIL_GATE=failure, CMAKE_BUILD_PARALLEL_LEVEL="3"),
                capture_output=True, text=True, check=False,
            )
            return result, log.read_text() if log.exists() else "", {
                report.name: report.read_text() for report in (build / "qa").glob("*.txt")
            }

    def test_builds_all_formats_harness_and_core_before_native_checks(self):
        result, calls, reports = self.run_gates()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        lines = calls.splitlines()
        self.assertEqual(len(lines), 4)
        self.assertTrue(lines[0].startswith("cmake --build "))
        for target in ("RoboDrummer_VST3", "RoboDrummer_CLAP", "RoboDrummer_Standalone", "RoboDrummerHarness"):
            self.assertIn(target, lines[0])
        self.assertIn("--config Release --parallel 3", lines[0])
        self.assertNotIn("--target", lines[1])
        self.assertIn("plugin_harness|default_output_reference", lines[2])
        self.assertEqual(lines[3], "native validation")
        self.assertEqual(reports["core-tests.txt"], "core result\n")
        self.assertIn("harness.txt", reports)

    def test_each_required_failure_blocks_later_gates(self):
        for failure, code, expected_calls in (
            ("target-policy", 20, 0),
            ("targets", 21, 1), ("core-build", 22, 2),
            ("core-tests", 23, 3), ("native", 25, 4),
        ):
            with self.subTest(failure=failure):
                result, calls, reports = self.run_gates(failure)
                self.assertEqual(result.returncode, code, result.stdout + result.stderr)
                self.assertEqual(len(calls.splitlines()), expected_calls)
                if failure in ("core-tests", "native"):
                    self.assertEqual(reports["core-tests.txt"], "core result\n")


if __name__ == "__main__":
    unittest.main()
