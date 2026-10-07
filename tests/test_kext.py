"""Build CezanneGPU.kext and check it stays within the authorized read-only stages.

Builds only; never loads it.
"""
import os
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
KEXT = ROOT / "driver" / "kext"
CORE = ROOT / "driver" / "core"
# Hardware-facing calls no authorized stage makes: configuration or register
# writes, decode/bus-master changes, DMA, direct interrupt registration and
# power management. Stage 18's one MSI handler goes through an
# IOFilterInterruptEventSource, confined by test_interrupt_path_is_confined.
# Virtual calls are indirect in the binary, so this source check is the guard.
FORBIDDEN = ("configWrite", "extendedConfigWrite", "ioWrite", "memoryWrite", "setMemoryEnable",
             "setIOEnable", "setBusMaster", "setBusLead", "IOBufferMemoryDescriptor", "IODMACommand",
             "IOMemoryDescriptor", "getDeviceMemory", "mapDeviceMemoryWithIndex", "registerInterrupt",
             "enableInterrupt", "IOInterruptEventSource::interruptEventSource", "IOTimerEventSource",
             "PMinit", "joinPMtree", "registerPowerDriver",
             "setPowerState", "enablePCIPowerManagement", "IOMapper", "ioRead")
# Direct call targets in the built binary: metaclass plumbing, logging, boot
# argument, configuration-space reads, the mapping's address, the discovery
# table's physical range (stage 2), the stage 4 user client's privilege check
# and lock, and the core.
DIRECT_CALLS = {"___stack_chk_fail", "__ZN11OSMetaClassC2EPKcPKS_j", "__ZN11OSMetaClassD2Ev",
                "__ZN15OSMetaClassBase12safeMetaCastEPKS_PK11OSMetaClass",
                "__ZN8OSObjectdlEPvm", "__ZN8OSObjectnwEm",
                "__ZN9IOServiceC2EPK11OSMetaClass", "__ZN9IOServiceD2Ev",
                "__ZNK11OSMetaClass19instanceConstructedEv", "_IOLog", "_PE_parse_boot_argn", "_snprintf",
                "__ZN11IOPCIDevice19extendedConfigRead8Ey", "__ZN11IOPCIDevice20extendedConfigRead16Ey",
                "__ZN11IOPCIDevice20extendedConfigRead32Ey", "__ZN11IOMemoryMap18getPhysicalAddressEv",
                "__ZN14IODeviceMemory9withRangeEyy", "__ZN12IOUserClient18clientHasPrivilegeEPvPKc",
                "__ZN12IOUserClientC2EPK11OSMetaClass", "__ZN12IOUserClientD2Ev", "_IOLockAlloc", "_IOLockFree",
                "_IOLockLock", "_IOLockUnlock", "_IOSleep",
                # Zeroing the driver's own 148-byte metrics buffer (stage 9).
                "___bzero",
                # Stage 18: the MSI handler on the driver's own work loop, its
                # counter and its timing.
                "__ZN10IOWorkLoop8workLoopEv",
                "__ZN28IOFilterInterruptEventSource26filterInterruptEventSourceEP8OSObjectPFvS1_"
                "P22IOInterruptEventSourceiEPFbS1_PS_EP9IOServicei",
                "_OSAddAtomic", "_OSIncrementAtomic", "_mach_absolute_time", "_absolutetime_to_nanoseconds"}
OWN_PREFIXES = ("__ZN7cezanne", "__ZN10CezanneGPU", "__ZN20CezanneGPUUserClient")


def strip_comments(source):
    return re.sub(r"//[^\n]*|/\*.*?\*/", "", source, flags=re.S)


