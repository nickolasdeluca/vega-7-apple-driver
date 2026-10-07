"""Build cezanne-diag and check it without the driver (the diagnostic interface is stage 4 only)."""
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DIAG = ROOT / "tools" / "diag"


class DiagSourceTests(unittest.TestCase):
    def test_tool_calls_only_known_selectors(self):
        source = (DIAG / "cezanne_diag.cpp").read_text()
        calls = re.findall(r"IOConnect\w+\s*\(\s*connection\s*,\s*(\w+)", source)
        self.assertEqual(sorted(set(calls)),
                         ["kDiagnosticDisplayVerify", "kDiagnosticGartVerify", "kDiagnosticGetInfo", "kDiagnosticIntrVerify", "kDiagnosticMetricsRead",
                          "kDiagnosticReadRegister", "kDiagnosticSdmaInventory", "selector"])
        helper = re.findall(r"\bcall\(connection, (\w+)", source)
        self.assertEqual(helper, ["kDiagnosticScratchCheck", "kDiagnosticScratchWrite", "kDiagnosticScratchRestore", "kDiagnosticSmuCheck", "kDiagnosticSmuQuery", "kDiagnosticSmuCheck", "kDiagnosticGfxOffDisallow", "kDiagnosticMetricsCheck", "kDiagnosticMetricsTransfer", "kDiagnosticPspRingCheck", "kDiagnosticPspRingCreate", "kDiagnosticPspRingObserve", "kDiagnosticPspRingDestroy", "kDiagnosticPspRingDestroy", "kDiagnosticPspRingCheck", "kDiagnosticPspRingCreate", "kDiagnosticPspTmrSubmit", "kDiagnosticPspTmrObserve", "kDiagnosticPspTmrTeardown", "kDiagnosticPspTmrTeardown", "kDiagnosticSdmaStop", "kDiagnosticIntrCheck", "kDiagnosticIntrEnable", "kDiagnosticSdmaSubmit", "kDiagnosticIntrAck", "kDiagnosticIntrRestore", "kDiagnosticGartCheck", "kDiagnosticGartEnable", "kDiagnosticSdmaSubmit", "kDiagnosticGartRestore", "kDiagnosticDisplayCheck", "kDiagnosticDisplayFlip", "kDiagnosticDisplayRestore", "kDiagnosticSdmaCopyCheck", "kDiagnosticSdmaStart", "kDiagnosticSdmaSubmit", "kDiagnosticSdmaVerify", "kDiagnosticPspRingCheck", "kDiagnosticPspRingCreate", "kDiagnosticPspTmrSubmit", "kDiagnosticSdmaLoad", "kDiagnosticSdmaObserve"])

        # The only messages the tool can ask for are the two version queries.
        self.assertEqual(re.findall(r'\{"smu \d/3 query: \w+ \(0x\d\)", (\w+)\}', source),
                         ["kSmuMsgGetDriverIfVersion", "kSmuMsgGetSmuVersion"])
        self.assertNotRegex(source, r"IOConnect(MapMemory|SetNotificationPort|CallAsync|CallStructMethod)")

    def test_scratch_test_runs_only_on_request(self):
        source = (DIAG / "cezanne_diag.cpp").read_text()
        self.assertEqual(len(re.findall(r"\bsmuQuery\(connection\)", source)), 1)
        self.assertRegex(source, r"if \(smu\) \{[^}]*info\[1\] < kSmuStage")
        self.assertIn('std::strcmp(argv[i], "--smu-query") == 0', source)
        self.assertEqual(len(re.findall(r"\bgfxOffDisallow\(connection\)", source)), 1)
        self.assertRegex(source, r"if \(gfxoff\) \{[^}]*info\[1\] < kGfxOffStage")
        self.assertEqual(len(re.findall(r"\bsmuMetrics\(connection\)", source)), 1)
        self.assertRegex(source, r"if \(metrics\) \{[^}]*info\[1\] < kMetricsStage")
        self.assertEqual(len(re.findall(r"\bpspRing\(connection\)", source)), 1)
        self.assertRegex(source, r"if \(ring\) \{[^}]*info\[1\] < kPspRingStage")
        self.assertEqual(len(re.findall(r"\bpspTmr\(connection\)", source)), 1)
        self.assertRegex(source, r"if \(tmr\) \{[^}]*info\[1\] < kPspTmrStage")
        self.assertEqual(re.findall(r"\bpspSdma\(connection, (\w+)\)", source), ["0", "1", "2", "3", "4"])
        self.assertRegex(source, r"if \(gartFlag\) \{[^}]*info\[1\] < kGartStage")
        self.assertRegex(source, r"if \(intrFlag\) \{[^}]*info\[1\] < kIntrStage")
        self.assertRegex(source, r"if \(displayFlag\) \{[^}]*info\[1\] < kDisplayStage")
        self.assertEqual(len(re.findall(r"\bdisplayPattern\(connection\)", source)), 1)
        # The display restore runs whenever the flip was sent.
        display = re.search(r"bool displayPattern\(.*?\n}\n", source, re.S).group(0)
        self.assertLess(display.index("kDiagnosticDisplayFlip"), display.index("kDiagnosticDisplayRestore"))
        self.assertNotRegex(display[display.index("kDiagnosticDisplayFlip"):display.index("kDiagnosticDisplayRestore")],
                            r"\breturn\b")
        # The interrupt steps run only after a passing stage 17 verify, inside
        # its restore, and their own restore whenever the enable was sent.
        self.assertIn("if (ok && intr) ok = intrIh(connection);", source)
        gart = re.search(r"bool gartIh\(.*?\n}\n", source, re.S).group(0)
        self.assertLess(gart.index("intrIh(connection)"), gart.index("kDiagnosticGartRestore"))
        intr = re.search(r"bool intrIh\(.*?\n}\n", source, re.S).group(0)
        self.assertLess(intr.index("kDiagnosticIntrEnable"), intr.index("kDiagnosticIntrRestore"))
        self.assertNotRegex(intr[intr.index("kDiagnosticIntrEnable"):intr.index("kDiagnosticIntrRestore")], r"\breturn\b")
        self.assertLess(intr.index("kDiagnosticIntrVerify"), intr.index("kDiagnosticIntrAck"))
        # The GART steps run only after a passing stage 15 verify, and the
        # restore whenever the enable was sent.
        self.assertIn("if (ok && gart) ok = gartIh(connection, intr);", source)
        gart = re.search(r"bool gartIh\(.*?\n}\n", source, re.S).group(0)
        self.assertLess(gart.index("kDiagnosticGartEnable"), gart.index("kDiagnosticGartRestore"))
        self.assertNotRegex(gart[gart.index("kDiagnosticGartEnable"):gart.index("kDiagnosticGartRestore")], r"\breturn\b")
        self.assertRegex(source, r"if \(copy\) \{[^}]*info\[1\] < kSdmaCopyStage")
        self.assertRegex(source, r"if \(inventory16Flag\) \{[^}]*info\[1\] < kInventory16Stage")
        self.assertRegex(source, r"if \(inventory\) \{[^}]*info\[1\] < kSdmaInventoryStage")
        self.assertRegex(source, r"if \(sdma\) \{[^}]*info\[1\] < kPspSdmaStage")
        self.assertEqual(len(re.findall(r"\bpspState\(connection\)", source)), 1)
        self.assertRegex(source, r"if \(psp\) \{[^}]*info\[1\] < kPspStateStage")
        self.assertEqual(len(re.findall(r"\bscratchTest\(connection\)", source)), 1)
        self.assertRegex(source, r"if \(scratch\) \{[^}]*info\[1\] < kScratchStage")
        self.assertIn('std::strcmp(argv[i], "--scratch-test") == 0', source)

    def test_tool_names_every_stage_17_register_in_order(self):
        source = (DIAG / "cezanne_diag.cpp").read_text()
        header = (ROOT / "driver" / "core" / "cezanne_core.h").read_text()
        expected = []
        for name in ("kStage16Registers", "kStage17Registers", "kStage19Registers"):
            listed = re.search(r"%s\[\] = \{([^}]*)\}" % name, header).group(1)
            expected += [entry.strip() for entry in listed.split(",")]
        self.assertEqual(re.findall(r'\{"\w+", (kReg\w+)\}', source), expected)


