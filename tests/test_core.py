"""Build and run the hardware-core unit tests on the host (fakes only, no hardware)."""
import re
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
            "writeAllowed(kRegScratchReg0 + 4, 0, 6)": ("&& offset == kRegScratchReg0) return true;",
                                                        "&& offset >= kRegScratchReg0) return true;"),
            "writeAllowed(kRegMp1C2PMsg66, message, 7)": ("return value == kSmuMsgGetSmuVersion || value == kSmuMsgGetDriverIfVersion ||",
                                                          "return value < 0x40 ||"),
            "!writeAllowed(kRegMp1C2PMsg66, 0x7, 8)": ("(stage >= kGfxOffStage && value == kSmuMsgDisableGfxOff)",
                                                       "(stage >= kGfxOffStage && value >= 0x7 && value <= 0x8)"),
            "!smuArgumentAllowed(kSmuMsgSetDriverDramAddrLow, 0xF4, 9)": (
                "case kSmuMsgSetDriverDramAddrLow: return stage >= kMetricsStage && argument == uint32_t(kMetricsGpuAddress);",
                "case kSmuMsgSetDriverDramAddrLow: return stage >= kMetricsStage;"),
            "kTableRegionInUse": ("if (value != snapshot[offset / 4]) return kTableRegionInUse;", ""),
            "kTableOverflow": ("if (value != snapshot[offset / 4]) return kTableOverflow;", ""),
            "changing.pauses == 3": ("for (uint32_t i = 0; i < pauses; i++) writer.pause(writer.context);",
                                     "if (pauses > 0) writer.pause(writer.context);"),
            "kGfxOffTimeout": ("if (i == kGfxOffConfirmPauses) return kGfxOffTimeout;",
                               "if (i == kGfxOffConfirmPauses) return kOK;"),
            "misc == 0x4": ("        if (((*gfxMisc & kGfxOffStatusMask) >> kGfxOffStatusShift) == kGfxOffStatusOn) return kOK;\n        if (i == kGfxOffConfirmPauses)",
                            "        return kOK;\n        if (i == kGfxOffConfirmPauses)"),
            "writeAllowed(kRegMp1C2PMsg90, 1, 7)": ("return value == 0;", "return true;"),
            "writeAllowed(offset, 0, 7)": ("    if (stage < kPspRingStage) return false;\n    if (offset ==",
                                           "    if (stage < kPspRingStage) return offset >= kSmuPageOffset;\n    if (offset =="),
            "kSmuBusy": ("return mailbox->response == 0 ? kSmuBusy : kOK;", "return kOK;"),
            "kSmuTimeout": ("if (i == kSmuPollPauses) return kSmuTimeout;", "if (i == kSmuPollPauses) break;"),
            "kSmuResponseNotOk": ("if (*response != kSmuResponseOk) return kSmuResponseNotOk;", ""),
            "kCpNotHalted": ("return kCpNotHalted;", "(void)0;"),
            "kRlcEnabled": ("if (check->rlcCntl != 0) return kRlcEnabled;", ""),
            "kScratchUnstable": ("return check->original2 == check->original ? kOK : kScratchUnstable;",
                                 "return kOK;"),
            "kScratchRestoreMismatch": ("return *readback == original ? kOK : kScratchRestoreMismatch;",
                                        "return kOK;"),
            "registerAllowed(kRegMp1C2PMsg90, 4)": (": stage >= 3 ? kStage3RegisterCount", ": stage >= 3 ? kStage5RegisterCount"),
            "!registerAllowed(kRegMp0C2PMsg64, 9)": ("stage >= 10  ? kStage10RegisterCount",
                                                     "stage >= 9  ? kStage10RegisterCount"),
            "!pspCommandAllowed(kPspCmdInitGpcomRing, 3, 0x0015244bu, kPspRingSize, 11)": (
                "    case kPspCmdInitGpcomRing:\n        return low ==", "    case kPspCmdInitGpcomRing:\n        return (void)low, true;\n        return low =="),
            "11, true, &response) == kPspNotReady": ("if ((ready & kPspResponseFlag) == 0) return kPspNotReady;", ""),
            "kPspTimeout": ("if (i == kPspPollPauses) return kPspTimeout;", "if (i == kPspPollPauses) break;"),
            "kPspOutOfOrder": ("if (!created) return kPspOutOfOrder;", "(void)created;"),
            "kPspRingExists": ("return kPspRingExists;", "(void)0;"),
            "11, &response, &written) == kOK": (
                "return (*response & kPspResponseMask) == kPspResponseFlag ? kOK : kPspResponseNotOk;",
                "return *response == kPspResponseFlag ? kOK : kPspResponseNotOk;"),
            "kPspTimeout && written": ("    *written = true;\n    for", "    for"),
            "gfxGated(kRegGcApertureHigh)": ("if (kStage10GfxGatedRegisters[i] == offset) return true;", "(void)0;"),
            "registerAllowed(kRegGrbmGfxIndex, 2)": ("stage == 2 ? kStage2RegisterCount",
                                                     "stage == 2 ? kStage3RegisterCount"),
        }
        for expected, mutation in mutants.items():
            with self.subTest(expected):
                run = self.compile_and_run(mutation)
                self.assertNotEqual(run.returncode, 0)
                self.assertIn(expected, run.stderr)

    def test_core_has_one_write_site_and_no_runtime_dependencies(self):
        for name in ("cezanne_core.h", "cezanne_core.cpp"):
            source = (CORE / name).read_text()
            self.assertNotRegex(source, r"#include\s*<(?!stdint\.h>)", name)
            self.assertNotIn("volatile", source, name)
        source = (CORE / "cezanne_core.cpp").read_text()
        # The only call of the write callback, inside writeRegister, after the allowlist.
        self.assertEqual(len(re.findall(r"\.write32\s*\(", source)), 1)
        body = re.search(r"static Status writeRegister\(.*?\n}\n", source, re.S).group(0)
        self.assertIn("if (!writeAllowed(offset, value, stage)) return kRegisterNotAllowed;", body)
        self.assertLess(body.index("writeAllowed"), body.index("write32"))
        # Scratch pattern and restore; SMU response, argument and message; PSP
        # arguments (C2PMSG_69, _70, _71) and command.
        self.assertEqual(len(re.findall(r"\bwriteRegister\s*\(writer", source)), 9)
        allow = re.search(r"bool writeAllowed\(.*?\n}\n", source, re.S).group(0)
        self.assertEqual(allow.count("return"), 11)
        self.assertIn("if (stage >= kScratchStage && offset == kRegScratchReg0) return true;", allow)
        self.assertIn("if (offset == kRegMp1C2PMsg90) return value == 0;", allow)
        # Every SMU message is checked against its argument before any write.
        send = re.search(r"static Status sendSmuMessage\(.*?\n}\n", source, re.S).group(0)
        self.assertLess(send.index("smuArgumentAllowed(message, argument, stage)"), send.index("writeRegister"))
        self.assertIn("(stage >= kGfxOffStage && value == kSmuMsgDisableGfxOff)", allow)
        # Every PSP command is checked against its arguments before any write.
        psp = re.search(r"static Status sendPspCommand\(.*?\n}\n", source, re.S).group(0)
        self.assertLess(psp.index("pspCommandAllowed(command, low, high, size, stage)"), psp.index("writeRegister"))
        self.assertIn("if (stage < kPspRingStage) return false;", allow)


if __name__ == "__main__":
    unittest.main()
