#!/usr/bin/env python3
"""Derive the USB test EFI from a copy of the known-good OpenCore EFI, and verify copies.

Python 3.9+, standard library only. Reads the known-good tree and writes a new
output directory; it never mounts, writes or erases disks, NVRAM or EFI
partitions. See docs/test-boot.md for the procedure and the boot rules.

build:  test_efi.py build --known-good EFI --kext CezanneProbe.kext --output DIR
        [--ocvalidate PATH]. Writes DIR/EFI and DIR/manifest.json. The tree is
        assembled in DIR.partial and renamed only after every check passes.
verify: test_efi.py verify --manifest DIR/manifest.json --side test|known_good EFI
        Compares an EFI folder (the USB copy, or the internal EFI after testing)
        with the hashes recorded at build time.
Exit 0: built or verified; 2: a check rejected the input; 1: usage or I/O error.
"""
import argparse
import copy
import hashlib
import json
import os
import plistlib
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

APPLE_BOOT_GUID = "7C436110-AB2A-4BBB-A880-FE41995C9F82"
TEST_BOOT_ARGS = "-v keepsyms=1 debug=0x100 msgbuf=1048576 -cezanne-probe"
# Kexts that claim or read the Cezanne GPU; none may run beside the probe.
REMOVED_KEXTS = {
    "NootedRed.kext": "drives the Cezanne iGPU through Apple's AMD kexts",
    "SMCRadeonSensors.kext": "reads AMD GPU sensor registers itself",
}
PROBE_BUNDLE = "CezanneProbe.kext"
PROBE_ENTRY = {
    "Arch": "x86_64",
    "BundlePath": PROBE_BUNDLE,
    "Comment": "Cezanne passive probe (test EFI only)",
    "Enabled": True,
    "ExecutablePath": "Contents/MacOS/CezanneProbe",
    "MaxKernel": "",
    "MinKernel": "25.0.0",
    "PlistPath": "Contents/Info.plist",
}
# The only config values the test EFI may change.
EXPECTED_CHANGES = {"Kernel.Add", "NVRAM.Add.%s.boot-args" % APPLE_BOOT_GUID,
                    "Misc.Security.AllowSetDefault"}
IGNORED_NAMES = (".DS_Store",)


class Rejected(ValueError):
    """The known-good tree or the derived EFI fails a safety check."""


def ignored(name):
    return name.startswith("._") or name in IGNORED_NAMES


def find_config(efi):
    oc = efi / "OC"
    matches = [p for p in oc.iterdir() if p.name.lower() == "config.plist"] if oc.is_dir() else []
    if len(matches) != 1:
        raise Rejected("expected exactly one OC/config.plist in %s, found %d" % (efi, len(matches)))
    return matches[0]


def is_config_backup(path, config):
    return path.parent == config.parent and path != config and path.name.lower().startswith("config.plist")