@unittest.skipUnless(sys.platform == "darwin" and shutil.which("xcrun"), "macOS SDK required")
class DiagBuildTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.out = Path(cls.tmp.name) / "diag"
        cls.build = subprocess.run([str(DIAG / "build.sh"), str(cls.out)], capture_output=True, text=True,
                                   timeout=180)
        cls.tool = cls.out / "cezanne-diag"

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        self.assertEqual(self.build.returncode, 0, self.build.stderr)

    def run_tool(self, *args):
        return subprocess.run([str(self.tool), *args], capture_output=True, text=True, timeout=30)

    def test_help_and_argument_errors(self):
        self.assertEqual(self.run_tool("--help").returncode, 0)
        for args in (["--repeat", "0"], ["--repeat", "x"], ["--interval"], ["--bogus"]):
            with self.subTest(args):
                self.assertEqual(self.run_tool(*args).returncode, 2)

    def test_reports_a_missing_driver(self):
        present = subprocess.run(["ioreg", "-r", "-c", "CezanneGPU"], capture_output=True, text=True, timeout=30)
        if present.stdout.strip():
            self.skipTest("CezanneGPU is loaded; this check is for the known-good boot")
        run = self.run_tool()
        self.assertEqual(run.returncode, 1)
        self.assertIn("no CezanneGPU service", run.stderr)


if __name__ == "__main__":
    unittest.main()
