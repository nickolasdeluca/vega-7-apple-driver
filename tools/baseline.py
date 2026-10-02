#!/usr/bin/env python3
"""Capture OS-reported graphics state without opening devices or writing registers.

Python 3.9+, standard library only. Raw evidence stays in a new output directory.
Exit 0: sources parsed successfully; 2: partial capture; 1: output/setup error.
"""
import argparse
import datetime
import json
import plistlib
import re
import subprocess
from pathlib import Path
from xml.parsers.expat import ExpatError


COMMANDS = {
    "os": ["/usr/bin/sw_vers"],
    "architecture": ["/usr/bin/uname", "-m"],
    "cpu": ["/usr/sbin/sysctl", "-n", "machdep.cpu.brand_string"],
    "registry": ["/usr/sbin/ioreg", "-a", "-l", "-p", "IOService"],
    "displays": ["/usr/sbin/system_profiler", "-json", "SPDisplaysDataType", "-detailLevel", "full"],
    "loaded_components": ["/usr/bin/kmutil", "showloaded"],
}
GRAPHICS_COMPONENT = re.compile(r"amd|radeon|graphics|accelerator|framebuffer|iosurface|metal|lilu|nootedred", re.I)


def fact(value, source, reason="not reported by source"):
    return {"status": "available" if value is not None else "unavailable",
            "value": value, "source": source, **({"reason": reason} if value is None else {})}


def capture(name, argv, output, timeout=60):
    result = {"argv": argv, "status": "ok", "exit_code": None,
              "stdout_file": name + ".stdout", "stderr_file": name + ".stderr"}
    stdout, stderr = b"", b""
    try:
        process = subprocess.run(argv, capture_output=True, timeout=timeout, check=False)
        stdout, stderr = process.stdout, process.stderr
        result["exit_code"] = process.returncode
        if process.returncode:
            result.update(status="failed", error=stderr.decode("utf-8", "replace").strip() or
                          "command exited with status " + str(process.returncode))
    except subprocess.TimeoutExpired as error:
        stdout, stderr = error.stdout or b"", error.stderr or b""
        result.update(status="timeout", error="query exceeded " + str(timeout) + " seconds")
    except OSError as error:
        result.update(status="unavailable", error=str(error))
    (output / result["stdout_file"]).write_bytes(stdout)
    (output / result["stderr_file"]).write_bytes(stderr)
    return result


def pci_number(value):
    if isinstance(value, bytes) and 0 < len(value) <= 4:
        return int.from_bytes(value, "little")
    if isinstance(value, int) and not isinstance(value, bool) and 0 <= value <= 0xffffffff:
        return value
    return None


def json_value(value):
    if isinstance(value, bytes):
        return {"hex": value.hex()}
    if isinstance(value, dict):
        return {key: json_value(item) for key, item in value.items()}
    if isinstance(value, list):
        return [json_value(item) for item in value]
    return value


def graphics_registry(tree):
    devices = []

    def describe(node, path):
        return {"path": path, "name": node.get("IORegistryEntryName"),
                "class": node.get("IOObjectClass", node.get("IOClass")),
                "registry_id": node.get("IORegistryEntryID"),
                "bundle_id": node.get("CFBundleIdentifier"),
                "user_client_class": json_value(node.get("IOUserClientClass")),
                "discovery_properties": {key: json_value(node[key]) for key in (
                    "MetalPluginName", "MetalPluginClassName", "MetalStatisticsName", "IOGLBundleName",
                    "IOAccelRevision", "IOAccelDisplayPipeCapabilities", "IOCFPlugInTypes") if key in node}}

    def descendants(node, path):
        items = []
        for child in node.get("IORegistryEntryChildren", []):
            child_path = path + "/" + child.get("IORegistryEntryName", "?")
            items.append(describe(child, child_path))
            items.extend(descendants(child, child_path))
        return items

    def memory(node, path):
        items = [{"path": path, "property": key, "value": json_value(value)}
                 for key, value in node.items() if re.search(r"vram|videomemory", key, re.I)]
        for child in node.get("IORegistryEntryChildren", []):
            items.extend(memory(child, path + "/" + child.get("IORegistryEntryName", "?")))
        return items

    def visit(node, parent):
        path = "/".join(parent + [node.get("IORegistryEntryName", "?")])
        vendor, device = pci_number(node.get("vendor-id")), pci_number(node.get("device-id"))
        class_code = pci_number(node.get("class-code"))
        if vendor is not None and device is not None and (
                (class_code is not None and class_code >> 16 == 3) or (vendor, device) == (0x1002, 0x1638)):
            item = describe(node, path)
            for field, prop, width in [("vendor_id", "vendor-id", 4), ("device_id", "device-id", 4),
                                       ("revision_id", "revision-id", 2), ("class_code", "class-code", 6)]:
                value = pci_number(node.get(prop))
                item[field] = fact(format(value, "0" + str(width) + "x") if value is not None else None,
                                   "registry:" + path + ":" + prop)
            item["descendants"] = descendants(node, path)
            item["memory_properties"] = memory(node, path)
            item["properties"] = {key: json_value(node[key]) for key in (
                "model", "IOName", "pcidebug", "assigned-addresses", "IODeviceMemory",
                "IOInterruptControllers", "IOInterruptSpecifiers", "IOPCIMSIMode",
                "IOPowerManagement", "acpi-path") if key in node}
            devices.append(item)
        for child in node.get("IORegistryEntryChildren", []):
            visit(child, parent + [node.get("IORegistryEntryName", "?")])

    if isinstance(tree, dict):
        tree = [tree]
    if not isinstance(tree, list):
        raise ValueError("expected ioreg plist array or root dictionary")
    for root in tree:
        visit(root, [])
    return devices


