"""Test EFI derivation and verification on synthetic OpenCore trees (no real EFI or disks)."""
import importlib.util
import io
import json
import plistlib
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

MODULE_PATH = Path(__file__).resolve().parents[1] / "tools" / "test_efi.py"
GUID = "7C436110-AB2A-4BBB-A880-FE41995C9F82"


def write_plist(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as handle:
        plistlib.dump(value, handle)


def kext(root, name, bundle_id, libraries=None, executable=True):
    write_plist(root / name / "Contents" / "Info.plist",
                {"CFBundleIdentifier": bundle_id, "OSBundleLibraries": libraries or {}})
    if executable:
        (root / name / "Contents" / "MacOS").mkdir(parents=True)
        (root / name / "Contents" / "MacOS" / name.split(".")[0]).write_bytes(b"\xcf\xfa\xed\xfe" + name.encode())


def entry(name):
    return {"Arch": "Any", "BundlePath": name, "Comment": "", "Enabled": True,
            "ExecutablePath": "Contents/MacOS/" + name.split(".")[0], "MaxKernel": "", "MinKernel": "",
            "PlistPath": "Contents/Info.plist"}


def config():
    return {
        "ACPI": {"Add": []},
        "Kernel": {"Add": [entry("Lilu.kext"), entry("NootedRed.kext"), entry("SMCRadeonSensors.kext"),
                           entry("Other.kext")]},
        "Misc": {"Boot": {"LauncherOption": "Disabled"}, "Security": {"AllowSetDefault": True}},
        "NVRAM": {"Add": {GUID: {"boot-args": "-NRedRBPlus", "csr-active-config": b"\0\0\0\0"}},
                  "Delete": {GUID: ["boot-args", "csr-active-config"]}},
        "PlatformInfo": {"Generic": {"SystemSerialNumber": "FIXTURE"}},
    }


class TestEfiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location("test_efi", MODULE_PATH)
        cls.tool = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.tool)

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.known = self.root / "known" / "EFI"
        kexts = self.known / "OC" / "Kexts"
        kext(kexts, "Lilu.kext", "as.vit9696.Lilu")
        kext(kexts, "NootedRed.kext", "org.ChefKiss.NootedRed", {"as.vit9696.Lilu": "1.7.0"})
        kext(kexts, "SMCRadeonSensors.kext", "org.ChefKiss.SMCRadeonSensors")
        kext(kexts, "Other.kext", "example.other", {"com.apple.kpi.iokit": "19.0"})
        (self.known / "BOOT").mkdir(parents=True)
        (self.known / "BOOT" / "BOOTx64.efi").write_bytes(b"boot")
        (self.known / "OC" / "._Lilu.kext").write_bytes(b"appledouble")
        self.write_config(config())
        (self.known / "OC" / "Config.plist.bak-old").write_bytes(b"backup")
        kext(self.root / "probe", "CezanneProbe.kext", "org.cezanne-driver.probe")
        self.probe = self.root / "probe" / "CezanneProbe.kext"
        self.output = self.root / "usb"

    def tearDown(self):
        self.tmp.cleanup()

    def write_config(self, value):
        write_plist(self.known / "OC" / "Config.plist", value)

    def run_tool(self, *argv):
        out = io.StringIO()
        with redirect_stdout(out), redirect_stderr(io.StringIO()):
            code = self.tool.main(list(argv))
        return code, out.getvalue()

    def build(self):
        return self.run_tool("build", "--known-good", str(self.known), "--kext", str(self.probe),
                             "--output", str(self.output))

    def test_build_changes_only_the_intended_values_and_files(self):
        code, out = self.build()
        self.assertEqual(code, 0, out)
        with (self.output / "EFI" / "OC" / "Config.plist").open("rb") as handle:
            test = plistlib.load(handle)
        names = [e["BundlePath"] for e in test["Kernel"]["Add"]]
        self.assertEqual(names, ["Lilu.kext", "Other.kext", "CezanneProbe.kext"])
        self.assertEqual(test["NVRAM"]["Add"][GUID]["boot-args"], self.tool.TEST_BOOT_ARGS)
        self.assertFalse(test["Misc"]["Security"]["AllowSetDefault"])
        self.assertEqual(test["PlatformInfo"], config()["PlatformInfo"])
        self.assertFalse((self.output / "EFI" / "OC" / "Kexts" / "NootedRed.kext").exists())
        self.assertFalse((self.output / "EFI" / "OC" / "Config.plist.bak-old").exists())
        self.assertFalse((self.output / "EFI" / "OC" / "._Lilu.kext").exists())
        manifest = json.loads((self.output / "manifest.json").read_text())
        self.assertEqual(manifest["skipped_config_backups"], ["OC/Config.plist.bak-old"])
        self.assertEqual(manifest["removed_kexts"]["NootedRed.kext"]["bundle_id"], "org.ChefKiss.NootedRed")
        self.assertIsNone(manifest["ocvalidate"])

    def test_build_refuses_existing_output(self):
        self.output.mkdir()
        code, _ = self.build()
        self.assertEqual(code, 1)

    def test_verify_matches_copies_and_reports_changes(self):
        self.assertEqual(self.build()[0], 0)
        manifest = str(self.output / "manifest.json")
        code, out = self.run_tool("verify", "--manifest", manifest, "--side", "known_good", str(self.known))
        self.assertEqual((code, json.loads(out)["match"]), (0, True))
        (self.output / "EFI" / "BOOT" / "BOOTx64.efi").write_bytes(b"tampered")
        (self.output / "EFI" / "extra.txt").write_bytes(b"x")
        code, out = self.run_tool("verify", "--manifest", manifest, "--side", "test", str(self.output / "EFI"))
        report = json.loads(out)
        self.assertEqual(code, 2)
        self.assertEqual((report["changed"], report["extra"]), (["BOOT/BOOTx64.efi"], ["extra.txt"]))

    def assert_rejected(self, fragment):
        code, out = self.build()
        self.assertEqual(code, 2, out)
        self.assertIn(fragment, json.loads(out)["rejected"])
        self.assertFalse(self.output.exists())

    def test_rejects_launcher_option(self):
        value = config()
        value["Misc"]["Boot"]["LauncherOption"] = "Full"
        self.write_config(value)
        self.assert_rejected("LauncherOption must be Disabled")

    def test_rejects_config_without_a_gpu_kext_to_remove(self):
        value = config()
        value["Kernel"]["Add"] = [e for e in value["Kernel"]["Add"] if e["BundlePath"] != "NootedRed.kext"]
        self.write_config(value)
        self.assert_rejected("lacks ['NootedRed.kext']")

    def test_rejects_kept_kext_that_depends_on_a_removed_one(self):
        kext(self.known / "OC" / "Kexts", "Needy.kext", "example.needy", {"org.ChefKiss.NootedRed": "0.9"})
        value = config()
        value["Kernel"]["Add"].append(entry("Needy.kext"))
        self.write_config(value)
        self.assert_rejected("Needy.kext needs org.ChefKiss.NootedRed")

    def test_rejects_boot_args_the_known_good_boot_would_not_reset(self):
        value = config()
        value["NVRAM"]["Delete"][GUID] = ["csr-active-config"]
        self.write_config(value)
        self.assert_rejected("known-good NVRAM.Delete.%s lacks boot-args" % GUID)

    def test_check_configs_rejects_any_other_change(self):
        known = config()
        test, _ = self.tool.derive(known, self.known / "OC" / "Kexts")
        self.tool.check_configs(known, test)
        test["PlatformInfo"]["Generic"]["SystemSerialNumber"] = "OTHER"
        with self.assertRaisesRegex(self.tool.Rejected, "PlatformInfo.Generic.SystemSerialNumber"):
            self.tool.check_configs(known, test)

    def test_failed_check_keeps_tree_under_rejected_name(self):
        value = config()
        value["Kernel"]["Add"][3]["ExecutablePath"] = "Contents/MacOS/Missing"
        self.write_config(value)
        code, out = self.build()
        self.assertEqual(code, 2)
        self.assertIn("enabled kext file missing", out)
        self.assertFalse(self.output.exists())
        self.assertEqual(len(list(self.root.glob("usb.rejected-*"))), 1)


if __name__ == "__main__":
    unittest.main()
