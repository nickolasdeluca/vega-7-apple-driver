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
        self.assertEqual(sorted(set(calls)), ["kDiagnosticGetInfo", "kDiagnosticReadRegister", "selector"])
        helper = re.findall(r"\bcall\(connection, (\w+)", source)
        self.assertEqual(helper, ["kDiagnosticScratchCheck", "kDiagnosticScratchWrite", "kDiagnosticScratchRestore"])
        self.assertNotRegex(source, r"IOConnect(MapMemory|SetNotificationPort|CallAsync|CallStructMethod)")

    def test_scratch_test_runs_only_on_request(self):
        source = (DIAG / "cezanne_diag.cpp").read_text()
        self.assertEqual(len(re.findall(r"\bscratchTest\(connection\)", source)), 1)
        self.assertRegex(source, r"if \(scratch\) \{[^}]*info\[1\] < kScratchStage")
        self.assertIn('std::strcmp(argv[i], "--scratch-test") == 0', source)

    def test_tool_names_every_stage_6_register_in_order(self):
        source = (DIAG / "cezanne_diag.cpp").read_text()
        header = (ROOT / "driver" / "core" / "cezanne_core.h").read_text()
        listed = re.search(r"kStage6Registers\[\] = \{([^}]*)\}", header).group(1)
        expected = [name.strip() for name in listed.split(",")]
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
