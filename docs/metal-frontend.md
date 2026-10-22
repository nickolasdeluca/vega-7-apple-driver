# Metal shader frontend boundary

This study answers the open half of the offline shader boundary study in the
[graphics contract](graphics-contract.md): what Apple's permitted, public Metal
toolchain emits for one fixed MSL compute kernel, and what an independent
backend for the Cezanne graphics core (`gfx90c`, see the
[shader target study](shader-target.md)) would still lack. Nothing was loaded,
dispatched or run on the GPU, no device library was created, and AIR was not
translated to any GPU ISA.

**Result, in short:**
- The public toolchain emits **AIR: LLVM bitcode with Apple metadata**, and the
  official LLVM 20.1.7 used for the shader target study reads it without
  errors. The kernel's argument bindings, built-in inputs and resource limits
  are explicit, readable metadata.
- **Apple's own AMD translator does not list Cezanne.** The toolchain ships an
  AIR native translator, `amdgpu-nt`, whose AMD plugin lists `gfx802`, `gfx803`,
  `gfx900`, `gfx904`, `gfx906`, `gfx1010`–`gfx1012`, `gfx1030`, `gfx1032` (and
  `gfx1010_nsgc`). It lists no `gfx902`, `gfx909` or `gfx90c`. So for this GPU
  there is no Apple-supplied backend to reuse, and an independent backend is
  required, not optional.
- **What a backend would still lack** is everything below AIR: the lowering of
  AIR intrinsics, address spaces and built-ins to AMDGPU, the resource-binding
  ABI that the runtime driver and the compiled code must agree on, and the
  pipeline-state format Metal hands the vendor driver. Readable AIR shows the
  input side only; it does **not** establish that Metal would accept an
  independent backend or call one.

## Experiment record

