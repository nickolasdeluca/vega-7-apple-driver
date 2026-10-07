"""Recompute every stage 16, 17, 19 and 20 register offset from the pinned Linux v6.12 headers.

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


def block_constants(start, end):
    """(name, offset) for each annotated constant in one header block."""
    text = HEADER.read_text()
    block = text[text.index(start):text.index(end)]
    return [(name, int(offset, 16))
            for offset, name in re.findall(r"= 0x([0-9a-f]+); // (\w+):", block)]


def stage16_constants():
    return block_constants("// Stage 16:", "const uint32_t kDisplayInventory[]")


def stage17_constants():
    return block_constants("// Stage 17:", "// Read from stage 17 on")


def stage19_constants():
    return block_constants("// Stage 19:", "// Read from stage 19 on")


def stage20_constants():
    return block_constants("// Stage 20:", "// Read from stage 20 on")


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
        # Stage 17's engine 17 semaphore, request and address range.
        stage17 = stage17_constants()
        self.assertEqual([name for name, _ in stage17],
                         ["VM_INVALIDATE_ENG17_SEM", "VM_INVALIDATE_ENG17_REQ", "VM_INVALIDATE_ENG17_ADDR_RANGE_LO32",
                          "VM_INVALIDATE_ENG17_ADDR_RANGE_HI32"])
        # Stage 19's flip status, surface state and frame counter.
        stage19 = stage19_constants()
        self.assertEqual(len(stage19), 9)
        # Stage 20's flip interrupt control, routing and status.
        stage20 = stage20_constants()
        self.assertEqual([name for name, _ in stage20],
                         ["HUBPREQ0_DCSURF_SURFACE_FLIP_INTERRUPT", "DCHUB_INTERRUPT_DEST2",
                          "DISP_INTERRUPT_STATUS_CONTINUE17"])
        for name, offset in constants + stage17 + stage19 + stage20:
            with self.subTest(name):
                found = [(bases[idx[name]] + regs[name]) * 4 for regs, idx, bases in tables if name in regs]
                self.assertEqual(found, [offset])
                self.assertLess(offset + 4, 0x80000 + 1)


if __name__ == "__main__":
    unittest.main()
