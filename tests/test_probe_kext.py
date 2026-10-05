"""Build the passive probe kext and check it stays passive. Builds only; never loads it."""
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

PROBE = Path(__file__).resolve().parents[1] / "driver" / "probe"
# Hardware-facing IOPCIDevice/IOService/IOKit calls the passive stage must not make.
# Virtual calls are indirect in the binary, so this source check is the guard for them.
FORBIDDEN = ("configRead", "configWrite", "extendedConfig", "ioRead", "ioWrite", "memoryRead",
             "memoryWrite", "setMemoryEnable", "setIOEnable", "setBusMaster", "setBusLead",
             "getDeviceMemory", "mapDeviceMemory", "IOMemoryDescriptor", "IOBufferMemoryDescriptor",
             "IODMACommand", "registerInterrupt", "enableInterrupt", "IOInterruptEventSource",
             "PMinit", "joinPMtree", "registerPowerDriver", "setPowerState", "findPCICapability",
             "enablePCIPowerManagement", "IOMapper")
# Direct call targets in the built binary: metaclass plumbing, logging, boot-arg read.
DIRECT_CALLS = {"___stack_chk_fail", "__ZN11OSMetaClassC2EPKcPKS_j", "__ZN11OSMetaClassD2Ev",
                "__ZN15OSMetaClassBase12safeMetaCastEPKS_PK11OSMetaClass",
                "__ZN8OSNumber10withNumberEyj", "__ZN8OSObjectdlEPvm", "__ZN8OSObjectnwEm",
                "__ZN9IOServiceC2EPK11OSMetaClass", "__ZN9IOServiceD2Ev",
                "__ZNK11OSMetaClass19instanceConstructedEv", "_IOLog", "_PE_parse_boot_argn",
                "_snprintf"}


def hardware_calls(source):
    """Forbidden names used in code (comments ignored), plus any open() call."""
    code = re.sub(r"//[^\n]*|/\*.*?\*/", "", source, flags=re.S)
    found = [name for name in FORBIDDEN if re.search(r"\b%s" % name, code)]
    return found + (["open"] if re.search(r"(->|\.)\s*open\s*\(", code) else [])


class ProbeSourceTests(unittest.TestCase):
    def test_source_makes_no_hardware_facing_calls(self):
        self.assertEqual(hardware_calls((PROBE / "CezanneProbe.cpp").read_text()), [])

    def test_check_rejects_planted_calls_and_ignores_comments(self):
        source = (PROBE / "CezanneProbe.cpp").read_text()
        planted = source + "\nvoid f(IOPCIDevice *p) { p->configRead32(0); p->open(nullptr); } // setBusMaster\n"
        self.assertEqual(hardware_calls(planted), ["configRead", "open"])

    def test_interlock_and_identity_are_declared(self):
        info = plistlib.loads((PROBE / "Info.plist").read_bytes())
        personality = info["IOKitPersonalities"]["CezanneProbe"]
        self.assertEqual(personality["IOPCIMatch"], "0x16381002")
        self.assertEqual(personality["IOProviderClass"], "IOPCIDevice")
        self.assertEqual(personality["CFBundleIdentifier"], info["CFBundleIdentifier"])
        self.assertNotIn("OSBundleRequired", info)
        self.assertIn("KMOD_EXPLICIT_DECL(%s," % info["CFBundleIdentifier"],
                      (PROBE / "kmod_info.c").read_text())
        self.assertIn('PE_parse_boot_argn("-cezanne-probe"', (PROBE / "CezanneProbe.cpp").read_text())


@unittest.skipUnless(sys.platform == "darwin" and shutil.which("xcrun"), "macOS SDK required")
class ProbeBuildTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        out = Path(cls.tmp.name).resolve() / "build"
        cls.build = subprocess.run([str(PROBE / "build.sh"), str(out)], capture_output=True,
                                   text=True, timeout=120)
        cls.kext = out / "CezanneProbe.kext"
        cls.binary = cls.kext / "Contents" / "MacOS" / "CezanneProbe"

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        self.assertEqual(self.build.returncode, 0, self.build.stderr)

    def test_build_refuses_existing_output(self):
        again = subprocess.run([str(PROBE / "build.sh"), str(self.kext.parent)], capture_output=True,
                               text=True, timeout=30)
        self.assertEqual(again.returncode, 1)
        self.assertIn("refusing to overwrite", again.stderr)

    def test_binary_is_x86_64_kext_with_matching_executable(self):
        info = plistlib.loads((self.kext / "Contents" / "Info.plist").read_bytes())
        self.assertEqual(info["CFBundleExecutable"], self.binary.name)
        kind = subprocess.run(["file", "-b", str(self.binary)], capture_output=True, text=True, timeout=30)
        self.assertEqual(kind.stdout.strip(), "Mach-O 64-bit kext bundle x86_64")
        self.assertIn(info["CFBundleIdentifier"].encode(), self.binary.read_bytes())

    def test_direct_calls_are_limited_to_the_passive_set(self):
        listing = subprocess.run(["otool", "-tV", str(self.binary)], capture_output=True, text=True,
                                 timeout=30)
        self.assertEqual(listing.returncode, 0, listing.stderr)
        targets = set(re.findall(r"\bcallq?\s+(_\S+)", listing.stdout))
        targets |= set(re.findall(r"\bjmpq?\s+(_\S+)", listing.stdout))
        self.assertTrue(targets)
        self.assertEqual(targets - DIRECT_CALLS, set())

    @unittest.skipUnless(shutil.which("kmutil"), "kmutil required")
    def test_every_undefined_symbol_resolves_in_a_declared_library(self):
        # kmutil omits unresolved symbols instead of failing, so compare against nm.
        undefined = subprocess.run(["nm", "-u", str(self.binary)], capture_output=True, text=True,
                                   timeout=30).stdout.split()
        resolved = subprocess.run(["kmutil", "libraries", "-p", str(self.kext)], capture_output=True,
                                  text=True, timeout=120)
        self.assertEqual(resolved.returncode, 0, resolved.stderr)
        rows = dict(re.findall(r"^\t\t(\S+) in .*: (\S+) \(", resolved.stdout, re.M))
        self.assertTrue(undefined)
        self.assertEqual(sorted(set(undefined) - set(rows)), [])
        declared = plistlib.loads((self.kext / "Contents" / "Info.plist").read_bytes())["OSBundleLibraries"]
        self.assertEqual(sorted(set(rows.values()) - set(declared)), [])


if __name__ == "__main__":
    unittest.main()