# The register write path (stage 6 on), the work-area write path (stage 12),
# the firmware-buffer write path (stage 13), the copy work area (stage 15) and
# the GART work area (stage 17), and the only writable constructs allowed: one
# writable map each, one store each (registerWrite; workWrite; firmwareWrite;
# sdmaWorkWrite; gartWorkWrite), and the non-const volatile pointers that
# carry them.
SCRATCH_MAP = "pageMemory->map(kIOMapInhibitCache)"
SCRATCH_STORE = "page->base[i][(offset - page->pageOffset[i]) / 4] = value;"
WORK_MAP = "workMemory->map(kIOMapInhibitCache)"
WORK_STORE = "work->base[offset / 4] = value;"
FIRMWARE_MAP = "firmwareMemory->map(kIOMapInhibitCache)"
FIRMWARE_STORE = "firmware->base[offset / 4] = value;"
SDMA_WORK_MAP = "sdmaWorkMemory->map(kIOMapInhibitCache)"
SDMA_WORK_STORE = "sdmaWork->base[offset / 4] = value;"
GART_WORK_MAP = "gartWorkMemory->map(kIOMapInhibitCache)"
GART_WORK_STORE = "gartWork->base[offset / 4] = value;"
PATTERN_MAP = "patternMemory->map(kIOMapInhibitCache)"
PATTERN_STORE = "pattern->base[offset / 4] = value;"
ALLOWED_MAPS = (SCRATCH_MAP, WORK_MAP, FIRMWARE_MAP, SDMA_WORK_MAP, GART_WORK_MAP, PATTERN_MAP)
ALLOWED_STORES = (SCRATCH_STORE, WORK_STORE, FIRMWARE_STORE, SDMA_WORK_STORE, GART_WORK_STORE, PATTERN_STORE)
ALLOWED_RANGE_SIZES = ("cezanne::kDiscoveryTmrSize", "cezanne::kPageSize", "cezanne::kPspWorkSize",
                       "cezanne::kSdmaFwBufferSize", "cezanne::kGartWorkSize", "cezanne::kPatternSize", "length")
ALLOWED_VOLATILE = 12
SDMA_FIRMWARE = ROOT / "out" / "firmware-provenance" / "fw" / "green_sardine_sdma.bin"


def hardware_calls(source):
    """Forbidden names used in code, and writable constructs beyond the scratch test's."""
    code = strip_comments(source)
    found = [name for name in FORBIDDEN if re.search(r"\b%s" % name, code)]
    maps = re.findall(r"((?:mapDeviceMemoryWithRegister|\w+->map)\s*\(([^;)]*)\))", code)
    writable = [call for call, args in maps if "kIOMapReadOnly" not in args]
    found += ["writable mapping" for call in writable if call.split("=")[-1].strip() not in ALLOWED_MAPS]
    for allowed in ALLOWED_MAPS:
        if len([call for call in writable if call.strip().endswith(allowed)]) > 1:
            found.append("writable mapping")
    # Physical ranges: the discovery binary and the scratch page, at fixed sizes.
    ranges = re.findall(r"\bwithRange\s*\(([^;]*)\)\s*;", code)
    found += ["unbounded range" for args in ranges if not args.strip().endswith(ALLOWED_RANGE_SIZES)]
    if len(re.findall(r"(?<!const )\bvolatile\b", code)) > ALLOWED_VOLATILE:
        found.append("non-const volatile")
    stores = re.findall(r"\w+->base\s*(?:\[[^\]]*\])+\s*=[^=][^;]*;", code)
    if [store for store in stores if store not in ALLOWED_STORES] or len(stores) > len(ALLOWED_STORES):
        found.append("register store")
    return found


def user_client_gate(source):
    """The newUserClient body, which must refuse non-root callers and early stages."""
    match = re.search(r"IOReturn CezanneGPU::newUserClient\(.*?\n}\n", strip_comments(source), re.S)
    return match.group(0) if match else ""


