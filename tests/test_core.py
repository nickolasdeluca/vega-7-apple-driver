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
            "!registerAllowed(kRegMp0C2PMsg64, 9)": (": stage >= 10 ? kStage10RegisterCount",
                                                     ": stage >= 9 ? kStage10RegisterCount"),
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
            "kPspReadbackMismatch": ("return kPspReadbackMismatch;", "(void)0;"),
            "kPspFenceTimeout": ("if (i == kPspFencePollPauses) return kPspFenceTimeout;",
                                 "if (i == kPspFencePollPauses) return kOK;"),
            "!writeAllowed(kRegMp0C2PMsg67, 48, 12)": ("return value == kPspFrameDwords || value == 2 * kPspFrameDwords ||",
                                                       "return value <= 3 * kPspFrameDwords ||"),
            "!pspWorkWriteAllowed(kPspWorkSize, 0, 12)": (
                "if (stage < kPspTmrStage || (offset & 3) != 0 || offset >= kPspWorkSize) return false;",
                "if (stage < kPspTmrStage || (offset & 3) != 0) return false;"),
            "checkSdmaImage(image, kSdmaImageSize) == kSdmaImageInvalid": ("return kSdmaImageInvalid;\n    return kSdmaUcodeOffset", "(void)0;\n    return kSdmaUcodeOffset"),
            "!sdmaFirmwareWriteAllowed(image, kSdmaFwBufferSize, 0, 13)": (
                "if (stage < kPspSdmaStage || (offset & 3) != 0 || offset >= kSdmaFwBufferSize) return false;",
                "if (stage < kPspSdmaStage || (offset & 3) != 0) return false;"),
            "!writeAllowed(kRegMp0C2PMsg67, 64, 13)": ("(stage >= kPspSdmaStage && value == 3 * kPspFrameDwords);",
                                                       "(stage >= kPspSdmaStage && value >= 3 * kPspFrameDwords);"),
            "!smuArgumentAllowed(kSmuMsgPowerUpSdma, 1, 14)": (
                "case kSmuMsgPowerDownSdma: return stage >= kSdmaInventoryStage && argument == 0;",
                "case kSmuMsgPowerDownSdma: return stage >= kSdmaInventoryStage;"),
            "kSdmaUnexpectedState": ("if (i != kSdmaStatusIndex && *value != kSdmaBoot19[i]) {",
                                     "if (i != kSdmaStatusIndex && false) {"),
            "!writeAllowed(kRegSdma0GbAddrConfig, 0x00100012, 15)": (
                "return offset == kRegSdma0GfxRbWptr && (value == kSdmaFrameDwords * 4 || value == 2 * kSdmaFrameDwords * 4);",
                "return (void)value, offset >= kRegSdma0PowerCntl && offset <= kRegSdma0Rlc1RbWptrPollCntl;"),
            "sdmaRingWord(261) == 0x40303000u": ("uint32_t(destination),", "uint32_t(destination + kSdmaWorkCheckSize),"),
            "r.sdma[kRegSdma0F32Cntl] == 1": ("    if (progress >= 2) {\n        for (const SdmaWrite &write : kSdmaStop)",
                                              "    if (progress >= 3) {\n        for (const SdmaWrite &write : kSdmaStop)"),
            "observed == 0xDEADBEEFu": ("    if (status == kOK) status = writeRegister(writer, stage, kRegSdma0GfxRbWptrHi, 0);\n",
                                        ""),
            "gfxGated(kRegGcApertureHigh)": ("if (kStage10GfxGatedRegisters[i] == offset) return true;", "(void)0;"),
            "registerAllowed(kRegGrbmGfxIndex, 2)": ("stage == 2 ? kStage2RegisterCount",
                                                     "stage == 2 ? kStage3RegisterCount"),
            # Stage 17.
            "!registerAllowed(kRegVmInvalidateEng17AddrRangeHi32, 16)": (
                "stage >= kGartStage && i < kStage17RegisterCount", "i < kStage17RegisterCount"),
            "!semaphoreReadAllowed(kRegVmInvalidateEng17Ack, 17)": (
                "return stage >= kGartStage && offset == kRegVmInvalidateEng17Sem;", "return stage >= kGartStage && offset >= kRegVmInvalidateEng17Sem;"),
            "!writeAllowed(g.offset, g.value, 16)": ("if (stage >= kGartStage && gartWriteListed(offset, value))",
                                                     "if (stage >= kSdmaCopyStage && gartWriteListed(offset, value))"),
            "!writeAllowed(kRegIhRbCntl, kIhRbCntlOn | 0x20000, 17)": (
                "(offset == kRegIhRbCntl && value == kIhRbCntlOn)",
                "(offset == kRegIhRbCntl && (value & ~0x20000u) == kIhRbCntlOn)"),
            "!gartWorkWriteAllowed(kGartWorkSize, 0, 17)": (
                "if (stage < kGartStage || (offset & 3) != 0 || offset >= kGartWorkSize) return false;",
                "if (stage < kGartStage || (offset & 3) != 0) return false;"),
            "gartRingWord(512 + i) == frame[i]": ("                              kSdmaOpTrap,\n",
                                                  "                              kSdmaOpNop,\n"),
            "kGartUnexpectedState": ("if ((*value & mask) != (expected & mask)) return kGartUnexpectedState;",
                                     "(void)mask;"),
            "17, &index, &value) == kOK)\n": ("               ? ~kHubpLiveStatus\n", "               ? 0xFFFFFFFFu\n"),
            "kGartSemaphoreTimeout": ("        if ((semaphore & 1) != 0) break;\n", "        break;\n"),
            "kGartAckTimeout": ("if (i == kGartPollPauses) status = kGartAckTimeout;", "if (i == kGartPollPauses) break;"),
            "!g.r.semHeld": ("    Status release = writeRegister(writer, stage, kRegVmInvalidateEng17Sem, 0);",
                             "    Status release = kOK;"),
            "kGartFault": ("if (report->faultStatus != 0) return kGartFault;", ""),
            "kIhNoTrap": ("if (report->sdmaTraps == 0) return kIhNoTrap;", ""),
            "report.gartFirst == kIhRingPage + 0x800": ("ok = wrapped || offset - kIhRingPage < end || value == 0;",
                                                         "ok = ((void)end, (void)wrapped, true);"),
            "report.displayChanged == 1": ("!= (display[i] & displayMask(i)) &&",
                                           "!= (display[i] & displayMask(i) & 0) + (value & displayMask(i)) &&"),
            "kRegMmhubVmContext0Cntl && g.w.values[before + 10]": (
                "        note(writeRegister(writer, stage, context.offset, context.boot22));\n", "        (void)context;\n"),
            "ihWptr == 0x20": ("        if (offset == kRegIhRbWptr) {", "        if (false) {"),
            # Stage 18.
            "!writeAllowed(kRegInterruptCntl2, kInterruptCntl2Dummy, 17)": (
                "if (stage >= kIntrStage && intrWriteListed(offset, value))",
                "if (stage >= kGartStage && intrWriteListed(offset, value))"),
            "!writeAllowed(kRegIhRbRptr, 0x22, 18)": ("value % kIhEntryBytes == 0 && value < kIhRingBytes",
                                                      "value < kIhRingBytes"),
            "!sdmaWorkWriteAllowed(768 * 4, 5, 17)": ("return stage >= kIntrStage && value == intrRingWord(offset / 4);",
                                                      "return value == intrRingWord(offset / 4);"),
            "17, 3, &observed) == kRegisterNotAllowed": ("|| (frame == 3 && stage < kIntrStage)", ""),
            "kIntrUnexpectedState": ("if (*value != 0) return kIntrUnexpectedState;", ""),
            "kIntrNotDelivered": ("if (report->msiCount == 0) return kIntrNotDelivered;", ""),
            "report.msiCount == 2": ("report->fence3 == 3 && report->msiCount == 1 &&", "report->fence3 == 3 &&"),
            "kIntrRefired": ("return *countAfter != *countBefore || *writeback != before ? kIntrRefired : kOK;",
                             "return kOK;"),
            "kIntrNotRestored": ("if (*value != 0) return status != kOK ? status : kIntrNotRestored;", ""),
            "msi.data == 0x4021": ("        data = static_cast<uint8_t>(offset + 12);\n", ""),
            "g.gartWork.words[kIhRingPage / 4] == 0": ("    for (uint32_t offset = kIhRingPage; offset < kGartWorkSize; offset += 4) {\n        Status status = writeGartWorkWord(gart, stage, offset);",
                                                       "    for (uint32_t offset = kGartWorkSize; offset < kGartWorkSize; offset += 4) {\n        Status status = writeGartWorkWord(gart, stage, offset);"),
            "g.w.writes == before + 21 + 2 && g.w.offsets[before] == kRegMmhubVmContext0Cntl": (
                "    if (progress >= 2) {\n        note(writeRegister(writer, stage, kRegIhRbCntl, kIhRbCntlOff));",
                "    if (progress >= 1) {\n        note(writeRegister(writer, stage, kRegIhRbCntl, kIhRbCntlOff));"),
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
        # The only calls of the write callbacks: writeRegister after the
        # register allowlist, writeWork after the work-area allowlist,
        # writeFirmwareWord after the firmware-buffer allowlist, and the SDMA
        # and GART work-area words after theirs.
        self.assertEqual(len(re.findall(r"\.write32\s*\(", source)), 5)
        for helper, check in (("writeSdmaWorkWord", "sdmaWorkWriteAllowed(offset, value, stage)"),
                              ("writeGartWorkWord", "gartWorkWriteAllowed(offset, value, stage)")):
            body = re.search(r"static Status %s\(.*?\n}\n" % helper, source, re.S).group(0)
            self.assertLess(body.index(check), body.index("write32"), helper)
        firmware = re.search(r"static Status writeFirmwareWord\(.*?\n}\n", source, re.S).group(0)
        self.assertLess(firmware.index("sdmaFirmwareWriteAllowed(image, offset, value, stage)"),
                        firmware.index("write32"))
        work = re.search(r"static Status writeWork\(.*?\n}\n", source, re.S).group(0)
        self.assertLess(work.index("pspWorkWriteAllowed(offset, value, stage)"), work.index("write32"))
        body = re.search(r"static Status writeRegister\(.*?\n}\n", source, re.S).group(0)
        self.assertIn("if (!writeAllowed(offset, value, stage)) return kRegisterNotAllowed;", body)
        self.assertLess(body.index("writeAllowed"), body.index("write32"))
        # Scratch pattern and restore; SMU response, argument and message; PSP
        # arguments (C2PMSG_69, _70, _71) and command; the ring write pointer.
        # Stage 17 adds the GART, IH and SDMA0_CNTL writes, the flush's request
        # and release, frame 2's write pointer, and their restores. Stage 18
        # adds the arm (two lists), the interrupt toggle, the acknowledgement,
        # the quiesce and INTERRUPT_CNTL2's restore.
        self.assertEqual(len(re.findall(r"\bwriteRegister\s*\(writer", source)), 32)
        allow = re.search(r"bool writeAllowed\(.*?\n}\n", source, re.S).group(0)
        self.assertEqual(allow.count("return"), 16)
        self.assertIn("if (stage >= kGartStage && gartWriteListed(offset, value)) return true;", allow)
        self.assertIn("if (stage >= kIntrStage && intrWriteListed(offset, value)) return true;", allow)
        # The semaphore is read only by the flush, through the reader directly
        # (never the allowlisted read), after its own stage check.
        self.assertEqual(source.count("kRegVmInvalidateEng17Sem, &"), 1)
        flush = re.search(r"Status flushGart\(.*?\n}\n", source, re.S).group(0)
        self.assertIn("registers.read32(registers.context, kRegVmInvalidateEng17Sem, &semaphore)", flush)
        self.assertLess(flush.index("semaphoreReadAllowed(kRegVmInvalidateEng17Sem, stage)"), flush.index("read32"))
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
