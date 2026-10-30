"""Stage 22's embedded kernel and stage 23's corpus are what tools/air_lower.py and LLVM 20.1.7 produce."""
import importlib.util
import re
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "driver" / "core" / "cezanne_core.h"
CORPUS_HEADER = ROOT / "driver" / "core" / "cezanne_corpus.h"
LLVM_BIN = ROOT / "out" / "shader-target" / "llvm" / "LLVM-20.1.7-macOS-X64" / "bin"
spec = importlib.util.spec_from_file_location("test_air_lower", ROOT / "tests" / "test_air_lower.py")
fixtures = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixtures)


def header_constants():
    text = HEADER.read_text()
    code = re.search(r"const uint32_t kComputeCode\[\] = \{([^}]*)\}", text).group(1)
    words = [int(w, 16) for w in re.findall(r"0x[0-9a-f]+", code)]
    rsrc = re.search(r"kComputePgmRsrc1 = (0x[0-9a-f]+), kComputePgmRsrc2 = (0x[0-9a-f]+);", text)
    return words, int(rsrc.group(1), 16), int(rsrc.group(2), 16)


def sections(path):
    data = path.read_bytes()
    shoff, = struct.unpack_from("<Q", data, 0x28)
    size, count, names_index = struct.unpack_from("<HHH", data, 0x3a)
    rows = [struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * size) for i in range(count)]
    names = data[rows[names_index][4]:rows[names_index][4] + rows[names_index][5]]
    found = {}
    for name, _, _, _, offset, length, *_ in rows:
        key = names[name:names.index(b"\0", name)].decode()
        found[key] = data[offset:offset + length]
    return found


class ComputeKernelTests(unittest.TestCase):
    def test_header_kernel_shape(self):
        words, rsrc1, rsrc2 = header_constants()
        self.assertEqual(len(words), 18)
        self.assertEqual(words[-1], 0xbf810000)  # s_endpgm
        self.assertEqual((rsrc2 >> 1) & 0x1f, 8)  # user SGPRs: segment buffer, dispatch packet, kernarg
        self.assertEqual((rsrc2 >> 7) & 1, 1)  # workgroup ID X
        self.assertEqual(rsrc1, 0x00af0040)

    @unittest.skipUnless((LLVM_BIN / "llc").exists(), "the study's LLVM 20.1.7 is not in out/")
    def test_header_kernel_matches_the_lowering(self):
        words, rsrc1, rsrc2 = header_constants()
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / "k.ll").write_text(fixtures.air_lower.lower(fixtures.STORE_INDEX))
            for argv in ([LLVM_BIN / "opt", "-mcpu=gfx90c", "-O2", "-S", tmp / "k.ll", "-o", tmp / "k.opt.ll"],
                         [LLVM_BIN / "llc", "-mcpu=gfx90c", "-filetype=obj", tmp / "k.opt.ll", "-o", tmp / "k.o"],
                         [LLVM_BIN / "ld.lld", "-shared", tmp / "k.o", "-o", tmp / "k.hsaco"]):
                run = subprocess.run([str(a) for a in argv], capture_output=True, text=True, timeout=120)
                self.assertEqual(run.returncode, 0, run.stderr)
            found = sections(tmp / "k.hsaco")
        text = found[".text"]
        self.assertEqual(list(struct.unpack("<%dI" % (len(text) // 4), text)), words)
        descriptor = found[".rodata"]
        _, got_rsrc1, got_rsrc2, properties = struct.unpack_from("<IIIH", descriptor, 44)
        self.assertEqual((got_rsrc1, got_rsrc2, properties), (rsrc1, rsrc2, 0xb))


class CorpusTests(unittest.TestCase):
    def test_corpus_header_shape(self):
        text = CORPUS_HEADER.read_text()
        rows = re.findall(r"\{kCorpus_(\w+), (\d+), (0x[0-9a-f]+), (0x[0-9a-f]+), (\d+), (\d+), (0x[0-9a-f]+)\}", text)
        self.assertEqual([row[0] for row in rows], ["store_index", "vector_add", "group_sum", "atomic_count", "grid_2d"])
        self.assertIn("const uint32_t kCorpusCount = 5;", text)
        for name, dwords, _, rsrc2, _, _, properties in rows:
            self.assertIn(int(properties, 16), (0xb, 0x9), name)
            self.assertEqual(int(rsrc2, 16) & 1, 0, name)  # no scratch
            self.assertLessEqual(int(dwords) * 4, 0x1000, name)
        # store_index is stage 22's kernel, word for word.
        code = re.search(r"const uint32_t kCorpus_store_index\[\] = \{([^}]*)\}", text).group(1)
        self.assertEqual([int(w, 16) for w in re.findall(r"0x[0-9a-f]+", code)], header_constants()[0])
        for name in (row[0] for row in rows):
            self.assertTrue((ROOT / "shaders" / "corpus" / (name + ".metal")).exists(), name)
            self.assertTrue((ROOT / "shaders" / "corpus" / (name + ".ll")).exists(), name)

    @unittest.skipUnless((LLVM_BIN / "llc").exists(), "the study's LLVM 20.1.7 is not in out/")
    def test_corpus_header_matches_the_build(self):
        run = subprocess.run(["python3", str(ROOT / "tools" / "build_corpus.py"), "--check"],
                             capture_output=True, text=True, timeout=600)
        self.assertEqual(run.returncode, 0, run.stderr)


if __name__ == "__main__":
    unittest.main()
