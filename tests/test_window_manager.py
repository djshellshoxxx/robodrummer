"""GUI checks require a real, ready EWMH window manager, including headless CI."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class WindowManagerTests(unittest.TestCase):
    def run_wrapper(self, existing=False, command_status=0, missing_display=False):
        with tempfile.TemporaryDirectory() as temporary:
            temporary = Path(temporary)
            bindir = temporary / 'bin'
            bindir.mkdir()
            ready = temporary / 'ready'
            launched = temporary / 'launched'
            if existing:
                ready.touch()
            (bindir / 'xprop').write_text('#!/bin/bash\n[[ -f "$WM_TEST_READY" ]] || exit 1\nprintf "_NET_SUPPORTING_WM_CHECK(WINDOW): window id # 0x123\\n"\n')
            (bindir / 'openbox').write_text('#!/bin/bash\nprintf started > "$WM_TEST_LAUNCHED"\ntouch "$WM_TEST_READY"\ntrap "exit 0" TERM\nwhile :; do sleep 0.05; done\n')
            for script in bindir.iterdir():
                script.chmod(0o755)
            environment = dict(os.environ, DISPLAY='' if missing_display else ':123',
                               PATH=f"{bindir}:{os.environ['PATH']}", WM_TEST_READY=str(ready),
                               WM_TEST_LAUNCHED=str(launched))
            result = subprocess.run(['bash', str(ROOT / 'scripts/ci/with_window_manager.sh'),
                                     'bash', '-c', f'exit {command_status}'],
                                    env=environment, capture_output=True, text=True, timeout=15)
            return result, launched.exists()

    def test_existing_ready_manager_is_reused(self):
        result, launched = self.run_wrapper(existing=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse(launched)

    def test_new_manager_becomes_ready_and_command_failures_propagate(self):
        result, launched = self.run_wrapper(command_status=7)
        self.assertEqual(result.returncode, 7, result.stdout + result.stderr)
        self.assertTrue(launched)

    def test_missing_display_fails_instead_of_skipping_gui_checks(self):
        result, launched = self.run_wrapper(missing_display=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(launched)

if __name__ == '__main__':
    unittest.main()
