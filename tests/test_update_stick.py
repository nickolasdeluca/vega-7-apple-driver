"""Check tools/update_stick.sh without touching a disk: its refusals and its guards."""
import re
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "update_stick.sh"


class UpdateStickTests(unittest.TestCase):
    def run_script(self, *args):
        return subprocess.run([str(SCRIPT), *args], capture_output=True, text=True, timeout=30)

    def test_refuses_bad_arguments_and_missing_builds(self):
        for args, code in (([], 2), (["1x"], 2), (["-1"], 2), (["1", "2"], 2), (["999"], 1)):
            with self.subTest(args):
                run = self.run_script(*args)
                self.assertEqual(run.returncode, code, run.stderr)
                self.assertNotIn("->", run.stdout)

    def test_removes_only_the_stick_efi_after_every_check(self):
        source = SCRIPT.read_text()
        self.assertIn("volume=/Volumes/CZTEST\n", source)
        self.assertEqual(re.findall(r"rm -rf [^\n]*", source), ['rm -rf "$volume/EFI"'])
        removal = source.index("rm -rf")
        for guard in ('[ "$(field MountPoint)" = "$volume" ]', '[ "$(field VolumeName)" = CZTEST ]',
                      '[ "$(field Internal)" = false ]', '[ "$(field RemovableMediaOrExternalDevice)" = true ]',
                      '[ -f "$build/manifest.json" ]'):
            self.assertLess(source.index(guard), removal, guard)
        # The copy is verified against the same build's manifest.
        self.assertIn('verify --manifest "$build/manifest.json" --side test "$volume/EFI"', source)
        self.assertIn("set -eu", source)


if __name__ == "__main__":
    unittest.main()
