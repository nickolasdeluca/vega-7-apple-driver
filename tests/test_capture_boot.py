"""Check tools/capture_boot.sh without running cezanne-diag: its refusals and its confinement."""
import re
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "capture_boot.sh"


class CaptureBootTests(unittest.TestCase):
    def run_script(self, *args):
        return subprocess.run([str(SCRIPT), *args], capture_output=True, text=True, timeout=30)

    def test_refuses_bad_names(self):
        for args in ([], ["stage17"], ["boot-24"], ["../boot-24-stage17"], ["boot-24-stage17/x"],
                     ["boot-24-stage17 x"], ["Boot-24-stage17"]):
            with self.subTest(args):
                run = self.run_script(*args)
                self.assertEqual(run.returncode, 2, run.stderr)
                self.assertNotIn("capturing", run.stdout)

    def test_refuses_an_existing_capture(self):
        existing = sorted((ROOT / "out" / "test-efi").glob("boot-*-stage*"))
        if not existing or not (ROOT / "out" / "diag" / "cezanne-diag").exists():
            self.skipTest("no earlier capture or no built cezanne-diag in out/")
        run = self.run_script(existing[0].name)
        self.assertEqual(run.returncode, 1)
        self.assertIn("refusing to overwrite", run.stderr)

    def test_writes_only_inside_the_new_folder(self):
        source = SCRIPT.read_text()
        self.assertIn("set -eu -o pipefail", source)
        self.assertNotRegex(source, r"\brm\b|\bmv\b|\bditto\b|\bdiskutil\b")
        # Every redirection or tee target is inside $dir.
        targets = re.findall(r"(?:>|tee) \"([^\"]+)\"", source)
        self.assertTrue(targets)
        for target in targets:
            self.assertTrue(target.startswith("$dir/"), target)
        self.assertLess(source.index('[ ! -e "$dir" ]'), source.index('mkdir -p "$dir"'))
        # Only cezanne-diag runs with sudo, with the caller's flags.
        self.assertEqual(re.findall(r"^[^#\n]*\bsudo [^\n]*", source, re.M), ['sudo "$diag" "$@" 2>&1 | tee "$dir/diag.txt" || status=$?'])


if __name__ == "__main__":
    unittest.main()
