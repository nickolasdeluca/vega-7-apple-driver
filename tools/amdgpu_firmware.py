#!/usr/bin/env python3
"""Validate documented AMD GPU firmware header fields without interpreting payloads.

Python 3.9+, standard library only. Reads files; never loads, sends or modifies
firmware. Field layouts follow Linux v6.12 amdgpu_ucode.h; region rules follow
the v6.12 consumers cited in docs/firmware-provenance.md and are stricter than
Linux. Payloads stay opaque byte ranges: AMD's microcode license forbids reverse
engineering, decompiling or disassembling them.
Exit 0: every file accepted; 2: at least one file rejected; 1: usage/IO error.
"""
import argparse
import hashlib
import json
import re
import struct
import sys
import zlib
from pathlib import Path

COMMON = ("size_bytes", "header_size_bytes", "header_version_major", "header_version_minor",
          "ip_version_major", "ip_version_minor", "ucode_version", "ucode_size_bytes",
          "ucode_array_offset_bytes", "crc32")
COMMON_FORMAT = "<IIHHHHIIII"
COMMON_SIZE = struct.calcsize(COMMON_FORMAT)
U32_MAX = 0xFFFFFFFF
DMCUB_PSP_HEADER_AND_FOOTER = 0x100 + 0x100

RLC_V2_0 = ("ucode_feature_version", "jt_offset", "jt_size", "save_and_restore_offset",
            "clear_state_descriptor_offset", "avail_scratch_ram_locations",
            "reg_restore_list_size", "reg_list_format_start", "reg_list_format_separate_start",
            "starting_offsets_start", "reg_list_format_size_bytes",
            "reg_list_format_array_offset_bytes", "reg_list_size_bytes",
            "reg_list_array_offset_bytes", "reg_list_format_separate_size_bytes",
            "reg_list_format_separate_array_offset_bytes", "reg_list_separate_size_bytes",
            "reg_list_separate_array_offset_bytes")
RLC_V2_1 = RLC_V2_0 + ("reg_list_format_direct_reg_list_length",) + tuple(
    "save_restore_list_%s_%s" % (name, field) for name in ("cntl", "gpm", "srm")
    for field in ("ucode_ver", "feature_ver", "size_bytes", "offset_bytes"))
TA_DESCRIPTORS = ("xgmi", "ras", "hdcp", "dtm", "securedisplay")

# (kind, header major, header minor) -> extension field names (all little-endian u32).
LAYOUTS = {
    ("gfx", 1, 0): ("ucode_feature_version", "jt_offset", "jt_size"),
    ("rlc", 2, 0): RLC_V2_0,
    ("rlc", 2, 1): RLC_V2_1,
    ("sdma", 1, 0): ("ucode_feature_version", "ucode_change_version", "jt_offset", "jt_size"),
    ("psp_asd", 1, 0): ("sos_fw_version", "sos_offset_bytes", "sos_size_bytes"),
    ("ta", 1, 0): tuple("%s_%s" % (name, field) for name in TA_DESCRIPTORS
                        for field in ("fw_version", "offset_bytes", "size_bytes")),
    ("dmcub", 1, 0): ("inst_const_bytes", "bss_data_bytes"),
    ("common", 1, 0): (),
}
KIND_BY_SUFFIX = {"ce": "gfx", "me": "gfx", "pfp": "gfx", "mec": "gfx", "mec2": "gfx",
                  "rlc": "rlc", "sdma": "sdma", "asd": "psp_asd", "ta": "ta",
                  "dmcub": "dmcub", "vcn": "common"}


class FirmwareFormatError(ValueError):
    """The file does not satisfy the documented header and bounds rules."""


def kind_for_name(name):
    match = re.fullmatch(r"[a-z0-9_]+_([a-z0-9]+)\.bin", name)
    if not match or match.group(1) not in KIND_BY_SUFFIX:
        raise FirmwareFormatError("no header kind known for file name " + repr(name))
    return KIND_BY_SUFFIX[match.group(1)]


def region(name, start, size, rule):
    if start > U32_MAX or size > U32_MAX or start + size > U32_MAX:
        raise FirmwareFormatError("%s wraps 32-bit arithmetic" % name)
    return {"name": name, "start": start, "end": start + size, "size": size, "rule": rule}


def within(child, parent):
    if not parent["start"] <= child["start"] <= child["end"] <= parent["end"]:
        raise FirmwareFormatError("%s [%d, %d) is outside %s [%d, %d)" % (
            child["name"], child["start"], child["end"], parent["name"], parent["start"], parent["end"]))
    return child


