"""Recompute every stage 16 register offset from the pinned Linux v6.12 headers.

The headers live in ignored out/references (docs/test-boot.md, stage 16); the
test skips when they are absent, as in a fresh clone.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REFS = ROOT / "out" / "references"
HEADER = ROOT / "driver" / "core" / "cezanne_core.h"
# Bases measured in the stage 2 discovery table (equal to renoir_ip_offset.h).
SOURCES = (
    ("dcn_2_1_0_offset.h", (0x12, 0xC0, 0x34C0, 0x9000, 0x2403C00)),
    ("mmhub_1_0_offset.h", (0x1A000, 0x2408800)),
    ("osssys_4_0_offset.h", (0x10A0, 0x240A000)),
    ("nbio_7_0_offset.h", (0x0, 0x14, 0xD20, 0x10400, 0x241B000)),
)


def stage16_constants():
    """(name, offset) for each constant in the header's stage 16 block."""
    text = HEADER.read_text()
    block = text[text.index("// Stage 16:"):text.index("const uint32_t kDisplayInventory[]")]
    return [(name, int(offset, 16))
            for offset, name in re.findall(r"= 0x([0-9a-f]+); // (\w+):", block)]


@unittest.skipUnless(all((REFS / name).is_file() for name, _ in SOURCES), "pinned headers not in out/references")
class Inventory16OffsetTests(unittest.TestCase):
    def test_every_offset_matches_its_header(self):
        tables = []
        for name, bases in SOURCES:
            text = (REFS / name).read_text()
            regs = {m.group(1): int(m.group(2), 16)
                    for m in re.finditer(r"#define mm(\w+)\s+0x([0-9a-fA-F]+)\b", text) if not m.group(1).endswith("_BASE_IDX")}
            idx = {m.group(1): int(m.group(2))
                   for m in re.finditer(r"#define mm(\w+)_BASE_IDX\s+(\d+)", text)}
            tables.append((regs, idx, bases))
        constants = stage16_constants()
        self.assertEqual(len(constants), 91)
        self.assertNotIn("VM_INVALIDATE_ENG17_SEM", [name for name, _ in constants])
        for name, offset in constants:
            with self.subTest(name):
                found = [(bases[idx[name]] + regs[name]) * 4 for regs, idx, bases in tables if name in regs]
                self.assertEqual(found, [offset])
                self.assertLess(offset + 4, 0x80000 + 1)


if __name__ == "__main__":
    unittest.main()