Offline, 2026-10-22, on the project host (macOS 26.4.1, x86_64). Ignored
`out/metal-frontend/` keeps the kernel, outputs, and every command's arguments,
UTC time, stdout/stderr and exit status (the capture wrapper from the
[shader target study](shader-target.md#reproduction)).

| Item | Value |
| --- | --- |
| Xcode | 26.5, build 17F42 (`xcodebuild -version`) |
| First compile | `xcrun -f metal` resolves, but every run exits 1: "cannot execute tool 'metal' due to missing Metal Toolchain; use: xcodebuild -downloadComponent MetalToolchain". `xcodebuild -showComponent MetalToolchain`: status uninstalled. `xcrun -f metallib` fails |
| Component | With the user's approval, `xcodebuild -downloadComponent MetalToolchain` (687.9 MB, Apple's asset service) installed "Metal Toolchain 17F42", toolchain `com.apple.dt.toolchain.Metal.32023.883`, mounted under `~/Library/Developer/DVTDownloads/MetalToolchain/mounts/` |
| Compiler | `Apple metal version 32023.883 (metalfe-32023.883)`, target `air64-apple-darwin25.4.0` |
| Reader | Official LLVM 20.1.7 (`llvm-bcanalyzer`, `llvm-dis`) from ignored `out/shader-target/` |

The fixed kernel, the MSL counterpart of the shader target study's
`store_index`:

```metal
// Fixed study kernel: each thread stores its x index into out[index].
#include <metal_stdlib>
using namespace metal;
kernel void store_index(device uint *out [[buffer(0)]], uint id [[thread_position_in_grid]]) {
    out[id] = id;
}
```

Commands, all exit 0 once the component was installed (`M=$(xcrun -f metal)`):

```sh
$M --version
$M -c store_index.metal -o store_index.air               # 3,360 bytes
$M store_index.metal -o store_index.metallib             # 3,860 bytes
$M -S -emit-llvm store_index.metal -o store_index.ll     # textual AIR
llvm-bcanalyzer -dump store_index.air                    # LLVM 20.1.7
llvm-dis store_index.air -o store_index.llvm20.ll        # LLVM 20.1.7
```

## The intermediate form

**`.air`** is an LLVM bitcode wrapper (magic `0x0b17c0de`, version 0, offset
0x14, size 0xd04, CPU type `0xffffffff`) around plain bitcode (`BC 0xC0DE`). Its
identification block has an empty producer string and epoch 0.

**Module:** target triple `air64_v28-apple-macosx26.0.0`, a 64-bit data layout
with vector alignments up to 1024 bits, AIR version `2.8.0`
(`!air.version`), language `Metal 4.0.0`. Apple's own textual form still uses
**typed pointers** (`i32 addrspace(1)*`), which places its LLVM base before
opaque pointers became the only form; LLVM 20 reads the bitcode and
auto-upgrades it to `ptr`. The two listings differ only in pointer spelling
and attribute syntax (`argmemonly writeonly` → `memory(argmem: write)`).

**The kernel**, after the frontend's optimization, is three instructions:
`zext` the id, `getelementptr` into address space 1, `store`. There are **no
AIR intrinsic calls** in this kernel: the thread position arrives as an
ordinary `i32` argument, and the buffer as an `addrspace(1)` pointer. Kernel
semantics live in named metadata:

| Metadata | Content |
| --- | --- |
| `!air.kernel` | the function, an empty list, and one node per argument |
| argument 0 | `air.buffer`, `air.location_index` 0 with count 1, `air.read_write`, `air.address_space` 1, type size 4, alignment 4, type name `uint`, name `out` |
| argument 1 | `air.thread_position_in_grid`, type name `uint`, name `id` |
| module flags | `air.max_device_buffers` 31, `max_constant_buffers` 31, `max_threadgroup_buffers` 31, `max_textures` 128, `max_read_write_textures` 8, `max_samplers` 16 |
| `!air.compile_options` | `denorms_disable`, `fast_math_enable`, `framebuffer_fetch_enable` |
| function attributes | fast-math set (`no-nans`, `no-infs`, `unsafe-fp-math`, …), `"air-buffer-no-alias"` on the buffer |
| other | `!air.source_file_name` (absolute path), TBAA and alias-scope nodes |

So a backend learns, from AIR alone, which argument is buffer slot 0 and which
is the grid position. It does not learn how the runtime will deliver slot 0
to the hardware (a user SGPR pair, a descriptor table, an argument buffer):
that is a contract between the backend and the vendor runtime driver, and it
is not in the file.

**`.metallib`** starts `MTLB`, followed by version fields (`0x8001`, 2, 9, 0)
and a total file size of 3,860 bytes. It contains tagged records (`NAME`,
`TYPE`, `HASH`, `MDSZ`, `OFFT`, `VERS`, each list closed by `ENDT`) and the
AIR bitcode itself at offset 341, unwrapped (the `.air` wrapper header is not
copied). The record layout was read from this one file only and is not
decoded further here.

## Apple's native translator

The toolchain's `bin` holds, beside `metal`, `metallib` and the usual `metal-*`
and `air-*` LLVM-style tools, three "native translator" drivers:
`applegpu-nt`, `intelgpu-nt` and `amdgpu-nt`. Only informational options were
run on `amdgpu-nt` (`--version`, `--help`, `-archs`, `-i`,
`-available-archs macos 26.0`); nothing was translated and no plugin was
loaded into this project's code.

- `amdgpu-nt --version`: `AIR-NT 32023.883`. Its help describes an "AIR Native
  Translator Driver" with stages `metal-script`, `materialize`, `translate`,
  `package`, translation plugins (`-load`, `-mtranslator`) and a CPU option
  (`-mcpu`).
- `-i` lists three vendor plugins: Apple's (`libapplegpu-nt.dylib`, through
  `wrapper-nt.dylib`), Intel's (`libMTLIGCCompilerPlugin.dylib`) and **AMD's
  (`libAMDNTPlugin.dylib`, with a second copy under `amd_13/`, vendor AMD,
  kind "Opaque (Bitcode Version 3902)", one entry with `MaxVersion
  13.255.255`)**.
- **AMD architectures:** `amdgpu_gfx802`, `gfx803`, `gfx900`, `gfx904`,
  `gfx906`, `gfx1010`, `gfx1010_nsgc`, `gfx1011`, `gfx1012`, `gfx1030`,
  `gfx1032`. The same list is reported as available for macOS 26.0. These are
  the discrete and Vega parts Apple shipped; the GFX9 APUs (`gfx902`, `gfx909`,
  `gfx90c`) are absent.

