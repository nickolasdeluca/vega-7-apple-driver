"""Failure handling and registry interpretation must not invent hardware facts."""
import importlib.util
import json
import plistlib
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

MODULE_PATH = Path(__file__).resolve().parents[1] / "tools" / "baseline.py"


class BaselineTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        assert MODULE_PATH.exists(), "read-only collector is not implemented"
        spec = importlib.util.spec_from_file_location("baseline", MODULE_PATH)
        cls.baseline = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.baseline)

    def registry_fixture(self):
        return [{"IORegistryEntryName": "Root", "IOObjectClass": "IOService",
                 "IORegistryEntryChildren": [{"IORegistryEntryName": "GFX0@0",
                 "IOObjectClass": "IOPCIDevice", "IORegistryEntryID": 42,
                 "vendor-id": bytes.fromhex("02100000"),
                 "device-id": bytes.fromhex("38160000"),
                 "revision-id": bytes.fromhex("c9000000"),
                 "class-code": bytes.fromhex("00000300"),
                 "IORegistryEntryChildren": [{"IORegistryEntryName": "Framebuffer",
                    "IOObjectClass": "AMDFramebuffer", "VRAM,totalMB": 2048,
                    "CFBundleIdentifier": "com.apple.kext.AMDFramebuffer"}]}]}]

    def test_decodes_little_endian_pci_ids_and_retains_topology(self):
        devices = self.baseline.graphics_registry(self.registry_fixture())
        self.assertEqual(len(devices), 1)
        gpu = devices[0]
        self.assertEqual(gpu["vendor_id"]["value"], "1002")
        self.assertEqual(gpu["device_id"]["value"], "1638")
        self.assertEqual(gpu["revision_id"]["value"], "c9")
        self.assertEqual(gpu["path"], "Root/GFX0@0")
        self.assertEqual(gpu["descendants"][0]["class"], "AMDFramebuffer")
        self.assertEqual(gpu["memory_properties"][0]["value"], 2048)

    def test_missing_revision_is_unavailable_not_target_revision(self):
        tree = self.registry_fixture()
        del tree[0]["IORegistryEntryChildren"][0]["revision-id"]
        revision = self.baseline.graphics_registry(tree)[0]["revision_id"]
        self.assertEqual(revision["status"], "unavailable")
        self.assertIsNone(revision["value"])

    def test_full_ioreg_capture_accepts_single_root_dictionary(self):
        devices = self.baseline.graphics_registry(self.registry_fixture()[0])
        self.assertEqual(devices[0]["device_id"]["value"], "1638")

    def test_accelerator_discovery_metadata_survives_normalization(self):
        tree = self.registry_fixture()
        tree[0]["IORegistryEntryChildren"][0]["IORegistryEntryChildren"][0].update(
            MetalPluginName="ExampleDriver", MetalPluginClassName="ExampleDevice", IOAccelRevision=2)
        node = self.baseline.graphics_registry(tree)[0]["descendants"][0]
        self.assertEqual(node["discovery_properties"], {
            "MetalPluginName": "ExampleDriver", "MetalPluginClassName": "ExampleDevice", "IOAccelRevision": 2})

    def test_profiler_accepts_versioned_metal_support_property(self):
        data = {"SPDisplaysDataType": [{"_name": "GPU", "spdisplays_mtlgpufamilysupport": "spdisplays_metal3"}]}
        self.assertEqual(self.baseline.profiler_graphics(data)[0]["metal_support"]["value"], "spdisplays_metal3")

    def test_profiler_keeps_memory_and_rotated_display_configuration(self):
        data = {"SPDisplaysDataType": [{"_name": "Example GPU", "spdisplays_vram": "2 GB",
             "_spdisplays_ndrvs": [{"_name": "Panel", "_spdisplays_resolution": "1080 x 1920",
                                      "spdisplays_rotation": "90"}]}]}
        gpu = self.baseline.profiler_graphics(data)[0]
        self.assertEqual(gpu["reported_memory"]["value"], "2 GB")
        self.assertEqual(gpu["displays"][0]["properties"]["spdisplays_rotation"], "90")
        self.assertEqual(gpu["displays"][0]["properties"]["_spdisplays_resolution"], "1080 x 1920")

    def test_failed_query_preserves_exit_status_stderr_and_partial_stdout(self):
        with tempfile.TemporaryDirectory() as tmp:
            command = [sys.executable, "-c", "import sys; print('partial'); print('denied', file=sys.stderr); sys.exit(7)"]
            result = self.baseline.capture("failure", command, Path(tmp), timeout=2)
            self.assertEqual(result["status"], "failed")
            self.assertEqual(result["exit_code"], 7)
            self.assertEqual((Path(tmp) / result["stdout_file"]).read_text(), "partial\n")
            self.assertEqual((Path(tmp) / result["stderr_file"]).read_text(), "denied\n")

    def test_timeout_and_missing_command_are_reported(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            timeout = self.baseline.capture("slow", [sys.executable, "-u", "-c",
                   "import time; print('started'); time.sleep(10)"], root, timeout=0.2)
            self.assertEqual(timeout["status"], "timeout")
            self.assertIn("started", (root / timeout["stdout_file"]).read_text())
            missing = self.baseline.capture("missing", [str(root / "absent")], root, timeout=1)
            self.assertEqual(missing["status"], "unavailable")
            self.assertIsNone(missing["exit_code"])
            self.assertTrue(missing["error"])

    def test_malformed_registry_is_a_parse_failure_in_report(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            sources = self.write_sources(root, registry=b"not a plist")
            report = self.baseline.build_report(root, sources)
            self.assertEqual(report["registry"]["status"], "unavailable")
            self.assertTrue(report["registry"]["reason"])
            self.assertEqual(report["os"]["value"]["BuildVersion"], "25E253")

    def test_truncated_xml_is_preserved_as_unavailable_in_report(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            sources = self.write_sources(root, registry=b'<?xml version="1.0"?><plist><dict>')
            report = self.baseline.build_report(root, sources)
            self.assertEqual(report["registry"]["status"], "unavailable")
            self.assertIn("parse failed", report["registry"]["reason"])

    def write_sources(self, root, registry=None):
        payloads = {
            "os": b"ProductName:\tmacOS\nProductVersion:\t26.4.1\nBuildVersion:\t25E253\n",
            "architecture": b"x86_64\n", "cpu": b"AMD Ryzen 5 5600GT\n",
            "registry": registry if registry is not None else plistlib.dumps(self.registry_fixture()),
            "displays": json.dumps({"SPDisplaysDataType": [{"_name": "GPU"}]}).encode(),
            "loaded_components": b"1 0 0 0 com.apple.kext.AMDRadeonX5000 (1.0)\n2 0 0 0 as.vit9696.Lilu (1.7)\n",
        }
        sources = {}
        for name, data in payloads.items():
            (root / (name + ".stdout")).write_bytes(data)
            (root / (name + ".stderr")).write_bytes(b"")
            sources[name] = {"status": "ok", "exit_code": 0, "stdout_file": name + ".stdout",
                             "stderr_file": name + ".stderr", "argv": [name]}
        return sources

    def test_report_marks_denied_cpu_and_empty_display_list_unavailable(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            sources = self.write_sources(root)
            sources["cpu"]["status"] = "failed"
            sources["cpu"]["exit_code"] = 1
            sources["cpu"]["error"] = "Operation not permitted"
            (root / "displays.stdout").write_text('{"SPDisplaysDataType": []}')
            report = self.baseline.build_report(root, sources)
            self.assertEqual(report["cpu"]["status"], "unavailable")
            self.assertIsNone(report["cpu"]["value"])
            self.assertEqual(report["graphics"]["status"], "unavailable")
            self.assertEqual(len(report["loaded_graphics_components"]["value"]), 2)

    def test_cli_refuses_to_overwrite_existing_capture(self):
        with tempfile.TemporaryDirectory() as tmp:
            sentinel = Path(tmp) / "report.json"
            sentinel.write_text("keep me")
            result = subprocess.run([sys.executable, str(MODULE_PATH), "--output", tmp], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(sentinel.read_text(), "keep me")


if __name__ == "__main__":
    unittest.main()
