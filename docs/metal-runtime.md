# Metal runtime compilation path

This study answers, by observation only, the runtime question left by the
[Metal frontend study](metal-frontend.md): where macOS 26's Metal turns a
shader into GPU code on this host, which images do it, and what that implies
for an independent driver's backend. It complements the
[loader](metal-loader-study.md) and [admission](metal-admission.md) studies,
which cover how Metal finds and constructs the vendor device.

**Method, as the user set it on 2026-10-22:** observation only. That means file
and `Info.plist` inventory, public API calls from our own process, and
watching the system from outside (`ps`, `lsof`, `vmmap`, `log`, `codesign -d`,
`csrutil status`, `nvram`). No Apple or AMD binary was disassembled and no
strings or symbol tables were read. The user also allowed our program to
compile a kernel and create a pipeline on the current device, with **no
command queue, command buffer or dispatch**.

**Result, in short:**
- Compilation does not run in the app. Each app gets its own
  `MTLCompilerService` XPC instance. It turns MSL into AIR with Apple's
  compiler, then, for the pipeline, loads **AMD's own shader compiler** into
  itself for the duration of the compile: `AMDRadeonX5000Shared.bundle` and
  `AMDShared.bundle`'s `libAMDIL902.dylib`, `libSC.dylib` and
  `libGCNASM.dylib`. That is AMD's AMDIL/SC path, **not** the AIR native
  translator (`libAMDNTPlugin.dylib`), which was never mapped.
- **On this host the current stack compiles for `gfx900`.** The binary archive
  our program serialized holds an `amdgpu_gfx900` GPU Mach-O. NootedRed
  presents the GPU as `AMDRadeonX5000_AMDVega10GraphicsAccelerator` (Vega 10),
  and the desktop runs on that gfx900 code.
- The compiler service is an Apple platform binary with no
  `disable-library-validation` entitlement, and SIP reports enabled. By Apple's
  documented library-validation rules it loads Apple-signed code only, so **an
  independent backend cannot be placed where AMD's is** without changing that
  policy. This is a policy gate, not a missing interface, and it is the same
  gate the admission study found for the driver bundle itself. It has not been
  tested, because testing it means installing code.

## Experiment record

