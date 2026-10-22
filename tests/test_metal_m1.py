"""Metal track M1: the observation bundle, the admission probe and the capture script."""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CAPTURE = ROOT / "tools" / "capture_m1.sh"


class CaptureScriptTests(unittest.TestCase):
    def test_refuses_bad_names(self):
        for args in ([], ["boot-39-stage21"], ["boot-39-m1-rung3"], ["boot-39-m1-rung1", "x"], ["Boot-39-m1-rung1"]):
            with self.subTest(args):
                run = subprocess.run([str(CAPTURE), *args], capture_output=True, text=True, timeout=30)
                self.assertEqual(run.returncode, 2, run.stderr)

    def test_installs_only_in_an_m1_boot_and_always_removes(self):
        source = CAPTURE.read_text()
        self.assertIn("target=/Library/GPUBundles/CezanneMTLDriver.bundle\n", source)
        self.assertEqual(re.findall(r"sudo rm -rf [^\n]*", source), ['sudo rm -rf "$target"'])
        install = source.index('sudo ditto "$bundle" "$target"')
        # The boot must be an M1 boot before anything is installed, and the
        # removal trap must be armed before the install.
        self.assertLess(source.index('grep -q "cezanne-metal=1"'), install)
        self.assertLess(source.index("trap remove EXIT"), install)
        self.assertEqual(source.count("sudo ditto"), 1)


@unittest.skipUnless(sys.platform == "darwin" and shutil.which("xcrun"), "macOS SDK required")
class BundleAndProbeTests(unittest.TestCase):
    def test_bundle_constructs_on_mtldevice_and_records_messages(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            build = subprocess.run([str(ROOT / "driver" / "metal" / "build.sh"), str(tmp / "b")],
                                   capture_output=True, text=True, timeout=120)
            self.assertEqual(build.returncode, 0, build.stderr)
            bundle = tmp / "b" / "CezanneMTLDriver.bundle"
            probe = tmp / "metal-admission-probe"
            compile_ = subprocess.run(["xcrun", "clang", "-fobjc-arc", "-Wall", "-Wextra", "-Werror",
                                       "-framework", "Foundation", "-framework", "Metal",
                                       str(ROOT / "tools" / "metal_admission_probe.m"), "-o", str(probe)],
                                      capture_output=True, text=True, timeout=60)
            self.assertEqual(compile_.returncode, 0, compile_.stderr)
            log = tmp / "m1.log"
            run = subprocess.run([str(probe), "--load-bundle", str(bundle)], capture_output=True, text=True,
                                 timeout=120, env=dict(os.environ, CEZANNE_M1_LOG=str(log)))
            self.assertEqual(run.returncode, 0, run.stderr)
            result = json.loads(run.stdout)
            self.assertEqual(result["principal_class"], "CezanneMtlDevice")
            self.assertTrue(result["is_mtldevice_subclass"])
            self.assertTrue(result["constructed"].startswith("CezanneMtlDevice"))
            lines = log.read_text().splitlines()
            self.assertTrue(any(l.startswith("load: CezanneMtlDevice registered on _MTLDevice") for l in lines))
            self.assertIn("initWithAcceleratorPort: 0 (class CezanneMtlDevice, superclass _MTLDevice)", lines)
            # The vendor-neutral front end works on our device; pipeline creation is the vendor's.
            self.assertTrue(result["device"]["library"].startswith("_MTLLibrary"))
            self.assertEqual(result["device"]["pipeline"], "nil")


if __name__ == "__main__":
    unittest.main()
