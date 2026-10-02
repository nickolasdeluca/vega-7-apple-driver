"""Build and exercise the public Metal inventory boundary on macOS."""
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


@unittest.skipUnless(sys.platform == "darwin" and shutil.which("xcrun"), "macOS SDK required")
class MetalInventoryTests(unittest.TestCase):
    def test_inventory_builds_and_returns_json_even_when_discovery_is_denied(self):
        source = Path(__file__).resolve().parents[1] / "tools" / "metal_inventory.m"
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "metal-inventory"
            build = subprocess.run(["xcrun", "clang", "-fobjc-arc", "-Wall", "-Wextra", "-Werror",
                "-framework", "Foundation", "-framework", "Metal", str(source), "-o", str(binary)],
                capture_output=True, text=True, timeout=60)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertIn(run.returncode, (0, 2), run.stderr)
            result = json.loads(run.stdout)
            self.assertIsInstance(result["devices"], list)
            self.assertIsInstance(result["loaded_graphics_images"], list)
            self.assertNotIn(binary.name, [Path(image).name for image in result["loaded_graphics_images"]])
            if result["devices"]:
                self.assertEqual(run.returncode, 0)
                for device in result["devices"]:
                    self.assertIsInstance(device["registry_id"], int)
                    self.assertTrue(device["name"])
            else:
                self.assertEqual(run.returncode, 2)
                self.assertEqual(result["status"], "unavailable")
                self.assertTrue(result["reason"])
