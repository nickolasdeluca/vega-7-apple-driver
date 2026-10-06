"""Build CezanneGPU.kext and check it stays within the authorized read-only stages.

Builds only; never loads it.
"""
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
# writes, decode/bus-master changes, DMA, interrupts and power management.
# Virtual calls are indirect in the binary, so this source check is the guard.
FORBIDDEN = ("configWrite", "extendedConfigWrite", "ioWrite", "memoryWrite", "setMemoryEnable",
             "setIOEnable", "setBusMaster", "setBusLead", "IOBufferMemoryDescriptor", "IODMACommand",
             "IOMemoryDescriptor", "getDeviceMemory", "mapDeviceMemoryWithIndex", "registerInterrupt",
             "enableInterrupt", "IOInterruptEventSource", "PMinit", "joinPMtree", "registerPowerDriver",
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
                "___bzero"}
OWN_PREFIXES = ("__ZN7cezanne", "__ZN10CezanneGPU", "__ZN20CezanneGPUUserClient")


def strip_comments(source):
    return re.sub(r"//[^\n]*|/\*.*?\*/", "", source, flags=re.S)


# The register write path (stage 6 on) and the work-area write path (stage
# 12), and the only writable constructs allowed: one writable map each (the
# test's BAR5 page; the three work-area pages), one store each (registerWrite;
# workWrite), and the four non-const volatile pointers that carry them.
SCRATCH_MAP = "pageMemory->map(kIOMapInhibitCache)"
SCRATCH_STORE = "page->base[(offset - page->pageOffset) / 4] = value;"
WORK_MAP = "workMemory->map(kIOMapInhibitCache)"
WORK_STORE = "work->base[offset / 4] = value;"
ALLOWED_MAPS = (SCRATCH_MAP, WORK_MAP)
ALLOWED_STORES = (SCRATCH_STORE, WORK_STORE)
ALLOWED_RANGE_SIZES = ("cezanne::kDiscoveryTmrSize", "cezanne::kPageSize", "cezanne::kPspWorkSize", "length")
ALLOWED_VOLATILE = 4


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
    stores = re.findall(r"\w+->base\s*\[[^\]]*\]\s*=[^=][^;]*;", code)
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
        self.assertEqual(writable, [("cezanne::kScratchPageOffset", "scratchRestoreOperation"),
                                    ("cezanne::kScratchPageOffset", "scratchRestoreOperation"),
                                    ("cezanne::kScratchPageOffset", "scratchWriteOperation"),
                                    ("cezanne::kSmuPageOffset", "gfxOffOperation"),
                                    ("cezanne::kSmuPageOffset", "metricsTransferOperation"),
                                    ("cezanne::kSmuPageOffset", "pspCreateOperation"),
                                    ("cezanne::kSmuPageOffset", "pspDestroyOperation"),
                                    ("cezanne::kSmuPageOffset", "smuQueryOperation"),
                                    ("cezanne::kSmuPageOffset", "tmrSubmitOperation"),
                                    ("cezanne::kSmuPageOffset", "tmrTeardownOperation")])
        self.assertEqual(sorted(re.findall(r"accessDevice\(0, (\w+)", source)),
                         ["metricsCheckOperation", "metricsReadOperation", "pspCheckOperation", "pspObserveOperation",
                          "readOperation", "scratchCheckOperation", "smuCheckOperation", "tmrObserveOperation"])
        # Carveout memory: only the metrics page's and the PSP ring page's
        # ranges, read-only, at their check sizes or one page.
        self.assertEqual(re.findall(r"IODeviceMemory::withRange\((\w+), length\)", source), ["physical"])
        self.assertIn("if (physical != cezanne::kMetricsPhysical && physical != cezanne::kPspRingPhysical &&\n"
                      "        physical != cezanne::kPspTmrPhysical) {", source)
        self.assertEqual(sorted(re.findall(r"withCarveoutMemory\(cezanne::(\w+), cezanne::(\w+),", source)),
                         [("kMetricsPhysical", "kMetricsCheckSize"), ("kMetricsPhysical", "kPageSize"),
                          ("kPspRingPhysical", "kPspRingCheckSize"), ("kPspRingPhysical", "kPspRingCheckSize"),
                          ("kPspRingPhysical", "kPspRingCheckSize"), ("kPspTmrPhysical", "kPspTmrSize")])
        # A PSP ring created by a connection is destroyed if it closes early.
        abandon = re.search(r"void CezanneGPU::scratchAbandon\(.*?\n}\n", source, re.S).group(0)
        self.assertIn("pspDestroyLocked(&response, &mailbox)", abandon)
        self.assertIn("memory->map(kIOMapReadOnly | kIOMapInhibitCache)", source)
        self.assertIn("writablePage != 0 && writablePage != cezanne::kScratchPageOffset && "
                      "writablePage != cezanne::kSmuPageOffset", source)
        page = re.search(r"IODeviceMemory::withRange\(\(state\.bar5 & ~0xFull\) \+ writablePage, cezanne::kPageSize\)",
                         source)
        self.assertIsNotNone(page)
        # An SMU query needs a passing check by the same connection.
        self.assertEqual(source.count("if (smuChecked_ && smuOwner_ == owner) {"), 2)  # query and GFXOFF
        # A connection closed mid-test restores the register.
        self.assertRegex(source, r"clientClose\(\)\s*\{\s*gpu_->scratchAbandon\(this\);")

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
        self.assertRegex(header, r"const uint32_t kMaxStage = 12;")
        self.assertRegex(header, r"kStage1Registers\[\] = \{kRegC2PMsg33, kRegConfigMemsize\}")
        self.assertRegex(header, r"kStage2Registers\[\] = \{kRegC2PMsg33, kRegConfigMemsize, kRegMcVmFbOffset\}")
        self.assertRegex(header, r"kDiscoveryTmrSize = 10 << 10;")
        self.assertRegex(header, r"kStage3Registers\[\] = \{kRegC2PMsg33, kRegConfigMemsize, kRegMcVmFbOffset, kRegGrbmStatus,")


@unittest.skipUnless(sys.platform == "darwin" and shutil.which("xcrun"), "macOS SDK required")
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