2026-10-22, macOS 26.4.1 (25E253), x86_64, the known-good boot with the
existing NootedRed + Apple AMD stack (`boot-args` `-NRedRBPlus`). Ignored
`out/metal-runtime/` keeps every command's arguments, output and exit status
(the capture wrapper of the [shader target study](shader-target.md#reproduction)),
the probe binary, its JSON reports, the 146 `vmmap` samples and the serialized
archive.

**The probe** is [`tools/metal_compile_probe.m`](../tools/metal_compile_probe.m).
It takes the default device and compiles a `store_index` kernel with
`newLibraryWithSource:options:error:`. It then creates its pipeline with
`newComputePipelineStateWithFunction:error:` and, with `--archive PATH`,
serializes an `MTLBinaryArchive` containing it. It records the images dyld has
loaded into our own process after each step. `--pause N` keeps it alive
afterwards, and `--big N` adds N dependent `sin` steps, so the native compile
lasts long enough to watch. A per-run UUID salts a constant in the code.

```sh
xcrun clang -fobjc-arc -Wall -Wextra -Werror -framework Foundation -framework Metal \
  tools/metal_compile_probe.m -o out/metal-runtime/metal-compile-probe
out/metal-runtime/metal-compile-probe --archive "$PWD/out/metal-runtime/archive1.bin"
log show --start "<run start>" --predicate 'process == "MTLCompilerService" OR process == "metal-compile-probe"' --style compact
out/metal-runtime/metal-compile-probe --pause 20 &   # then, while it waits:
vmmap -w <its pid>; vmmap -w "$(pgrep -P 1 -n -f MTLCompilerService.xpc)"
out/metal-runtime/metal-compile-probe --big 20000 &  # sample the service's vmmap until it exits
codesign -d --entitlements - --xml .../MTLCompilerService.xpc; codesign -dvvv .../MTLCompilerService.xpc
csrutil status; nvram boot-args
```

`vmmap` on our own process and on the same-user compiler service worked
without privileges. `lsof` shows only files mapped from outside the dyld shared
cache, so it saw neither the AMD images nor most of the compiler.

## Observations

| Question | Observation |
| --- | --- |
| Device | `GFX9_MtlDevice`, "AMD Radeon RX Renoir Graphics", `supportsFamily:Mac2`. The registry node is `AMDRadeonX5000_AMDVega10GraphicsAccelerator` with `MetalPluginName` `AMDRadeonX5000MTLDriver` and `MetalPluginClassName` `AMDMTLGFX9Device`. It has no compiler-specific property |
| What the app loads | Creating the device adds one image, `AMDRadeonX5000MTLDriver.bundle/Contents/MacOS/AMDRadeonX5000MTLDriver`. Compiling the library, creating the pipeline and serializing the archive add **none**. `vmmap` of the paused probe shows no LLVM and no AMD compiler, only `libGPUCompilerUtils` and `libllvm-flatbuffers` from `GPUCompiler.framework` |
| Who compiles | The log shows a new `MTLCompilerService` per client (its `Info.plist` sets `_MultipleInstances`), started on the first compile, with an XPC peer named for our PID. It receives **`MTLBuildLibraryFromSourceToArchive`** (MSL → AIR, about 12–14 ms) and **`MTLBuildFunctions - pipeline`** (about 15 ms), and logs "MTLCompiler upgrade pass forced to use air version 2.7" for the pipeline. The service exits when the client disconnects |
| What the service always has | `MTLCompiler.framework`, `GPUCompiler.framework` (`libGPUCompiler`, `libGPUCompilerImpl`, `libLLVM`, …), `GPUWrangler`, `GPURawCounter`, Metal's `.rtlib` runtime libraries |
| What it adds for AMD | With `--big 20000` the pipeline compile took **129 s**. 144 of 146 `vmmap` samples of the service, taken while the probe was alive, show `AMDRadeonX5000Shared.bundle/Contents/MacOS/AMDRadeonX5000Shared`, `AMDShared.bundle/Contents/PlugIns/libAMDIL902.dylib`, `libSC.dylib` and `libGCNASM.dylib`. A `vmmap` taken after a short compile had finished (run 3) shows none, so they are present only while compiling. **No sample shows `libAMDNTPlugin.dylib`**, although it is the one AMD plugin present on disk |
| Caching | A second run whose UUID changed only a comment sent the service just the source request, with no pipeline request: the native result is reused by AIR, not by source text |
| Pipeline | `GFX9_MtlComputePipelineState`, `threadExecutionWidth` 64, `maxTotalThreadsPerThreadgroup` 1024 |
| Serialized archive | A fat file (`0xcbfebabe`, 2 slices). Slice 1: CPU type `0x01000017` subtype 12, a "MetalLib executable", our AIR. Slice 2: CPU type `0x01000014` subtype 5000, `file` says "Mach-O 64-bit GPU executable amdgpu_gfx900", file type 13, 12 load commands: an `__AIR_DATA` segment, eight `LC_NOTE`s, a `__TEXT` segment with `__reflection` (288 bytes), `__compute` (1,728), `__descriptor` (416) and `__metallib` (4,464), then `LC_BUILD_VERSION` and `LC_UUID`. The section contents were not decoded |
| Service signature | `com.apple.MTLCompilerService`, Apple "Software Signing", `Platform identifier=26`, no Team ID; entitlements: only `com.apple.developer.hardened-process` |
| Host policy | `csrutil status`: enabled. `boot-args`: `-NRedRBPlus` only |

On-disk AMD bundles (names only): `AMDRadeonX5000MTLDriver.bundle` holds an
`Info.plist` and statistics, MPS and Tensile resource files. Its executable,
like those of the other bundles, is in the shared cache.
`AMDRadeonX5000Shared.bundle` ("AMD Radeon X5000 Shared IL") and
`AMDShared.bundle` ("AMD Shared IL") hold `runtime.amdil.bc` and `.builtin`
runtime libraries. `AMDShared.bundle/Contents/PlugIns/libAMDNTPlugin.dylib` is
the only plugin file on disk; `libAMDIL902`, `libSC` and `libGCNASM` are
mapped from the cache.

## The path, as observed

```text
app process                                   MTLCompilerService (one per app, Apple platform binary)
  Metal.framework                               MTLCompiler + GPUCompiler (Apple LLVM)
  AMDRadeonX5000MTLDriver (GFX9_MtlDevice)
newLibraryWithSource:  ── XPC ──────────────►  MSL ──► AIR                       (vendor-neutral)
newComputePipelineState ── XPC ─────────────►  AIR ──► "upgrade" to AIR 2.7
                                                    ──► AMDRadeonX5000Shared  ┐ loaded for the
                                                        libAMDIL902, libSC,    │ compile, then
                                                        libGCNASM             ┘ gone
                        ◄──────────────────── GPU Mach-O amdgpu_gfx900 (__compute, __reflection, __descriptor)
GFX9_MtlComputePipelineState
```

Which request field tells the service to load AMD's bundles is not visible
from outside. The likeliest carrier is the vendor device object in the app,
since the registry has no compiler property, but this method cannot show it.

## What this means for an independent driver

1. **The vendor-neutral half is reusable as is.** MSL → AIR happens in Apple's
   service before any vendor code is involved. Our backend can consume that AIR,
   which the [lowering prototype](metal-frontend.md#air-lowering-prototype)
   already does offline.
2. **The vendor half's slot is closed to third parties on a normal system.** AMD's
   backend runs inside an Apple platform process that only loads Apple-signed
   code. A third-party compiler plugin would need that process to accept a
   non-Apple library, which Apple's [library
   validation](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.security.cs.disable-library-validation)
   rules do not allow without an entitlement the service does not have. This
   is a documented-policy inference; no plugin was installed to test it.
3. **The same policy gates the driver bundle in every app.** The admission study
   found that hardened apps load Apple-signed or same-team code. An independent
   `MTLDevice` subclass has to load into apps before it could do anything,
   including compiling AIR in the app's own process, which Metal might or might
   not allow for a device it did not ship (not observable here).
4. **gfx900 code runs on this GPU today.** The desktop is composited with gfx900
   code from AMD's compiler. This is practical evidence that gfx900-targeted
   code executes on Cezanne for what that compiler emits. It is not proof that
   all gfx900 code does. The [shader target study](shader-target.md) found
   LLVM's `gfx900` and `gfx90c` feature sets equivalent except for the ELF
   machine value and XNACK defaults.
5. **"Faithful" has a cost here.** The path Apple's stack uses (a vendor driver
   bundle in the app plus a vendor compiler in Apple's service) is open only to
   Apple-signed code. Reproducing it with our own code on this host means one
   of these:
   - relaxing code-signing enforcement (SIP/AMFI settings) on this host;
   - keeping Apple's AMD user-space, which AGENTS.md excludes from the
     finished stack;
   - finding a sanctioned extension point that this observation-only study
     did not see.

   **Choosing among them is the user's decision**, and this study does not make
   it.

## Limits

- One host, one boot, one build; the existing stack is NootedRed's
  presentation of Cezanne as Vega 10, not Apple-supported hardware.
- `vmmap` samples show images mapped at sample times, not calls. "Loaded only
  while compiling" rests on samples during a 129 s compile and one after a
  short one, not on observing the unload.
- The request format between app and service, how the backend is selected and
  what `__compute`, `__reflection` and `__descriptor` contain are unknown by
  this method.
- Library validation's effect on a third-party plugin is inferred from
  Apple's documentation and the observed entitlements, not tested.

## Next experiments (each needs the user's decision)

- **Within observation only:** compare a graphics pipeline (vertex and
  fragment), to see whether the same AMD images are used and which sections
  the archive holds. Also run a precise-math kernel and an AIR-2.7-only
  feature, to see the "upgrade pass" behave.
- **Policy decision:** whether the project may relax code-signing enforcement
  on this host, in an experimental environment, so that our own driver bundle
  and backend can be loaded at all. Without that, the next Metal milestone
  ("own Metal discovery") cannot be attempted.
