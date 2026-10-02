"""Header checks must reject malformed bounds; fixtures are synthetic, not AMD firmware."""
import importlib.util
import io
import json
import struct
import tempfile
import unittest
import zlib
from contextlib import redirect_stdout
from pathlib import Path

MODULE_PATH = Path(__file__).resolve().parents[1] / "tools" / "amdgpu_firmware.py"


def image(kind, extension, major=1, minor=0, payload=256, offset=None, header_size=None,
          size=None, crc=0, tail=b""):
    """Common header + extension u32 fields, zero padding to offset, payload bytes, tail."""
    header_size = 32 + 4 * len(extension) if header_size is None else header_size
    offset = max(header_size, 64) if offset is None else offset
    body = bytes(range(256)) * (payload // 256) + bytes(payload % 256)
    total = offset + payload + len(tail)
    head = struct.pack("<IIHHHHIIII", total if size is None else size, header_size, major, minor,
                       9, 3, 1, payload, offset, crc) + struct.pack("<%dI" % len(extension), *extension)
    return head + bytes(offset - len(head)) + body + tail


class FirmwareHeaderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location("amdgpu_firmware", MODULE_PATH)
        cls.fw = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.fw)

    def rejects(self, data, kind, message):
        with self.assertRaisesRegex(self.fw.FirmwareFormatError, message):
            self.fw.parse(data, kind)

    def test_accepts_gfx_jump_table_inside_ucode_and_reports_crc_without_enforcing(self):
        result = self.fw.parse(image("gfx", [7, 16, 8]), "gfx")
        jump = [r for r in result["regions"] if r["name"] == "jump_table"][0]
        self.assertEqual((jump["start"], jump["end"]), (64 + 64, 64 + 96))
        self.assertEqual(result["fields"]["ucode_feature_version"], 7)
        self.assertFalse(result["crc32_field_matches_payload"])
        self.assertFalse(result["payload_interpreted"])

    def test_crc_match_is_observed(self):
        payload = (bytes(range(256)))
        data = image("common", [], crc=zlib.crc32(payload))
        self.assertTrue(self.fw.parse(data, "common")["crc32_field_matches_payload"])

    def test_rejects_short_file_and_size_mismatch(self):
        self.rejects(b"\0" * 31, "gfx", "shorter than the 32-byte")
        self.rejects(image("gfx", [0, 0, 0], size=999), "gfx", "does not equal file size")

    def test_rejects_unsupported_kind_version(self):
        self.rejects(image("gfx", [0, 0, 0], major=2), "gfx", "unsupported gfx header version 2.0")
        self.rejects(image("rlc", [0] * 18, major=2, minor=2), "rlc", "unsupported rlc header version 2.2")

    def test_rejects_header_smaller_than_layout_or_past_ucode(self):
        self.rejects(image("gfx", [0, 0, 0], header_size=40), "gfx", "smaller than the 44-byte layout")
        self.rejects(image("gfx", [0, 0, 0], header_size=80, offset=64), "gfx", "past ucode_array_offset")

    def test_rejects_empty_or_out_of_file_ucode(self):
        self.rejects(image("common", [], payload=0), "common", "ucode_size_bytes is zero")
        data = bytearray(image("common", []))
        struct.pack_into("<I", data, 20, 4096)
        self.rejects(bytes(data), "common", "ucode .* is outside file")

    def test_rejects_32_bit_wrap(self):
        data = bytearray(image("common", []))
        struct.pack_into("<II", data, 20, 0x20, 0xFFFFFFF0)
        self.rejects(bytes(data), "common", "wraps 32-bit arithmetic")

    def test_rejects_jump_table_outside_ucode(self):
        self.rejects(image("gfx", [0, 60, 8]), "gfx", "jump_table .* is outside ucode")

    def rlc(self, **changes):
        fields = dict.fromkeys(self.fw.RLC_V2_1, 0)
        fields.update(changes)
        return image("rlc", [fields[name] for name in self.fw.RLC_V2_1], major=2, minor=1,
                     tail=b"\1" * 32)

    def test_accepts_rlc_lists_after_header_and_rejects_bad_lists(self):
        ok = self.fw.parse(self.rlc(reg_list_size_bytes=16, reg_list_array_offset_bytes=156 + 256,
                                    save_restore_list_srm_size_bytes=16,
                                    save_restore_list_srm_offset_bytes=156 + 256 + 16), "rlc")
        self.assertEqual([r["name"] for r in ok["regions"][2:]], ["reg_list", "save_restore_list_srm"])
        self.rejects(self.rlc(reg_list_size_bytes=6, reg_list_array_offset_bytes=412), "rlc",
                     "not a whole number of dwords")
        self.rejects(self.rlc(reg_list_size_bytes=64, reg_list_array_offset_bytes=412), "rlc",
                     "reg_list .* is outside file")
        self.rejects(self.rlc(save_restore_list_cntl_size_bytes=8,
                              save_restore_list_cntl_offset_bytes=8), "rlc", "overlaps the header")

    def ta(self, **descriptors):
        values = []
        for name in self.fw.TA_DESCRIPTORS:
            values += list(descriptors.get(name, (0, 0, 0)))
        return image("ta", values)

    def test_ta_regions_follow_linux_start_rules(self):
        result = self.fw.parse(self.ta(hdcp=(1, 9999, 128), dtm=(2, 128, 128)), "ta")
        regions = {r["name"]: (r["start"], r["end"]) for r in result["regions"]}
        self.assertEqual(regions["ta_hdcp"], (92, 220))  # hdcp's own offset field is not used
        self.assertEqual(regions["ta_dtm"], (220, 348))
        self.rejects(self.ta(dtm=(2, 200, 128)), "ta", "ta_dtm .* is outside ucode")

    def test_dmcub_requires_psp_wrapper_and_bounded_bss(self):
        self.assertEqual(self.fw.parse(image("dmcub", [512, 0], payload=512), "dmcub")["regions"][2]["end"], 64 + 512)
        self.rejects(image("dmcub", [0x1FF, 0], payload=512), "dmcub", "smaller than its 0x100 PSP")
        self.rejects(image("dmcub", [512, 8], payload=512), "dmcub", "dmcub_bss_data .* is outside ucode")

    def test_infers_kind_from_file_name(self):
        self.assertEqual(self.fw.kind_for_name("green_sardine_mec2.bin"), "gfx")
        self.assertEqual(self.fw.kind_for_name("green_sardine_asd.bin"), "psp_asd")
        with self.assertRaisesRegex(self.fw.FirmwareFormatError, "no header kind"):
            self.fw.kind_for_name("green_sardine_sos.bin")

    def test_cli_exit_codes_and_rejection_report(self):
        with tempfile.TemporaryDirectory() as directory:
            good = Path(directory) / "chip_me.bin"
            bad = Path(directory) / "chip_sdma.bin"
            good.write_bytes(image("gfx", [0, 0, 0]))
            bad.write_bytes(image("sdma", [0, 0, 0, 0], size=1))
            for files, expected in (([good], 0), ([good, bad], 2)):
                output = io.StringIO()
                with redirect_stdout(output):
                    self.assertEqual(self.fw.main([str(f) for f in files]), expected)
                report = json.loads(output.getvalue())["files"]
                self.assertEqual(report[0]["status"], "accepted")
            self.assertEqual(report[1]["status"], "rejected")
            self.assertIn("does not equal file size", report[1]["error"])
            self.assertEqual(self.fw.main([str(Path(directory) / "missing_me.bin")]), 1)


if __name__ == "__main__":
    unittest.main()