def profiler_graphics(data):
    devices = []
    for gpu in data.get("SPDisplaysDataType", []):
        displays = gpu.get("spdisplays_ndrvs", gpu.get("_spdisplays_ndrvs", []))
        devices.append({"name": gpu.get("_name"),
                        "reported_memory": fact(gpu.get("spdisplays_vram", gpu.get("spdisplays_vram_shared")),
                                                "displays:spdisplays_vram/spdisplays_vram_shared"),
                        "metal_support": fact(gpu.get("spdisplays_metal", gpu.get("spdisplays_mtlgpufamilysupport")),
                                              "displays:spdisplays_metal/spdisplays_mtlgpufamilysupport"),
                        "properties": {key: value for key, value in gpu.items()
                                       if key not in ("spdisplays_ndrvs", "_spdisplays_ndrvs")},
                        "display_status": "available" if displays else "unavailable",
                        "displays": [{"name": display.get("_name"), "properties": display}
                                     for display in displays]})
    return devices


def build_report(output, sources):
    def read(name, parser):
        source = sources.get(name)
        if not source or source["status"] != "ok":
            return fact(None, name, (source or {}).get("error", "source unavailable"))
        try:
            value = parser((output / source["stdout_file"]).read_bytes())
            return fact(value if value else None, name, "empty result; unavailable, not proof of absence")
        except (ValueError, TypeError, KeyError, AttributeError, plistlib.InvalidFileException, ExpatError) as error:
            return fact(None, name, "parse failed: " + str(error))

    text = lambda data: data.decode("utf-8", "replace").strip()
    return {
        "schema_version": 1,
        "captured_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "scope": "read-only OS queries; reported properties are not direct hardware measurements",
        "sources": sources,
        "os": read("os", lambda data: {key.strip(): value.strip() for key, value in
                   (line.split(":", 1) for line in text(data).splitlines() if ":" in line)}),
        "architecture": read("architecture", text), "cpu": read("cpu", text),
        "registry": read("registry", lambda data: graphics_registry(plistlib.loads(data))),
        "graphics": read("displays", lambda data: profiler_graphics(json.loads(data))),
        "loaded_graphics_components": read("loaded_components", lambda data:
               [line for line in text(data).splitlines() if GRAPHICS_COMPONENT.search(line)]),
        "unresolved": ["physical UMA carveout and usable GPU memory", "physical connector routing",
                       "Metal vendor-driver discovery ABI", "user-client selectors and shared-memory layouts"],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path, help="new private capture directory")
    args = parser.parse_args()
    try:
        args.output.mkdir(parents=True, exist_ok=False, mode=0o700)
    except OSError as error:
        parser.exit(1, "Cannot create new output directory: " + str(error) + "\n")
    sources = {name: capture(name, argv, args.output) for name, argv in COMMANDS.items()}
    report = build_report(args.output, sources)
    (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    fields = ("os", "architecture", "cpu", "registry", "graphics", "loaded_graphics_components")
    partial = any(report[field]["status"] == "unavailable" for field in fields)
    print(str(args.output / "report.json"))
    for name, source in sources.items():
        print(name + ": " + source["status"] + " (exit " + str(source["exit_code"]) + ")")
    return 2 if partial else 0


if __name__ == "__main__":
    raise SystemExit(main())
