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
                "_IOLockLock", "_IOLockUnlock"}
OWN_PREFIXES = ("__ZN7cezanne", "__ZN10CezanneGPU", "__ZN20CezanneGPUUserClient")


def strip_comments(source):
    return re.sub(r"//[^\n]*|/\*.*?\*/", "", source, flags=re.S)


def hardware_calls(source):
    """Forbidden names used in code, unguarded register mappings and register-pointer stores."""
    code = strip_comments(source)
    found = [name for name in FORBIDDEN if re.search(r"\b%s" % name, code)]
    maps = re.findall(r"(?:mapDeviceMemoryWithRegister|->map)\s*\(([^;]*)\)\s*;", code)
    found += ["writable mapping" for args in maps if "kIOMapReadOnly" not in args]
    # Physical ranges are created only for the discovery binary's fixed size.
    ranges = re.findall(r"\bwithRange\s*\(([^;]*)\)\s*;", code)
    found += ["unbounded range" for args in ranges if not args.strip().endswith("cezanne::kDiscoveryTmrSize")]
    if re.search(r"\bvolatile\b", code) and re.search(r"(?<!const )volatile", code):
        found.append("non-const volatile")
    if re.search(r"->base\s*\[[^\]]*\]\s*=[^=]", code):
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
        self.assertIn("readAllowedRegister(registers, aperture.length, stage_, offset, value)", source)
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
        self.assertRegex(header, r"const uint32_t kMaxStage = 4;")
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