def tree(root):
    """Relative POSIX path -> SHA-256 for every regular file, skipping macOS metadata."""
    hashes = {}
    for directory, names, files in os.walk(root):
        names[:] = sorted(n for n in names if not ignored(n))
        for name in sorted(files):
            if ignored(name):
                continue
            path = Path(directory) / name
            if path.is_symlink():
                raise Rejected("symbolic link in EFI tree: %s" % path)
            hashes[path.relative_to(root).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    return hashes


def bundle_id(kext):
    info = kext / "Contents" / "Info.plist"
    with info.open("rb") as handle:
        return plistlib.load(handle)["CFBundleIdentifier"], info


def dependents(kexts_dir, removed_ids, kept):
    """Kept kexts whose OSBundleLibraries name a removed bundle."""
    found = []
    for entry in kept:
        info = kexts_dir / entry["BundlePath"] / entry.get("PlistPath", "Contents/Info.plist")
        with info.open("rb") as handle:
            libraries = plistlib.load(handle).get("OSBundleLibraries", {})
        found += ["%s needs %s" % (entry["BundlePath"], lib) for lib in libraries if lib in removed_ids]
    return found


def derive(config, kexts_dir):
    """Return (test config, {removed bundle: bundle id}). Reads only kext Info.plists."""
    test = copy.deepcopy(config)
    if config["Misc"]["Boot"].get("LauncherOption") != "Disabled":
        raise Rejected("Misc.Boot.LauncherOption must be Disabled: the test EFI must not "
                       "register itself as a firmware boot option")
    entries = config["Kernel"]["Add"]
    names = [e["BundlePath"] for e in entries]
    missing = sorted(set(REMOVED_KEXTS) - set(names))
    if missing:
        raise Rejected("known-good config lacks %s; review REMOVED_KEXTS before building" % missing)
    if PROBE_BUNDLE in names:
        raise Rejected("known-good config already lists %s" % PROBE_BUNDLE)
    removed_ids = {name: bundle_id(kexts_dir / name)[0] for name in REMOVED_KEXTS}
    kept = [e for e in entries if e["BundlePath"] not in REMOVED_KEXTS]
    blocked = dependents(kexts_dir, set(removed_ids.values()), [e for e in kept if e.get("Enabled")])
    if blocked:
        raise Rejected("kept kexts depend on removed ones: %s" % blocked)
    test["Kernel"]["Add"] = kept + [dict(PROBE_ENTRY)]
    test["NVRAM"]["Add"].setdefault(APPLE_BOOT_GUID, {})["boot-args"] = TEST_BOOT_ARGS
    test["Misc"]["Security"]["AllowSetDefault"] = False
    return test, removed_ids


def differences(a, b, prefix=""):
    """Dotted paths whose values differ; lists compare whole."""
    if isinstance(a, dict) and isinstance(b, dict):
        out = []
        for key in sorted(set(a) | set(b)):
            path = "%s.%s" % (prefix, key) if prefix else key
            out += differences(a[key], b[key], path) if key in a and key in b else [path]
        return out
    return [] if type(a) is type(b) and a == b else [prefix]


def check_configs(known, test):
    """Raise Rejected unless the test config differs from known-good only as intended."""
    changed = set(differences(known, test))
    if changed != EXPECTED_CHANGES:
        raise Rejected("unexpected config differences: extra %s, missing %s"
                       % (sorted(changed - EXPECTED_CHANGES), sorted(EXPECTED_CHANGES - changed)))
    expected_kexts = [e for e in known["Kernel"]["Add"] if e["BundlePath"] not in REMOVED_KEXTS]
    if test["Kernel"]["Add"] != expected_kexts + [PROBE_ENTRY]:
        raise Rejected("Kernel.Add must be the known-good list minus %s plus the probe, in order"
                       % sorted(REMOVED_KEXTS))
    # Each boot must overwrite every NVRAM value the other one sets differently.
    for guid, values in test["NVRAM"]["Add"].items():
        for key, value in values.items():
            if known["NVRAM"]["Add"].get(guid, {}).get(key, KeyError) == value:
                continue
            for side, cfg in (("known-good", known), ("test", test)):
                if key not in cfg["NVRAM"].get("Delete", {}).get(guid, []):
                    raise Rejected("%s NVRAM.Delete.%s lacks %s; the value would persist across boots"
                                   % (side, guid, key))
    if test["Misc"]["Boot"].get("LauncherOption") != "Disabled":
        raise Rejected("test Misc.Boot.LauncherOption must be Disabled")


def check_tree(known_hashes, test_hashes, config_rel, skipped):
    """Raise Rejected unless files differ only by config, removed kexts and the probe."""
    removed = {p for p in known_hashes for k in REMOVED_KEXTS if p.startswith("OC/Kexts/%s/" % k)}
    probe = {p for p in test_hashes if p.startswith("OC/Kexts/%s/" % PROBE_BUNDLE)}
    want = (set(known_hashes) - removed - set(skipped)) | probe
    if set(test_hashes) != want:
        raise Rejected("test tree files: extra %s, missing %s" % (sorted(set(test_hashes) - want),
                                                                 sorted(want - set(test_hashes))))
    changed = sorted(p for p in set(known_hashes) & set(test_hashes)
                     if p != config_rel and known_hashes[p] != test_hashes[p])
    if changed:
        raise Rejected("copied files differ from known-good: %s" % changed)
    if not probe:
        raise Rejected("probe kext missing from test tree")


def check_bundles(efi, config):
    for entry in config["Kernel"]["Add"]:
        if not entry.get("Enabled"):
            continue
        bundle = efi / "OC" / "Kexts" / entry["BundlePath"]
        for rel in filter(None, (entry.get("PlistPath"), entry.get("ExecutablePath"))):
            if not (bundle / rel).is_file():
                raise Rejected("enabled kext file missing: %s" % (bundle / rel))


def run_ocvalidate(tool, config):
    process = subprocess.run([str(tool), str(config)], capture_output=True, text=True, timeout=60)
    result = {"argv": [str(tool), str(config)], "exit_code": process.returncode,
              "stdout": process.stdout, "stderr": process.stderr}
    if process.returncode != 0:
        raise Rejected("ocvalidate exited %d: %s" % (process.returncode,
                                                   (process.stdout + process.stderr).strip()[-2000:]))
    return result


def build(known_efi, kext, output, ocvalidate=None):
    known_efi, kext, output = Path(known_efi), Path(kext), Path(output)
    partial = output.with_name(output.name + ".partial")
    for path in (output, partial):
        if path.exists():
            raise FileExistsError("refusing to overwrite %s" % path)
    if not (kext / "Contents" / "Info.plist").is_file():
        raise Rejected("not a kext bundle: %s" % kext)
    config_path = find_config(known_efi)
    with config_path.open("rb") as handle:
        known = plistlib.load(handle)
    test, removed_ids = derive(known, known_efi / "OC" / "Kexts")
    config_rel = config_path.relative_to(known_efi).as_posix()
    skipped = sorted(p.relative_to(known_efi).as_posix() for p in config_path.parent.iterdir()
                     if p.is_file() and is_config_backup(p, config_path))
    removed_dirs = [known_efi / "OC" / "Kexts" / name for name in REMOVED_KEXTS]

    def skip(directory, names):
        here = Path(directory)
        return [n for n in names if ignored(n) or here / n in removed_dirs
                or (here / n).relative_to(known_efi).as_posix() in skipped]

    efi = partial / "EFI"
    try:
        shutil.copytree(known_efi, efi, ignore=skip)
        shutil.copytree(kext, efi / "OC" / "Kexts" / PROBE_BUNDLE, ignore=lambda d, n: [x for x in n if ignored(x)])
        with (efi / config_rel).open("wb") as handle:
            plistlib.dump(test, handle, sort_keys=False)
        with (efi / config_rel).open("rb") as handle:
            written = plistlib.load(handle)
        known_hashes, test_hashes = tree(known_efi), tree(efi)
        check_configs(known, written)
        check_tree(known_hashes, test_hashes, config_rel, skipped)
        check_bundles(efi, written)
        validation = run_ocvalidate(ocvalidate, efi / config_rel) if ocvalidate else None
    except Rejected as error:
        # Keep the rejected tree for inspection under a name no step will copy.
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        kept = output.with_name("%s.rejected-%s" % (output.name, stamp))
        partial.rename(kept)
        raise Rejected("%s (tree kept at %s)" % (error, kept)) from None
    manifest = {
        "created_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "known_good": str(known_efi.resolve()), "probe_kext": str(kext.resolve()),
        "config": config_rel, "boot_args": TEST_BOOT_ARGS,
        "removed_kexts": {name: {"bundle_id": removed_ids[name], "reason": REMOVED_KEXTS[name]}
                          for name in REMOVED_KEXTS},
        "skipped_config_backups": skipped, "config_changes": sorted(EXPECTED_CHANGES),
        "ocvalidate": validation, "known_good_sha256": known_hashes, "test_sha256": test_hashes,
    }
    (partial / "manifest.json").write_text(json.dumps(manifest, indent=1) + "\n")
    partial.rename(output)
    return manifest


def verify(manifest_path, side, efi):
    manifest = json.loads(Path(manifest_path).read_text())
    expected = manifest["test_sha256" if side == "test" else "known_good_sha256"]
    if side == "known_good":
        expected = {p: h for p, h in expected.items() if p not in manifest["skipped_config_backups"]}
    actual = tree(Path(efi))
    if side == "known_good":
        actual = {p: h for p, h in actual.items() if p not in manifest["skipped_config_backups"]}
    report = {"side": side, "efi": str(efi), "files": len(actual),
              "missing": sorted(set(expected) - set(actual)), "extra": sorted(set(actual) - set(expected)),
              "changed": sorted(p for p in set(expected) & set(actual) if expected[p] != actual[p])}
    report["match"] = not (report["missing"] or report["extra"] or report["changed"])
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    commands = parser.add_subparsers(dest="command", required=True)
    make = commands.add_parser("build")
    make.add_argument("--known-good", required=True, help="copy of the known-good EFI folder")
    make.add_argument("--kext", required=True, help="built CezanneProbe.kext")
    make.add_argument("--output", required=True, help="new directory for the test EFI")
    make.add_argument("--ocvalidate", help="ocvalidate matching the OpenCore version")
    check = commands.add_parser("verify")
    check.add_argument("--manifest", required=True)
    check.add_argument("--side", required=True, choices=("test", "known_good"))
    check.add_argument("efi", help="EFI folder to compare")
    args = parser.parse_args(argv)
    try:
        if args.command == "build":
            manifest = build(args.known_good, args.kext, args.output, args.ocvalidate)
            print(json.dumps({"output": args.output, "config_changes": manifest["config_changes"],
                              "removed_kexts": sorted(manifest["removed_kexts"]),
                              "ocvalidate_exit": (manifest["ocvalidate"] or {}).get("exit_code"),
                              "test_files": len(manifest["test_sha256"])}, indent=1))
            return 0
        report = verify(args.manifest, args.side, args.efi)
        print(json.dumps(report, indent=1))
        return 0 if report["match"] else 2
    except Rejected as error:
        print(json.dumps({"rejected": str(error)}, indent=1))
        return 2
    except (OSError, KeyError, ValueError, plistlib.InvalidFileException, subprocess.SubprocessError) as error:
        print("error: %s" % error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