def regions_for(kind, fields, header, ucode, file_region):
    out = []
    if kind == "gfx" and fields["jt_size"]:
        out.append(within(region("jump_table", ucode["start"] + 4 * fields["jt_offset"],
                                 4 * fields["jt_size"], "dwords from ucode start"), ucode))
    elif kind == "rlc":
        lists = [("reg_list_format", "reg_list_format_array_offset_bytes", "reg_list_format_size_bytes"),
                 ("reg_list", "reg_list_array_offset_bytes", "reg_list_size_bytes")]
        if "save_restore_list_cntl_size_bytes" in fields:
            lists += [("save_restore_list_" + name, "save_restore_list_%s_offset_bytes" % name,
                       "save_restore_list_%s_size_bytes" % name) for name in ("cntl", "gpm", "srm")]
        for name, offset, size in lists:
            if fields[size] % 4:
                raise FirmwareFormatError("%s size is not a whole number of dwords" % name)
            if fields[size]:
                item = within(region(name, fields[offset], fields[size], "bytes from file start"), file_region)
                if item["start"] < header["end"]:
                    raise FirmwareFormatError("%s overlaps the header" % name)
                out.append(item)
    elif kind == "ta":
        # Linux v6.12 parse_ta_v1_microcode: xgmi and hdcp start at the ucode array;
        # ras follows xgmi, dtm and securedisplay follow hdcp by their offsets.
        for name in TA_DESCRIPTORS:
            if not fields[name + "_size_bytes"]:
                continue
            base = 0 if name in ("xgmi", "hdcp") else fields[name + "_offset_bytes"]
            out.append(within(region("ta_" + name, ucode["start"] + base, fields[name + "_size_bytes"],
                                     "linux v6.12 descriptor start"), ucode))
    elif kind == "dmcub":
        if fields["inst_const_bytes"] < DMCUB_PSP_HEADER_AND_FOOTER:
            raise FirmwareFormatError("inst_const_bytes is smaller than its 0x100 PSP header and footer")
        inst = within(region("dmcub_inst_const", ucode["start"], fields["inst_const_bytes"],
                             "from ucode start, including PSP header and footer"), ucode)
        out.append(inst)
        if fields["bss_data_bytes"]:
            out.append(within(region("dmcub_bss_data", inst["end"], fields["bss_data_bytes"],
                                     "follows inst_const"), ucode))
    return out


def parse(data, kind):
    if len(data) < COMMON_SIZE:
        raise FirmwareFormatError("file is shorter than the 32-byte common header")
    common = dict(zip(COMMON, struct.unpack_from(COMMON_FORMAT, data)))
    if common["size_bytes"] != len(data):
        raise FirmwareFormatError("size_bytes %d does not equal file size %d" % (common["size_bytes"], len(data)))
    version = (kind, common["header_version_major"], common["header_version_minor"])
    if version not in LAYOUTS:
        raise FirmwareFormatError("unsupported %s header version %d.%d" % version)
    names = LAYOUTS[version]
    layout_size = COMMON_SIZE + 4 * len(names)
    if common["header_size_bytes"] < layout_size:
        raise FirmwareFormatError("header_size_bytes %d is smaller than the %d-byte layout" % (
            common["header_size_bytes"], layout_size))
    if common["header_size_bytes"] > common["ucode_array_offset_bytes"]:
        raise FirmwareFormatError("header extends past ucode_array_offset_bytes")
    if not common["ucode_size_bytes"]:
        raise FirmwareFormatError("ucode_size_bytes is zero")
    file_region = region("file", 0, len(data), "whole file")
    header = within(region("header", 0, common["header_size_bytes"], "from file start"), file_region)
    ucode = within(region("ucode", common["ucode_array_offset_bytes"], common["ucode_size_bytes"],
                          "from file start"), file_region)
    fields = dict(zip(names, struct.unpack_from("<%dI" % len(names), data, COMMON_SIZE)))
    regions = [header, ucode] + regions_for(kind, fields, header, ucode, file_region)
    payload_crc = zlib.crc32(data[ucode["start"]:ucode["end"]]) & U32_MAX
    return {"kind": kind, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(),
            "common": common, "fields": fields, "regions": regions,
            "crc32_field_matches_payload": payload_crc == common["crc32"],
            "payload_interpreted": False}


def inspect(path, kind=None):
    path = Path(path)
    result = {"file": path.name}
    try:
        result.update(status="accepted", **parse(path.read_bytes(), kind or kind_for_name(path.name)))
    except FirmwareFormatError as error:
        result.update(status="rejected", error=str(error))
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--kind", choices=sorted({kind for kind, _, _ in LAYOUTS}),
                        help="header kind; inferred from <chip>_<block>.bin names by default")
    parser.add_argument("files", nargs="+")
    args = parser.parse_args(argv)
    try:
        results = [inspect(name, args.kind) for name in args.files]
    except OSError as error:
        print(str(error), file=sys.stderr)
        return 1
    json.dump({"files": results}, sys.stdout, indent=2)
    sys.stdout.write("\n")
    return 0 if all(item["status"] == "accepted" for item in results) else 2


if __name__ == "__main__":
    sys.exit(main())