What this does and does not show:
- It shows that Apple's offline path for AMD is AIR → vendor plugin → native
  code, the same shape an independent backend would take.
- It does **not** show what the macOS 26 runtime does when a device's vendor
  driver is not Apple's or AMD's: whether `newLibraryWithSource:` and pipeline
  creation reach any vendor-replaceable compiler entry point. That is the
  [loader study](metal-loader-study.md)'s and the
  [admission](metal-admission.md) studies' question, and it remains open.
- `gfx900` is the nearest listed target. Whether its plugin output would run on
  `gfx90c` is not tested and is not a substitute: the plan forbids creating a
  device library through the AMD plugin, and the code-object format it emits is
  unknown.

## What an independent backend would still lack

From AIR, for `gfx90c`, using the public LLVM AMDGPU target:

1. **Lowering.** AIR is not AMDGPU IR. Built-ins arrive as metadata-tagged
   arguments (`air.thread_position_in_grid`) and, in larger kernels, as
   `air.*` intrinsic calls (none in this kernel; see the inventory below).
   Each needs a mapping to AMDGPU work-item/workgroup IDs, LDS, barriers,
   atomics and image instructions. Address spaces 1 and 3 match AMDGPU's
   global and local; 2 (constant) does not (see the inventory).
2. **The kernel ABI.** Metal buffer slots, textures and samplers must be
   assigned to user SGPRs, descriptor tables or argument buffers in a layout
   the runtime driver fills. Neither side of that contract exists yet.
3. **Fast-math and denormals.** AIR requests fast math and denormals off; the
   shader target study's `COMPUTE_PGM_RSRC1` has denormals on. The backend
   must translate `air.compile_options` into register state.
4. **Graphics stages.** Vertex/fragment AIR, stage-in, render-target and
   framebuffer-fetch semantics were not compiled here.
5. **Acceptance.** Whether Metal on this host would route compilation to an
   independent backend at all. That is a runtime question, not a toolchain one.

## License terms

The Metal toolchain is installed under the Xcode license (`License.rtf`,
"Xcode and Apple SDKs Agreement", license ID `EA1990`). Two terms matter here:
- section 2.7 ("Restrictions; No Other Permitted Uses") forbids decompiling, reverse engineering, disassembling or
  modifying "the Apple Software" (the toolchain, including `amdgpu-nt` and its
  plugins). This study ran only the documented command-line options and read
  **the output it produced from our own source**; it did not disassemble or
  inspect any Apple binary, and further work should keep to that;
- the agreement authorizes use "only for execution on an Apple-branded product
  running macOS" and forbids running it "on any non-Apple-branded computer".
  This project's host is a non-Apple PC running macOS. That applies to the
  existing Xcode install as much as to this component, and is recorded for the
  user's own judgment, not decided here.

## AIR intrinsic and metadata inventory

Added 2026-10-22, same toolchain, same rules: our own MSL source only, compiled
with `metal -S -emit-llvm` and `metal -c` (default options, so fast math on),
read as text. Three files in ignored `out/metal-frontend/inventory/` cover 13
functions:

| File | Functions | Covers |
| --- | --- | --- |
| `compute.metal` | `k_builtins`, `k_threadgroup`, `k_atomics`, `k_simd`, `k_math` | 8 compute built-ins, static and dynamic threadgroup memory, both barriers, five atomic forms, SIMD-group reductions and shuffles, common math |
| `textures.metal` | `k_texture`, `k_const_sampler`, `k_argbuf` | read, write and sample access, a bound and a constexpr sampler, gather, an argument buffer holding a pointer, a texture and a constant |
| `graphics.metal` | `v_main`, `v_pull`, `f_main`, `f_simple` | stage-in and vertex pulling, `position` and `point_size`, two render targets, framebuffer fetch, depth output, discard, derivatives, `front_facing`, `sample_id` |

All six compiles exit 0. LLVM 20.1.7's `llvm-dis` reads all three `.air` files
and its `llvm-as` re-assembles the output, which runs the IR verifier, with
exit 0. LLVM 20's `opt` refuses them ("unrecognized architecture 'air64_v28'"):
public LLVM parses AIR but has no AIR target, so any pass pipeline must first
retarget the module.

