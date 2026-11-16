"""Cleanup must follow the last job step that can create certificate inputs."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SigningWorkflowTests(unittest.TestCase):
    def test_windows_cleanup_follows_every_signing_consumer(self):
        workflow = (ROOT / ".github/workflows/release-build.yml").read_text()
        windows = workflow.split("\n  windows-x64:\n", 1)[1].split("\n  mac-arm64:\n", 1)[0]
        cleanup = re.search(r"(?m)^      - name: [^\n]*\n        if: always\(\)\n        shell: bash\n        run: python3 -B scripts/ci/signing.py cleanup", windows)
        self.assertIsNotNone(cleanup, "Windows signing requires unconditional final cleanup")
        consumers = [windows.index("run: bash scripts/ci/package.sh"), windows.index("./scripts/ci/windows_installer_smoke.ps1")]
        self.assertGreater(cleanup.start(), max(consumers), "cleanup must follow signed installer/uninstaller smoke too")


if __name__ == "__main__":
    unittest.main()
