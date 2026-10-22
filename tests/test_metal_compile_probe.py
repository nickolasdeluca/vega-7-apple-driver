"""Build and run the Metal runtime compile probe on macOS (compiles, never dispatches)."""
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


@unittest.skipUnless(sys.platform == "darwin" and shutil.which("xcrun"), "macOS SDK required")
class MetalCompileProbeTests(unittest.TestCase):
    def test_probe_compiles_and_reports_json(self):
        source = Path(__file__).resolve().parents[1] / "tools" / "metal_compile_probe.m"
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "metal-compile-probe"
            build = subprocess.run(["xcrun", "clang", "-fobjc-arc", "-Wall", "-Wextra", "-Werror",
                "-framework", "Foundation", "-framework", "Metal", str(source), "-o", str(binary)],
                capture_output=True, text=True, timeout=60)
            self.assertEqual(build.returncode, 0, build.stderr)
            archive = Path(tmp) / "archive.bin"
            run = subprocess.run([str(binary), "--archive", str(archive)], capture_output=True, text=True, timeout=120)
            result = json.loads(run.stdout)
            if run.returncode == 2:
                # No Metal device (for example on the USB test EFI): reported, not a crash.
                self.assertIn("error", result)
                return
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertTrue(result["library"]["ok"])
            self.assertTrue(result["pipeline"]["ok"])
            self.assertGreater(result["pipeline"]["thread_execution_width"], 0)
            for key in ("images_added_by_device", "images_added_by_library", "images_added_by_pipeline"):
                self.assertIsInstance(result[key], list)
            self.assertTrue(result["binary_archive"]["serialized"], result["binary_archive"]["error"])
            self.assertGreater(archive.stat().st_size, 0)


if __name__ == "__main__":
    unittest.main()
