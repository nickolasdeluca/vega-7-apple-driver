"""Exercise the offline AIR -> AMDGPU lowering prototype on fixed AIR text."""
import importlib.util
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("air_lower", ROOT / "tools" / "air_lower.py")
air_lower = importlib.util.module_from_spec(spec)
spec.loader.exec_module(air_lower)
LLVM_BIN = ROOT / "out" / "shader-target" / "llvm" / "LLVM-20.1.7-macOS-X64" / "bin"

# store_index as Apple metal 32023.883 emits it, printed by LLVM 20.1.7's llvm-dis
# (metadata trimmed to what the lowering reads).
STORE_INDEX = '''target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-n8:16:32"
target triple = "air64_v28-apple-macosx26.0.0"

define void @store_index(ptr addrspace(1) nocapture noundef writeonly "air-buffer-no-alias" %0, i32 noundef %1) local_unnamed_addr #0 {
  %3 = zext i32 %1 to i64
  %4 = getelementptr inbounds i32, ptr addrspace(1) %0, i64 %3
  store i32 %1, ptr addrspace(1) %4, align 4
  ret void
}

attributes #0 = { mustprogress nounwind "frame-pointer"="all" }

!air.kernel = !{!0}
!llvm.module.flags = !{!5}
!air.version = !{!6}

!0 = !{ptr @store_index, !1, !2}
!1 = !{}
!2 = !{!3, !4}
!3 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.address_space", i32 1, !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"uint", !"air.arg_name", !"out"}
!4 = !{i32 1, !"air.thread_position_in_grid", !"air.arg_type_name", !"uint", !"air.arg_name", !"id"}
!5 = !{i32 7, !"air.max_device_buffers", i32 31}
!6 = !{i32 2, i32 8, i32 0}
'''

# A constant buffer at slot 2, a relaxed compare-exchange through a private
# alloca and a fast sine, in the same shapes as the inventory's k_atomics/k_math.
MIXED = '''target triple = "air64_v28-apple-macosx26.0.0"

define void @k_mixed(ptr addrspace(1) noundef "air-buffer-no-alias" %0, ptr addrspace(2) noundef readonly "air-buffer-no-alias" %1, <3 x i32> noundef %2) local_unnamed_addr #0 {
  %4 = alloca i32, align 4
  call void @llvm.lifetime.start.p0(i64 4, ptr nonnull %4)
  %5 = extractelement <3 x i32> %2, i64 0
  store i32 %5, ptr %4, align 4
  %6 = call i32 @air.atomic.global.cmpxchg.weak.i32(ptr addrspace(1) nocapture %0, ptr nocapture nonnull %4, i32 1, i32 0, i32 0, i32 2, i1 true)
  %7 = load float, ptr addrspace(2) %1, align 4
  %8 = tail call fast float @air.fast_sin.f32(float %7)
  call void @llvm.lifetime.end.p0(i64 4, ptr nonnull %4)
  ret void
}

declare i32 @air.atomic.global.cmpxchg.weak.i32(ptr addrspace(1) nocapture, ptr nocapture, i32, i32, i32, i32, i1)
declare float @air.fast_sin.f32(float)
declare float @air.fast_cos.f32(float)
declare void @llvm.lifetime.start.p0(i64, ptr)
declare void @llvm.lifetime.end.p0(i64, ptr)

attributes #0 = { nounwind }

!air.kernel = !{!0}

!0 = !{ptr @k_mixed, !1, !2}
!1 = !{}
!2 = !{!3, !4, !5}
!3 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.address_space", i32 1, !"air.arg_name", !"a"}
!4 = !{i32 1, !"air.buffer", !"air.location_index", i32 2, i32 1, !"air.read", !"air.address_space", i32 2, !"air.arg_name", !"c"}
!5 = !{i32 2, !"air.thread_position_in_grid", !"air.arg_type_name", !"uint3", !"air.arg_name", !"gid"}
'''