class KextSourceTests(unittest.TestCase):
    def test_diagnostic_interface_is_root_only_and_stage_gated(self):
        source = (KEXT / "CezanneGPU.cpp").read_text()
        gate = user_client_gate(source)
        self.assertIn("clientHasPrivilege(securityID, kIOClientPrivilegeAdministrator)", gate)
        self.assertIn("stage_ < cezanne::kDiagnosticStage", gate)
        self.assertIn("!diagnosticsReady_", gate)
        # The privilege and stage checks come before a client is created.
        self.assertLess(gate.index("clientHasPrivilege"), gate.index("OSTypeAlloc"))
        self.assertLess(gate.index("kDiagnosticStage"), gate.index("OSTypeAlloc"))
        # Reads go through the allowlist with the driver's own stage.
        self.assertIn("readDiagnosticRegister(registers, length, stage, read->offset, read->value)", source)
        self.assertIn("status = operation(stage_, registers, aperture.length, &writer, argument);", source)
        self.assertNotIn("IOConnectMapMemory", source)
        self.assertNotIn("clientMemoryForType", source)


    def test_source_makes_no_forbidden_calls(self):
        self.assertEqual(hardware_calls((KEXT / "CezanneGPU.cpp").read_text()), [])

    def test_check_rejects_planted_calls_and_ignores_comments(self):
        source = (KEXT / "CezanneGPU.cpp").read_text()
        planted = source + """
void f(IOPCIDevice *p, Aperture *a) {
    p->configWrite32(4, 6);
    p->mapDeviceMemoryWithRegister(0x24, kIOMapInhibitCache);
    IODeviceMemory *m = IODeviceMemory::withRange(0x1000, 0x80000000);
    m->map(kIOMapInhibitCache);
    volatile UInt32 *w = nullptr;
    a->base[0] = 1;
    p->base[0][1] = 2;
} // setBusMaster
"""
        self.assertEqual(hardware_calls(planted),
                         ["configWrite", "writable mapping", "writable mapping", "unbounded range",
                          "non-const volatile", "register store"])

    def test_scratch_write_path_is_confined(self):
        source = strip_comments((KEXT / "CezanneGPU.cpp").read_text())
        self.assertEqual(source.count(SCRATCH_MAP), 1)
        self.assertEqual(source.count(SCRATCH_STORE), 1)
        self.assertEqual(source.count(WORK_MAP), 1)
        self.assertEqual(source.count(WORK_STORE), 1)
        self.assertEqual(source.count(FIRMWARE_MAP), 1)
        self.assertEqual(source.count(FIRMWARE_STORE), 1)
        self.assertEqual(source.count(SDMA_WORK_MAP), 1)
        self.assertEqual(source.count(SDMA_WORK_STORE), 1)
        self.assertEqual(source.count(GART_WORK_MAP), 1)
        self.assertEqual(source.count(GART_WORK_STORE), 1)
        self.assertEqual(source.count(PATTERN_MAP), 1)
        self.assertEqual(source.count(PATTERN_STORE), 1)
        pattern = re.search(r"static bool patternWrite\(.*?\n}\n", source, re.S).group(0)
        self.assertLess(pattern.index("patternWriteAllowed(offset, value, pattern->stage)"), pattern.index(PATTERN_STORE))
        self.assertIn("IODeviceMemory::withRange(cezanne::kPatternPhysical, cezanne::kPatternSize)", source)
        self.assertIn("if (stage < cezanne::kDisplayStage) {", re.search(r"static cezanne::Status withPattern\(.*?\n}\n",
                                                                         source, re.S).group(0))
        sdma = re.search(r"static bool sdmaWorkWrite\(.*?\n}\n", source, re.S).group(0)
        self.assertLess(sdma.index("sdmaWorkWriteAllowed(offset, value, sdmaWork->stage)"), sdma.index(SDMA_WORK_STORE))
        # The copy work area: four pages, five from stage 17 (the second destination).
        self.assertIn("const UInt32 length = stage >= cezanne::kGartStage ? cezanne::kGartSdmaWorkSize : "
                      "cezanne::kSdmaWorkSize;\n    IODeviceMemory *sdmaWorkMemory = "
                      "IODeviceMemory::withRange(cezanne::kSdmaWorkPhysical, length);", source)
        gart = re.search(r"static bool gartWorkWrite\(.*?\n}\n", source, re.S).group(0)
        self.assertLess(gart.index("gartWorkWriteAllowed(offset, value, gartWork->stage)"), gart.index(GART_WORK_STORE))
        self.assertIn("IODeviceMemory::withRange(cezanne::kGartWorkPhysical, cezanne::kGartWorkSize)", source)
        self.assertIn("if (stage < cezanne::kGartStage) {", re.search(r"static cezanne::Status withGartWork\(.*?\n}\n",
                                                                      source, re.S).group(0))
        # The GART register page set: only these operations, only these pages;
        # only it may read the invalidation semaphore (the flush).
        self.assertEqual(sorted(re.findall(r"accessDevice\(cezanne::kGartPageSet, (\w+)", source)),
                         ["gartEnableOperation", "gartRestoreOperation"])
        # The interrupt page set: only these operations, only these pages.
        self.assertEqual(sorted(re.findall(r"accessDevice\(cezanne::kIntrPageSet, (\w+)", source)),
                         ["intrAckOperation", "intrArmOperation", "intrQuiesceOperation", "intrRestoreOperation",
                          "intrStartOperation"])
        self.assertIn("const uint32_t kIntrPages[] = {0x3000, 0x4000};", (CORE / "cezanne_core.h").read_text())
        self.assertIn("const uint32_t kGartPages[] = {0x4000, 0x69000, 0x6a000};", (CORE / "cezanne_core.h").read_text())
        self.assertIn("Aperture aperture = {nullptr, 0, stage_, writablePage == cezanne::kGartPageSet};", source)
        self.assertIn("stage_, false};", source)
        read = re.search(r"static bool registerRead\(.*?\n}\n", source, re.S).group(0)
        self.assertIn("(aperture->semaphore && cezanne::semaphoreReadAllowed(offset, aperture->stage))", read)
        # The SDMA register page set: only these operations, only these pages.
        self.assertEqual(sorted(re.findall(r"accessDevice\(cezanne::kSdmaPageSet, (\w+)", source)),
                         ["copyStartOperation", "copyStopOperation", "copySubmitOperation"])
        header = (CORE / "cezanne_core.h").read_text()
        self.assertIn("const uint32_t kSdmaPages[] = {0x4000, 0x5000, 0x58000};", header)
        firmware = re.search(r"static bool firmwareWrite\(.*?\n}\n", source, re.S).group(0)
        self.assertLess(firmware.index("sdmaFirmwareWriteAllowed(kCezanneSdmaImage, offset, value, firmware->stage)"),
                        firmware.index(FIRMWARE_STORE))
        self.assertIn("IODeviceMemory::withRange(cezanne::kSdmaFwPhysical, cezanne::kSdmaFwBufferSize)", source)
        # The work area: only its fixed range, written only after the core's
        # allowlist, from stage 12.
        work = re.search(r"static bool workWrite\(.*?\n}\n", source, re.S).group(0)
        self.assertLess(work.index("pspWorkWriteAllowed(offset, value, work->stage)"), work.index(WORK_STORE))
        self.assertIn("IODeviceMemory::withRange(cezanne::kPspRingPhysical, cezanne::kPspWorkSize)", source)
        self.assertIn("if (stage < cezanne::kPspTmrStage) {", re.search(r"static cezanne::Status withPspWork\(.*?\n}\n",
                                                                        source, re.S).group(0))
        write = re.search(r"static bool registerWrite\(.*?\n}\n", source, re.S).group(0)
        self.assertIn(SCRATCH_STORE, write)
        self.assertLess(write.index("writeAllowed(offset, value, page->stage)"), write.index(SCRATCH_STORE))
        # Writable pages: the scratch page for its write and restore steps, the
        # SMU mailbox page for a query; every other operation maps none.
        writable = sorted(re.findall(r"accessDevice\((cezanne::k\w+PageOffset), (\w+)", source))
        self.assertEqual(writable, [("cezanne::kDisplayPageOffset", "displayFlipOperation"),
                                    ("cezanne::kDisplayPageOffset", "displayRestoreOperation"),
                                    ("cezanne::kScratchPageOffset", "scratchRestoreOperation"),
                                    ("cezanne::kScratchPageOffset", "scratchRestoreOperation"),
                                    ("cezanne::kScratchPageOffset", "scratchWriteOperation"),
                                    ("cezanne::kSmuPageOffset", "gfxOffOperation"),
                                    ("cezanne::kSmuPageOffset", "metricsTransferOperation"),
                                    ("cezanne::kSmuPageOffset", "pspCreateOperation"),
                                    ("cezanne::kSmuPageOffset", "pspDestroyOperation"),
                                    ("cezanne::kSmuPageOffset", "sdmaInventoryOperation"),
                                    ("cezanne::kSmuPageOffset", "sdmaLoadOperation"),
                                    ("cezanne::kSmuPageOffset", "smuQueryOperation"),
                                    ("cezanne::kSmuPageOffset", "tmrSubmitOperation"),
                                    ("cezanne::kSmuPageOffset", "tmrTeardownOperation")])
        self.assertEqual(sorted(re.findall(r"accessDevice\(0, (\w+)", source)),
                         ["copyCheckOperation", "copyVerifyOperation", "displayCheckOperation",
                          "displayVerifyOperation", "gartCheckOperation", "gartVerifyOperation",
                          "intrCheckOperation", "intrVerifyOperation", "metricsCheckOperation", "metricsReadOperation", "pspCheckOperation", "pspObserveOperation",
                          "readOperation", "scratchCheckOperation", "sdmaObserveOperation", "smuCheckOperation",
                          "tmrObserveOperation"])
        # Carveout memory: only the metrics page's and the PSP ring page's
        # ranges, read-only, at their check sizes or one page.
        self.assertEqual(re.findall(r"IODeviceMemory::withRange\((\w+), length\)", source), ["physical"])
        self.assertIn("if (physical != cezanne::kMetricsPhysical && physical != cezanne::kPspRingPhysical &&\n"
                      "        physical != cezanne::kPspTmrPhysical && physical != cezanne::kSdmaFwPhysical &&\n"
                      "        physical != cezanne::kSdmaWorkPhysical && physical != cezanne::kGartWorkPhysical &&\n"
                      "        physical != cezanne::kPatternPhysical) {", source)
        self.assertEqual(sorted(re.findall(r"withCarveoutMemory\(cezanne::(\w+), cezanne::(\w+),", source)),
                         [("kGartWorkPhysical", "kGartWorkCheckSize"), ("kGartWorkPhysical", "kGartWorkCheckSize"),
                          ("kGartWorkPhysical", "kGartWorkCheckSize"), ("kGartWorkPhysical", "kGartWorkCheckSize"),
                          ("kMetricsPhysical", "kMetricsCheckSize"), ("kMetricsPhysical", "kPageSize"),
                          ("kPatternPhysical", "kPatternSize"), ("kPatternPhysical", "kPatternSize"),
                          ("kPspRingPhysical", "kPspRingCheckSize"), ("kPspRingPhysical", "kPspRingCheckSize"),
                          ("kPspRingPhysical", "kPspRingCheckSize"), ("kPspRingPhysical", "kPspRingCheckSize"),
                          ("kPspTmrPhysical", "kPspTmrSize"), ("kSdmaFwPhysical", "kSdmaFwCheckSize"),
                          ("kSdmaFwPhysical", "kSdmaFwCheckSize"), ("kSdmaWorkPhysical", "kSdmaWorkCheckSize"),
                          ("kSdmaWorkPhysical", "kSdmaWorkCheckSize"), ("kSdmaWorkPhysical", "kSdmaWorkCheckSize"),
                          ("kSdmaWorkPhysical", "kSdmaWorkCheckSize"), ("kSdmaWorkPhysical", "kSdmaWorkCheckSize")])
        # A PSP ring created by a connection is destroyed if it closes early.
        abandon = re.search(r"void CezanneGPU::scratchAbandon\(.*?\n}\n", source, re.S).group(0)
        self.assertIn("pspDestroyLocked(&response, &mailbox)", abandon)
        # The stage 15 stop (also on abandon) first restores every stage 17
        # register the enable may have changed.
        stop = re.search(r"cezanne::Status CezanneGPU::sdmaStopLocked\(.*?\n}\n", source, re.S).group(0)
        self.assertLess(stop.index("gartRestoreLocked(&index, &value, &ihWptr)"), stop.index("copyStopOperation"))
        self.assertIn("if (gartState_ >= kGartEnabled && gartState_ != kGartRestored) {", stop)
        self.assertIn("memory->map(kIOMapReadOnly | kIOMapInhibitCache)", source)
        self.assertIn("writablePage != 0 && writablePage != cezanne::kScratchPageOffset && "
                      "writablePage != cezanne::kSmuPageOffset &&\n        writablePage != cezanne::kSdmaPageSet && "
                      "writablePage != cezanne::kGartPageSet &&\n        writablePage != cezanne::kIntrPageSet && "
                      "writablePage != cezanne::kDisplayPageOffset) {", source)
        page = re.search(r"IODeviceMemory::withRange\(\(state\.bar5 & ~0xFull\) \+ page\.pageOffset\[i\], "
                         r"cezanne::kPageSize\)", source)
        self.assertIsNotNone(page)
        # An SMU query needs a passing check by the same connection.
        self.assertEqual(source.count("if (smuChecked_ && smuOwner_ == owner) {"), 2)  # query and GFXOFF
        # A connection closed mid-test restores the register.
        self.assertRegex(source, r"clientClose\(\)\s*\{\s*gpu_->scratchAbandon\(this\);")

    def test_interrupt_path_is_confined(self):
        source = strip_comments((KEXT / "CezanneGPU.cpp").read_text())
        # One registration: the MSI index intrCheck found, on the driver's
        # own work loop, made only by addIntrSourceLocked.
        self.assertEqual(source.count("filterInterruptEventSource("), 1)
        self.assertEqual(source.count("IOWorkLoop::workLoop()"), 1)
        add = re.search(r"cezanne::Status CezanneGPU::addIntrSourceLocked\(.*?\n}\n", source, re.S).group(0)
        self.assertIn("filterInterruptEventSource(this, intrAction, intrFilter, pci,", add)
        self.assertIn("msiIndex_);", add)
        check = re.search(r"cezanne::Status CezanneGPU::intrCheck\(.*?\n}\n", source, re.S).group(0)
        self.assertIn("(type & kIOInterruptTypePCIMessaged) != 0", check)
        # The enable arms the IH, then registers the handler, and only then
        # sets ENABLE_INTR; it is the only caller.
        self.assertEqual(source.count("status = addIntrSourceLocked();"), 1)
        self.assertEqual(source.count("addIntrSourceLocked()"), 3)  # declaration, definition, call
        enable = re.search(r"cezanne::Status CezanneGPU::intrEnable\(.*?\n}\n", source, re.S).group(0)
        self.assertLess(enable.index("intrArmOperation"), enable.index("addIntrSourceLocked()"))
        self.assertLess(enable.index("addIntrSourceLocked()"), enable.index("intrStartOperation"))
        # The filter only counts and times: no lock, log, device or memory access.
        filt = re.search(r"bool CezanneGPU::intrFilter\(.*?\n}\n", source, re.S).group(0)
        for name in ("IOLock", "IOLog", "accessDevice", "read32", "write32", "->base", "setProperty", "IOSleep"):
            self.assertNotIn(name, filt)
        self.assertIn("return false;", filt)
        # The restore quiets the IH, removes the handler, then restores
        # INTERRUPT_CNTL2; the GART restore (and so the stop) runs it first;
        # stop() removes a handler that is somehow still registered.
        restore = re.search(r"cezanne::Status CezanneGPU::intrRestoreLocked\(.*?\n}\n", source, re.S).group(0)
        self.assertLess(restore.index("intrQuiesceOperation"), restore.index("removeIntrSourceLocked()"))
        self.assertLess(restore.index("removeIntrSourceLocked()"), restore.index("intrRestoreOperation"))
        gart = re.search(r"cezanne::Status CezanneGPU::gartRestoreLocked\(.*?\n}\n", source, re.S).group(0)
        self.assertLess(gart.index("intrRestoreLocked(index, value, msi)"), gart.index("gartRestoreOperation"))
        self.assertIn("if (intrProgress_ != 0 || intrSource_ != nullptr) {", gart)
        stop = re.search(r"void CezanneGPU::stop\(.*?\n}\n", source, re.S).group(0)
        self.assertLess(stop.index("removeIntrSourceLocked()"), stop.index("IOService::stop(provider)"))
        # Calls: a failed registration, the restore and stop().
        self.assertEqual(len(re.findall(r"^\s+removeIntrSourceLocked\(\);", source, re.M)), 3)

    def test_display_flip_is_restored_on_every_path(self):
        source = strip_comments((KEXT / "CezanneGPU.cpp").read_text())
        # The surface counts as flipped before the flip's operation runs, and
        # the restore runs from its selector, an abandoned connection and stop().
        flip = re.search(r"cezanne::Status CezanneGPU::displayFlip\(.*?\n}\n", source, re.S).group(0)
        self.assertLess(flip.index("displayState_ = kDisplayFlipped;"), flip.index("displayFlipOperation"))
        self.assertEqual(source.count("displayRestoreLocked(&inuse, &pauses, &index, &value)"), 2)  # abandon, stop
        abandon = re.search(r"void CezanneGPU::scratchAbandon\(.*?\n}\n", source, re.S).group(0)
        self.assertIn("displayRestoreLocked(&inuse, &pauses, &index, &value)", abandon)
        stop = re.search(r"void CezanneGPU::stop\(.*?\n}\n", source, re.S).group(0)
        self.assertLess(stop.index("displayRestoreLocked"), stop.index("IOService::stop(provider)"))
        # The restore flips back to the GOP surface; the flip goes only to the pattern.
        self.assertIn("cezanne::kGopSurfaceAddress,", re.search(r"static cezanne::Status displayRestoreOperation\(.*?\n}\n",
                                                               source, re.S).group(0))
        self.assertIn("cezanne::kPatternGpuAddress,", re.search(r"static cezanne::Status displayFlipOperation\(.*?\n}\n",
                                                               source, re.S).group(0))

    def test_stage_interlock_and_identity_are_declared(self):
        info = plistlib.loads((KEXT / "Info.plist").read_bytes())
        personality = info["IOKitPersonalities"]["CezanneGPU"]
        self.assertEqual(personality["IOPCIMatch"], "0x16381002")
        self.assertEqual(personality["IOProviderClass"], "IOPCIDevice")
        self.assertEqual(personality["CFBundleIdentifier"], info["CFBundleIdentifier"])
        self.assertNotIn("OSBundleRequired", info)
        self.assertIn("KMOD_EXPLICIT_DECL(%s," % info["CFBundleIdentifier"], (KEXT / "kmod_info.c").read_text())
        source = (KEXT / "CezanneGPU.cpp").read_text()
        self.assertIn('PE_parse_boot_argn("cezanne-stage"', source)
        self.assertIn("stage > cezanne::kMaxStage", source)
        header = (CORE / "cezanne_core.h").read_text()
        self.assertRegex(header, r"const uint32_t kMaxStage = 19;")
        self.assertRegex(header, r"kStage1Registers\[\] = \{kRegC2PMsg33, kRegConfigMemsize\}")
        self.assertRegex(header, r"kStage2Registers\[\] = \{kRegC2PMsg33, kRegConfigMemsize, kRegMcVmFbOffset\}")
        self.assertRegex(header, r"kDiscoveryTmrSize = 10 << 10;")
        self.assertRegex(header, r"kStage3Registers\[\] = \{kRegC2PMsg33, kRegConfigMemsize, kRegMcVmFbOffset, kRegGrbmStatus,")


