"""Build and run the hardware-core unit tests on the host (fakes only, no hardware)."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / "driver" / "core"


@unittest.skipUnless(shutil.which("xcrun") or shutil.which("clang++"), "C++ compiler required")
class CoreTests(unittest.TestCase):
    def compile_and_run(self, mutate=None):
        """Build the core (optionally with one source line replaced) and the tests, then run them."""
        compiler = ["xcrun", "clang++"] if shutil.which("xcrun") else ["clang++"]
        with tempfile.TemporaryDirectory() as tmp:
            core = Path(tmp) / "core"
            shutil.copytree(CORE, core)
            if mutate:
                source = (core / "cezanne_core.cpp").read_text()
                self.assertIn(mutate[0], source)
                (core / "cezanne_core.cpp").write_text(source.replace(mutate[0], mutate[1]))
            binary = Path(tmp) / "core_test"
            build = subprocess.run(
                compiler + ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wconversion", "-g",
                            "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                            "-I", str(core), str(core / "cezanne_core.cpp"),
                            str(ROOT / "tests" / "core" / "core_test.cpp"), "-o", str(binary)],
                capture_output=True, text=True, timeout=120)
            self.assertEqual(build.returncode, 0, build.stderr)
            return subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)

    def test_core_unit_tests_pass(self):
        run = self.compile_and_run()
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIn("core tests passed", run.stdout)

    def test_suite_rejects_weakened_checks(self):
        mutants = {
            "kNotInD0": ("return kNotInD0;", "(void)0;"),
            "kMemoryDecodeDisabled": ("return kMemoryDecodeDisabled;", "(void)0;"),
            "kCapabilityListMalformed": ("i == kMaxCapabilities || offset < 0x40", "i == kMaxCapabilities"),
            "kRegisterNotAllowed": ("offset + 4ull > length", "((void)length, false)"),
            "kDeviceNotResponding": ("return kDeviceNotResponding;", "(void)0;"),
            "registerAllowed(kRegMcVmFbOffset, 1)": ("stage == 1 ? kStage1RegisterCount",
                                                     "stage == 1 ? kStage2RegisterCount"),
            "kCarveoutOverlapsDevice": ("return kCarveoutOverlapsDevice;", "(void)0;"),
            "kDiscoveryChecksum": ("if (byteSum(ihdr, tableSize) != le16(binary + 14)) return kDiscoveryChecksum;", ""),
            "kDiscoveryBaseMismatch": ("return kDiscoveryBaseMismatch;", "(void)0;"),
            "kGfxIndexNotSe0Sh0": ("return kGfxIndexNotSe0Sh0;", "(void)0;"),
            "kGfxNotOn": ("!= kGfxOffStatusOn) return kGfxNotOn;", "!= kGfxOffStatusOn) (void)0;"),
            "registerAllowed(kRegMp1C2PMsg90, 4)": (": stage >= 3 ? kStage3RegisterCount", ": stage >= 3 ? kStage5RegisterCount"),
            "registerAllowed(kRegGrbmGfxIndex, 2)": ("stage == 2 ? kStage2RegisterCount",
                                                     "stage == 2 ? kStage3RegisterCount"),
        }
        for expected, mutation in mutants.items():
            with self.subTest(expected):
                run = self.compile_and_run(mutation)
                self.assertNotEqual(run.returncode, 0)
                self.assertIn(expected, run.stderr)

    def test_core_has_no_write_path_or_runtime_dependencies(self):
        for name in ("cezanne_core.h", "cezanne_core.cpp"):
            source = (CORE / name).read_text()
            self.assertNotRegex(source, r"\bwrite\w*\s*\(", name)
            self.assertNotRegex(source, r"#include\s*<(?!stdint\.h>)", name)
            self.assertNotIn("volatile", source, name)


if __name__ == "__main__":
    unittest.main()