class AirLowerTests(unittest.TestCase):
    def assemble(self, text):
        """Parse and verify with LLVM 20's assembler when the study toolchain is present."""
        tool = LLVM_BIN / "llvm-as"
        if not tool.exists():
            return
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "m.ll"
            src.write_text(text)
            run = subprocess.run([str(tool), str(src), "-o", str(Path(tmp) / "m.bc")],
                                 capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stderr)

    def test_store_index_becomes_an_amdgpu_kernel(self):
        out = air_lower.lower(STORE_INDEX)
        self.assertIn('target triple = "amdgcn-amd-amdhsa"', out)
        self.assertNotIn("target datalayout", out)
        self.assertNotIn("!air.kernel =", out)
        self.assertIn("define amdgpu_kernel void @store_index(ptr addrspace(1) %slot0)", out)
        self.assertIn("define internal void @__air.store_index(", out)
        self.assertIn("alwaysinline #0", out)
        self.assertIn("call void @__air.store_index(ptr addrspace(1) %slot0, i32 %b1)", out)
        self.assertIn("@llvm.amdgcn.dispatch.ptr()", out)
        self.assertIn("!0 = !{ptr @__air.store_index", out)
        self.assemble(out)

    def test_slots_spaces_allocas_and_intrinsics(self):
        out = air_lower.lower(MIXED)
        # Slot n is kernarg n; the unused slot 1 keeps its place; slot 2 is constant.
        self.assertIn("@k_mixed(ptr addrspace(1) %slot0, ptr addrspace(1) %slot1, ptr addrspace(4) %slot2)", out)
        self.assertNotIn("addrspace(2)", out)
        self.assertIn("%__p5.4 = alloca i32, align 4, addrspace(5)", out)
        self.assertIn("%4 = addrspacecast ptr addrspace(5) %__p5.4 to ptr", out)
        self.assertNotIn("llvm.lifetime.start.p0(i64 4", out)
        self.assertIn('cmpxchg weak ptr addrspace(1) %p, i32 %e, i32 %desired syncscope("agent") monotonic monotonic', out)
        self.assertIn("store i32 %old, ptr %expected", out)
        self.assertIn("call fast float @llvm.sin.f32(float %x)", out)
        # Declared but never called: dropped, not lowered.
        self.assertNotIn("air.fast_cos", out)
        self.assertIn("ptr addrspace(1) %slot0, ptr addrspace(4) %slot2, <3 x i32> %gid.v2", out)
        self.assemble(out)

    def test_refusals(self):
        cases = {
            "only relaxed": MIXED.replace("i32 1, i32 0, i32 0, i32 2", "i32 1, i32 5, i32 0, i32 2"),
            "argument kind air.texture": MIXED.replace('!"air.buffer", !"air.location_index", i32 2, i32 1, !"air.read"',
                                                       '!"air.texture", !"air.location_index", i32 2, i32 1, !"air.read"'),
            "air.vertex": STORE_INDEX + "!air.vertex = !{!0}\n",
            "no kernel named": None,
        }
        for message, text in cases.items():
            with self.subTest(message=message):
                with self.assertRaisesRegex(air_lower.Unsupported, message):
                    if text is None:
                        air_lower.lower(STORE_INDEX, only="missing")
                    else:
                        air_lower.lower(text)
        with self.assertRaisesRegex(air_lower.Unsupported, "air.simd_sum"):
            air_lower.define_intrinsic("air.simd_sum.f32")

    def test_selecting_one_kernel_drops_the_others(self):
        both = MIXED.replace("!air.kernel = !{!0}", "!air.kernel = !{!0, !10}") + STORE_INDEX.split("attributes")[0].split("\n", 2)[2] + \
            '!10 = !{ptr @store_index, !1, !11}\n!11 = !{!12, !13}\n' + \
            '!12 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.address_space", i32 1}\n' + \
            '!13 = !{i32 1, !"air.thread_position_in_grid", !"air.arg_type_name", !"uint"}\n'
        out = air_lower.lower(both, only="store_index")
        self.assertNotIn("k_mixed(", out)
        self.assertIn("!0 = !{null, !1, !2}", out)
        self.assertNotIn("air.atomic", out)
        self.assemble(out)

    def test_type_suffixes(self):
        self.assertEqual(air_lower.llvm_type("v4f16"), "<4 x half>")
        self.assertEqual(air_lower.llvm_type("f32"), "float")
        self.assertEqual(air_lower.llvm_type("v2i32"), "<2 x i32>")
        with self.assertRaises(air_lower.Unsupported):
            air_lower.llvm_type("bf16")


if __name__ == "__main__":
    unittest.main()