### Address spaces

| AIR | Used for | AMDGPU equivalent |
| --- | --- | --- |
| 0 | private (the `cmpxchg` expected value) | 5 (private/scratch) after `alloca` lowering |
| 1 | `device` buffers, and texture handles (`%struct._texture_2d_t addrspace(1)*`) | 1 (global) for buffers; textures need a descriptor, not a pointer |
| 2 | `constant` buffers, argument buffers, sampler handles, constexpr sampler state | 4 (constant) |
| 3 | `threadgroup`: a static array (an `addrspace(3)` global) and a `[[threadgroup(0)]]` argument | 3 (local/LDS) |

Only 1 and 3 line up numerically. A backend must renumber 2, and must decide
what an `addrspace(1)` texture pointer and an `addrspace(2)` sampler pointer
are at run time: on GFX9 they become a 256-bit image descriptor (T#) and a
128-bit sampler descriptor (S#) loaded into SGPRs, which the runtime driver
writes.

### Intrinsics

Thirty-nine distinct `air.*` declarations (counting type variants separately). Floating-point names have a
`fast_` form when fast math is on (`air.fast_sin`, not `air.sin`); with fast
math off the precise forms should appear instead, which this run did not
produce.

| Group | Declarations seen | Shape |
| --- | --- | --- |
| Barriers | `air.wg.barrier(i32, i32)`, `air.simdgroup.barrier(i32, i32)` | (memory flags, scope). Observed: `mem_threadgroup` → `(2, 1)`, `mem_device` → `(1, 1)`, a SIMD-group `mem_none` → `(0, 4)`. So flags 1 device, 2 threadgroup; scope 1 threadgroup, 4 SIMD-group (inferred from three calls) |
| Atomics | `air.atomic.{global,local}.*`: `add.u.i32`, `max.s.i32`, `add.f32`, `xchg.i32`, `cmpxchg.weak.i32` | (pointer, value(s), memory order, scope, `i1`). The relaxed calls pass order 0; scope 2 for device memory, 1 for threadgroup; the trailing `i1` was always `true` (meaning not established). `cmpxchg` takes the expected value through an `addrspace(0)` pointer, plus two orders |
| SIMD-group | `air.simd_sum`, `simd_broadcast` (lane as `i16`), `simd_shuffle_down` (`i16` delta), `simd_any` (`i1`), `simd_prefix_exclusive_sum` | Map to wave64 DPP/permute and reductions; the SIMD width is a built-in (`air.threads_per_simdgroup`), so 64 is the backend's answer |
| Textures | `air.read_texture_2d`, `write_texture_2d`, `sample_texture_2d`, `gather_texture_2d`, `get_width_texture_2d`, `get_height_texture_2d`, `air.get_read_sampler` | Results are `{ <4 x T>, i8 }` (the value plus an `i8`, plausibly a residency status). `sample` takes coordinates, an `i1`, an offset, an `i1` (explicit LOD or not), the LOD, a float and an `i32`. The last `i32` of read/write calls tracks the access qualifier (read 1, write 2, sample 0; inferred). `read` obtains its sampler from `air.get_read_sampler()` |
| Fragment | `air.discard_fragment`, `air.dfdx`, `air.dfdy`, `air.fwidth` | GFX9: exec-mask kill and quad-lane differences |
| Math | `air.fma`, `air.dot`, `air.fast_{sin, exp, sqrt, rsqrt, pow, fabs, floor, fract, clamp}` | Most map to single GFX9 instructions or short LLVM AMDGPU expansions |
| Conversion | `air.convert.f.v4f16.f.v4f32`, `air.convert.f.v4f32.f.v4f16`, `air.convert.f.f32.u.i32`, `air.convert.f.v2f32.u.v2i32` | The name encodes destination and source kind and type |

Plain LLVM intrinsics appear too: only `llvm.lifetime.start`/`end` here.

### Function signatures and metadata

The **entry point's IR signature is the stage's whole interface**: every
built-in, buffer, texture and sampler is a parameter, and vertex and fragment
outputs are a returned packed struct (`<{ <4 x float>, <2 x float>, float }>`
for `v_main`). Which parameter is what lives only in metadata:

- **Stage lists:** `!air.kernel`, `!air.vertex`, `!air.fragment`; each entry is
  (function, outputs, inputs).
- **Bindings:** `air.buffer`, `air.texture`, `air.sampler`, each with
  `air.location_index` (slot, count), an access kind (`air.read`,
  `air.read_write`, `air.write`, `air.sample`) and, for buffers,
  `air.address_space`, `air.arg_type_size`, `air.arg_type_align_size`, and
  `air.struct_type_info` for structured types such as `metal::_atomic`.
- **Dynamic threadgroup memory** is an `air.buffer` with `air.address_space` 3;
  its size is supplied at dispatch, so the backend needs a run-time LDS-size
  input.
- **Argument buffers:** `air.indirect_buffer` with `air.buffer_size` 24 and an
  `air.struct_type_info` list of (offset, size, …, type, name,
  `air.indirect_argument`) per member: the pointer at 0 (8 bytes), the texture
  at 8, the `uint` at 16. The IR loads the texture handle out of that struct,
  so **the argument buffer's in-memory layout is part of the ABI**. Apple's AMD
  stack defines what 8 bytes of a texture handle mean; an independent driver
  must define its own and match it in the backend.
- **Compute built-ins:** `air.thread_position_in_grid`,
  `thread_position_in_threadgroup`, `threadgroup_position_in_grid`,
  `threads_per_threadgroup`, `threads_per_grid`, `thread_index_in_simdgroup`,
  `simdgroup_index_in_threadgroup`, `threads_per_simdgroup`. On GFX9 the
  positions come from VGPR/SGPR system values; the sizes need kernel arguments
  or constants the driver provides.
- **Vertex:** `air.vertex_input` with `air.location_index` per `[[attribute]]`.
  Stage-in arrives as **already-fetched values** (`<3 x float>`), so the
  backend or the driver must generate the vertex fetch from the pipeline's
  vertex descriptor. Also `air.vertex_id`, `air.instance_id`; outputs
  `air.position`, `air.point_size`, and `air.vertex_output` with a generated
  linkage name (`generated(2uvDv2_f)`).
- **Fragment:** `air.fragment_input` with the same generated name and its
  interpolation (`air.center`, `air.perspective` / `air.no_perspective`),
  `air.position`, `air.front_facing`, `air.sample_id`; outputs
  `air.render_target` (index, …) and `air.depth` with `air.depth_qualifier`.
  **Framebuffer fetch** is an input tagged `air.render_target` 0.
- **Constexpr samplers** become an `addrspace(2)` constant of two `i64` (here
  `0x007bff0000080a49` and 0), listed in `!air.sampler_states`. The encoding is
  Apple's and is not decoded here; a backend must translate it into an S#.

### What this adds to the backend list

- Every intrinsic seen has a plausible GFX9 lowering, and nothing in the IR is
  opaque **except** three things a backend must define together with the
  runtime driver: the texture and sampler handle representation (pointers to
  opaque structs in address spaces 1 and 2), the argument-buffer layout of
  those handles, and the constexpr sampler encoding.
- Vertex fetch and render-target export are not in AIR. Stage-in values and
  returned structs must be lowered against pipeline state (vertex descriptor,
  attachment formats), so a graphics backend compiles per pipeline, not per
  function.
- Some operand meanings above are inferred from a handful of calls (barrier
  flags, atomic scopes, the texture access code, the trailing `i1`s). Pinning
  them needs more variants of the same source, not reverse engineering.

## Remaining questions

- Pinning the inferred operand meanings (barrier flags, atomic scope and the
  trailing `i1`, the texture access code, the `sample` flags) with more source
  variants, and the precise (non-`fast_`) math names with fast math off.
- The runtime question: whether macOS 26's Metal reaches any replaceable
  compiler path for a non-Apple, non-AMD-kext device.
- An offline AIR → AMDGPU lowering prototype with the public LLVM target, which
  this task explicitly deferred ("do not translate AIR yet").