class FirmwareTrackingTests(unittest.TestCase):
    def test_no_firmware_is_tracked(self):
        tracked = subprocess.run(["git", "ls-files"], cwd=ROOT, capture_output=True, text=True, timeout=30)
        self.assertEqual(tracked.returncode, 0, tracked.stderr)
        names = tracked.stdout.split()
        self.assertEqual([n for n in names if n.endswith(".bin") or "sdma_image" in n], [])
        build = (KEXT / "build.sh").read_text()
        self.assertIn("cba8658ea950a99115ca46ee88c9622240632a0ca1731abbcd18b6d9be9e09de", build)
        self.assertIn('> "$out/obj/sdma_image.cpp"', build)


@unittest.skipUnless(sys.platform == "darwin" and shutil.which("xcrun"), "macOS SDK required")
@unittest.skipUnless(SDMA_FIRMWARE.is_file(), "pinned SDMA firmware not in out/ (docs/firmware-provenance.md)")
class KextBuildTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        out = Path(cls.tmp.name).resolve() / "build"
        cls.build = subprocess.run([str(KEXT / "build.sh"), str(out)], capture_output=True,
                                   text=True, timeout=180)
        cls.kext = out / "CezanneGPU.kext"
        cls.binary = cls.kext / "Contents" / "MacOS" / "CezanneGPU"

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        self.assertEqual(self.build.returncode, 0, self.build.stderr)

    def test_build_refuses_existing_output(self):
        again = subprocess.run([str(KEXT / "build.sh"), str(self.kext.parent)], capture_output=True,
                               text=True, timeout=30)
        self.assertEqual(again.returncode, 1)
        self.assertIn("refusing to overwrite", again.stderr)

    def test_build_refuses_firmware_with_a_wrong_hash(self):
        with tempfile.TemporaryDirectory() as tmp:
            wrong = Path(tmp) / "green_sardine_sdma.bin"
            data = bytearray(SDMA_FIRMWARE.read_bytes())
            data[-1] ^= 1
            wrong.write_bytes(bytes(data))
            env = dict(os.environ, CEZANNE_SDMA_FW=str(wrong))
            run = subprocess.run([str(KEXT / "build.sh"), str(Path(tmp) / "build")], capture_output=True, text=True,
                                 timeout=180, env=env)
            self.assertEqual(run.returncode, 1)
            self.assertIn("SHA-256 mismatch", run.stderr)

    def test_binary_is_x86_64_kext_with_matching_executable(self):
        info = plistlib.loads((self.kext / "Contents" / "Info.plist").read_bytes())
        self.assertEqual(info["CFBundleExecutable"], self.binary.name)
        kind = subprocess.run(["file", "-b", str(self.binary)], capture_output=True, text=True, timeout=30)
        self.assertEqual(kind.stdout.strip(), "Mach-O 64-bit kext bundle x86_64")
        self.assertIn(info["CFBundleIdentifier"].encode(), self.binary.read_bytes())

    def test_direct_calls_are_limited_to_the_read_only_set(self):
        listing = subprocess.run(["otool", "-tV", str(self.binary)], capture_output=True, text=True,
                                 timeout=30)
        self.assertEqual(listing.returncode, 0, listing.stderr)
        targets = set(re.findall(r"\bcallq?\s+(_\S+)", listing.stdout))
        targets |= set(re.findall(r"\bjmpq?\s+(_\S+)", listing.stdout))
        self.assertTrue(targets)
        foreign = {t for t in targets if not t.startswith(OWN_PREFIXES)}
        self.assertEqual(foreign - DIRECT_CALLS, set())

    @unittest.skipUnless(shutil.which("kmutil"), "kmutil required")
    def test_every_undefined_symbol_resolves_in_a_declared_library(self):
        # kmutil omits unresolved symbols instead of failing, so compare against nm.
        undefined = subprocess.run(["nm", "-u", str(self.binary)], capture_output=True, text=True,
                                   timeout=30).stdout.split()
        resolved = subprocess.run(["kmutil", "libraries", "-p", str(self.kext)], capture_output=True,
                                  text=True, timeout=180)
        self.assertEqual(resolved.returncode, 0, resolved.stderr)
        rows = dict(re.findall(r"^\t\t(\S+) in .*: (\S+) \(", resolved.stdout, re.M))
        self.assertTrue(undefined)
        self.assertEqual(sorted(set(undefined) - set(rows)), [])
        declared = plistlib.loads((self.kext / "Contents" / "Info.plist").read_bytes())["OSBundleLibraries"]
        self.assertEqual(sorted(set(rows.values()) - set(declared)), [])


if __name__ == "__main__":
    unittest.main()
