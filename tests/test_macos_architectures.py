"""Exercise native binary validation without requiring macOS or Apple tools."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class MacArchitectureTests(unittest.TestCase):
    def validate(self, architecture="arm64", wrong_binary=False):
        with tempfile.TemporaryDirectory(prefix="native validation ") as temporary:
            temporary = Path(temporary)
            bindir = temporary / "bin"
            bindir.mkdir()
            log = temporary / "calls"
            lipo = bindir / "lipo"
            # -verify_arch accepts multiple architectures. A following filename
            # is parsed as another architecture by Apple's tool, not an input.
            lipo.write_text(
                '#!/bin/bash\nset -eu\n'
                '[[ $# == 3 && "$2" == -verify_arch ]] || exit 2\n'
                '[[ "$1" == */Contents/MacOS/RoboDrummer ]] || exit 3\n'
                'printf "%s\\n" "$1" >> "$LIPO_TEST_LOG"\n'
                '[[ "$3" == "$LIPO_TEST_ARCH" ]] || exit 4\n'
                '[[ "$LIPO_TEST_WRONG" != 1 || "$1" != */CLAP/* ]] || exit 5\n'
            )
            lipo.chmod(0o755)
            result = subprocess.run(
                ["bash", str(ROOT / "scripts/ci/macos_architectures.sh"),
                 str(temporary / "build"), architecture],
                env=dict(os.environ, PATH=f"{bindir}:{os.environ['PATH']}",
                         LIPO_TEST_LOG=str(log), LIPO_TEST_ARCH=architecture,
                         LIPO_TEST_WRONG="1" if wrong_binary else "0"),
                text=True, capture_output=True, check=False,
            )
            return result, log.read_text().splitlines() if log.exists() else []

    def test_all_shipped_formats_are_checked_for_each_native_architecture(self):
        for architecture in ("arm64", "x86_64"):
            with self.subTest(architecture=architecture):
                result, checked = self.validate(architecture)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(len(checked), 4)
                for format_name in ("VST3", "CLAP", "AU", "Standalone"):
                    self.assertTrue(any(f"/{format_name}/" in path for path in checked))

    def test_wrong_architecture_blocks_validation(self):
        result, checked = self.validate(wrong_binary=True)
        self.assertEqual(result.returncode, 5, result.stdout + result.stderr)
        self.assertEqual(len(checked), 2)


if __name__ == "__main__":
    unittest.main()
