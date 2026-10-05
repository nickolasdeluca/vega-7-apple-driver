# Cezanne shader compiler target

This study chooses the LLVM processor name for the Cezanne graphics core and
compiles one fixed kernel offline to record the emitted ISA and code-object
metadata. **Decision: `gfx90c`, with XNACK left as "any", SRAM ECC unsupported
and wave64 only; `gfx9-generic` (code object v6) is the fallback.** The decision
rests on Linux's compute-driver mapping for GC 9.3.0 and on identical LLVM
feature sets for the competing names. It is not a measurement: the host's GC
version has not been read, and nothing was loaded, dispatched or run on the GPU.
Compiling a kernel does not show that the hardware executes it, and an LLVM
target does not supply a Metal backend.

This closes the "compiler target mapping" row of the
[target manifest](cezanne-target-manifest.md) and the compiler half of the
offline shader boundary study in the [graphics contract](graphics-contract.md).

## Experiment record

Offline study on 2026-10-05. The host was not queried; its identity remains PCI
`1002:1638:c9` from the [baseline](hardware-baseline.md), macOS 26.4.1 build
25E253, x86_64.

| Item | Value |
| --- | --- |
| Compiler | Official [LLVM 20.1.7](https://github.com/llvm/llvm-project/releases/tag/llvmorg-20.1.7) `LLVM-20.1.7-macOS-X64.tar.xz`, unpacked under ignored `out/`; nothing installed system-wide |
| Why 20.1.7 | The newest official x86_64 macOS archive. LLVM 23.1.2 ships only a macOS ARM64 archive; the user chose the prebuilt 20.1.7 over a source build of 23.1.2 |
| Archive identity | 1,464,445,324 bytes, SHA-256 `ccf82ffe7e136ee49659cb57157856a7963d0950fac3d05aabba0db75bfba26f`, equal to the GitHub release asset digest |
| Provenance | `gh attestation verify --repo llvm/llvm-project` against the release's `.jsonl` bundle (offline) passes: SLSA v1 provenance, workflow `release-tasks.yml` at `refs/tags/llvmorg-20.1.7`, GitHub-hosted runner, source commit `6146a88f60492b520a36f8f8f3231e15f3cc6082`, which is the tag's commit. The release has no GPG `.sig` for this asset |
| Archive safety | 10,014 members under one top-level directory; no absolute or `..` paths. The binaries are not code-signed and carry no quarantine attribute |
| Pinned sources | LLVM `GCNProcessors.td`, `AMDGPU.td`, `AMDGPUUsage.rst` and `ELF.h` at the 20.1.7 commit, plus the earlier 23.1.2 copies; Linux [v6.12](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/amd/amdkfd/kfd_device.c) `kfd_device.c`; Mesa `mesa-26.2.4` `amd_family.c`, `ac_llvm_util.c`, `ac_gpu_info.c` |

Ignored `out/shader-target/` keeps the archive, attestation, unpacked toolchain,
sources, compile outputs and every command's arguments, UTC time, stdout/stderr
and exit status. Preserved failures:
- Passing a target ID to `llc` (`-mcpu=gfx90c:xnack+`, `xnack-`, `sramecc+`)
  printed "not a recognized processor … (ignoring processor)" and aborted with
  signal 6. `llc` takes features through `-mattr`; target IDs are a clang form.
- The first clang target-ID runs stopped at "cannot find ROCm device library";
  the documented commands pass `-nogpulib`.
- `llvm-objdump --disassemble-symbols=store_index.kd` warned that the symbol is
  missing from the linked object and printed nothing. The checker below decodes
  the descriptor from `.rodata` instead.
- The first checker drafts expected 14 SGPRs for every variant and read the
  kernel descriptor at wrong offsets. Both were corrected against the outputs and
  the pinned descriptor table.

## Mapping evidence

Linux v6.12 classifies `1638` as Green Sardine under `CHIP_RENOIR`, served by
the GC 9.3.0 graphics block ([target manifest](cezanne-target-manifest.md)).
Renoir and Green Sardine therefore share one GC version, and the question is
which LLVM processor stands for GC 9.3.0.

| Source | Says | Weight |
| --- | --- | --- |
| Linux v6.12 `kfd_device.c` 324–326 | GC `IP_VERSION(9, 3, 0)` (commented "Renoir") sets `gfx_target_version = 90012` | **For `gfx90c`.** Read as major 9, minor 0, stepping 12; stepping 12 is hex `c`. The string form `gfx90c` is the ROCm naming convention, not text in this file |
| LLVM `AMDGPU.td` | A dedicated `FeatureISAVersion9_0_C` exists; `GCNProcessors.td` 191 binds `gfx90c` to it | Consistent with ISA version 9.0.12 |
| LLVM `AMDGPUUsage.rst` 367 (20.1.7) | `gfx90c`: APU, `xnack`, absolute flat scratch, runtime `pal-amdpal`, Ryzen 4000G products | Product names are Renoir parts, which share GC 9.3.0; product names alone are not identity evidence |
| Mesa 26.2.4 `amd_family.c` 145–147 | `CHIP_RAVEN2` and `CHIP_RENOIR` both return `"gfx909"`; `ac_llvm_util.c` 92–95 passes that name to LLVM | **Against `gfx90c` as the only name.** Mesa's graphics driver compiles Renoir-family code as `gfx909` |
| LLVM `AMDGPUUsage.rst` 353 | `gfx909`: APU, `xnack`, `pal-amdpal`, products "TBA" | No product claims either way |

The disagreement does not change code. Expanding each name's `FeatureSet` in
`AMDGPU.td` gives the same resolved list for `gfx902`, `gfx909` and `gfx90c` in
both 20.1.7 and 23.1.2, and all three use `SIQuarterSpeedModel`. 20.1.7 also
lists `FeatureImageInsts` on `gfx909` explicitly, but the shared base already
contains it. `gfx9-generic` drops `FeatureMadMixInsts` and adds
`FeatureRequiresCOV6`. (This expansion follows `FeatureSet` concatenation only,
not `SubtargetFeature` implications.)

The names do change the ELF machine value, which a loader matches:

| Target | `EF_AMDGPU_MACH` | Note |
| --- | --- | --- |
| `gfx902` | `0x02d` | Raven |
| `gfx909` | `0x031` | Mesa's Renoir name |
| `gfx90c` | `0x032` | KFD's Renoir/GC 9.3.0 version |
| `gfx9-generic` | `0x051` | Needs code object v6; ELF also carries generic version 1 |

Our own driver will be the loader, so which machine values it accepts is a
project contract, not an external requirement. Selecting `gfx90c` matches the
Linux compute stack's identity for GC 9.3.0 and LLVM's dedicated ISA version.

### Target features

From `AMDGPUUsage.rst` (20.1.7) and the compiles below:

- **XNACK** (751): if a target ID omits `xnack`, code object v4+ code runs with
  either XNACK replay setting. Default output is "any" (`0x100`); `xnack+` and
  `xnack-` set `0x300` and `0x200`. With `xnack-` the kernel no longer reserves the
  XNACK mask, so this kernel needs 10 SGPRs instead of 14. Keep "any" until the
  driver chooses an XNACK replay policy.
- **SRAM ECC**: unsupported on `gfx90c`. Clang rejects `gfx90c:sramecc+` as an
  invalid target ID, and `llc -mattr=+sramecc` leaves `e_flags` unchanged
  (SRAM ECC bits `0x000`).
- **Wave size**: GFX9 is wave64. Requesting `-mattr=+wavefrontsize32` exits 0 but
  emits **no kernel** (`amdhsa.kernels: []`, no code). A build must check that its
  kernels exist, not just the exit status.
- **`v_mad_mix`**: the `gfx9-generic` row says these instructions are unavailable
  on `gfx900`, `gfx902`, `gfx909` and `gfx90c`. The 20.1.7 assembler accepts
  `v_mad_mix_f32` on all four with one encoding and rejects it only on
  `gfx9-generic`. The row's wording does not match the assembler; the practical
  result is that generic code avoids the instruction.

## Compile results

The fixed kernel stores each work-item's x index at `out[index]`:

<!-- file: compile/store_index.ll -->
```llvm
; Fixed study kernel: each work-item stores its x index into out[index].
define amdgpu_kernel void @store_index(ptr addrspace(1) %out) {
  %id = call i32 @llvm.amdgcn.workitem.id.x()
  %slot = getelementptr i32, ptr addrspace(1) %out, i32 %id
  store i32 %id, ptr addrspace(1) %slot, align 4
  ret void
}
declare i32 @llvm.amdgcn.workitem.id.x()
```

For `amdgcn-amd-amdhsa` and `-mcpu=gfx90c` (code object v5 by default) LLVM
emits 28 bytes of code:

```text
s_load_dwordx2 s[0:1], s[8:9], 0x0     // C0060004 00000000
v_lshlrev_b32_e32 v1, 2, v0            // 24020082
s_waitcnt lgkmcnt(0)                   // BF8CC07F
global_store_dword v1, v0, s[0:1]      // DC708000 00000001
s_endpgm                               // BF810000
```

| Variant | `e_flags` | ABI version | SGPRs | Code and descriptor |
| --- | --- | --- | --- | --- |
| `gfx90c` | `0x132` (`xnack` any) | 3 (v5) | 14 | Reference |
| `gfx909` | `0x131` | 3 | 14 | Identical bytes |
| `gfx902` | `0x12d` | 3 | 14 | Identical bytes |
| `gfx90c`, code object v6 | `0x132` | 4 (v6) | 14 | Identical bytes |
| `gfx9-generic`, code object v6 | `0x1000151` (generic v1) | 4 | 14 | Identical bytes |
| `gfx9-generic`, code object v5 | — | — | — | Rejected: "only available on code object version 6 or better" |
| `gfx90c -mattr=+xnack` / clang `gfx90c:xnack+` | `0x332` | 3 | 14 | Identical bytes |
| `gfx90c -mattr=-xnack` / clang `gfx90c:xnack-` | `0x232` | 3 | 10 | Identical bytes |
| `gfx90c -mattr=+sramecc` | `0x132` | 3 | 14 | Identical bytes; request ignored |

All HSA objects are `EM_AMDGPU` (`0xe0`) with OS/ABI 64 (HSA). The 64-byte
kernel descriptor, decoded against the "Code Object V3 Kernel Descriptor" table
(`AMDGPUUsage.rst` 4889), is the same in every variant:

| Field | Value | Meaning |
| --- | --- | --- |
| Group / private segment size | 0 / 0 | No LDS, no scratch |
| Kernarg size | 264 | 8-byte `out` plus hidden arguments |
| Code entry offset | `0x10c0` | From the descriptor to `store_index` |
| `COMPUTE_PGM_RSRC1` | `0x00af0040` | VGPR granule 0 (4 VGPRs), SGPR granule 1 (16), FP32 and FP16/64 denormals on, DX10 clamp, IEEE mode |
| `COMPUTE_PGM_RSRC2` | `0x00001398` | No scratch, 12 user SGPRs, workgroup IDs x/y/z, work-item IDs up to z |
| `COMPUTE_PGM_RSRC3` | 0 | |
| Kernel code properties | `0x001f` | Private segment buffer, dispatch pointer, queue pointer, kernarg pointer, dispatch ID |

The metadata note (`NT_AMDGPU_METADATA`) records `amdhsa.target`
`amdgcn-amd-amdhsa--gfx90c`, `.wavefront_size: 64`, 2 VGPRs, 14 SGPRs,
`.kernarg_segment_size: 264`, the explicit `out` global buffer and 19 hidden
arguments in bytes 8–215, ending with the queue pointer at 208.

The PAL form, `amdgcn-amd-amdpal`, uses an `amdgpu_cs` entry point and produces
an object with OS/ABI 65 (PAL), the same `e_flags` `0x132`, an
`NT_AMD_HSA_ISA_NAME` note `amdgcn-amd-amdpal--gfx90c` and `amdpal.pipelines`
metadata (`.cs` with 6 SGPRs, 4 VGPRs and two raw register values). The ISA
differs because the PAL kernel addresses its buffer with 64-bit vector
arithmetic. The register keys are recorded, not interpreted.

## Reproduction

Run from the repository root. Download and verify the toolchain into a new
ignored directory; nothing is installed outside it:

```sh
export STUDY_ROOT=out/shader-target-new
mkdir -p "$STUDY_ROOT/llvm" "$STUDY_ROOT/compile"
U=https://github.com/llvm/llvm-project/releases/download/llvmorg-20.1.7
A=$STUDY_ROOT/llvm/LLVM-20.1.7-macOS-X64.tar.xz
curl -fL --retry 3 -sS -o "$A" "$U/LLVM-20.1.7-macOS-X64.tar.xz"
curl -fL -sS -o "$A.jsonl" "$U/LLVM-20.1.7-macOS-X64.tar.xz.jsonl"
shasum -a 256 "$A"   # ccf82ffe7e136ee49659cb57157856a7963d0950fac3d05aabba0db75bfba26f
gh attestation verify --repo llvm/llvm-project "$A" --bundle "$A.jsonl"
tar -tJf "$A" | grep -cE '(^/|(^|/)\.\.(/|$))'   # expect 0
tar -xJf "$A" -C "$STUDY_ROOT/llvm"
export LLVM_BIN=$STUDY_ROOT/llvm/LLVM-20.1.7-macOS-X64/bin
```

Save the blocks below under `$STUDY_ROOT` at the path shown above each one: the
`store_index.ll` block above goes to `compile/store_index.ll`. The capture
wrapper uses the repository's `tools/baseline.py`:

<!-- file: capture.py -->
```python
import datetime,importlib.util,json,os,sys
from pathlib import Path
spec=importlib.util.spec_from_file_location('baseline','tools/baseline.py')
baseline=importlib.util.module_from_spec(spec);spec.loader.exec_module(baseline)
root=Path(os.environ['STUDY_ROOT'])
name=sys.argv[1]
result=baseline.capture(name,sys.argv[2:],root,timeout=int(os.environ.get('CAPTURE_TIMEOUT','120')))
result['captured_at_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat()
(root/(name+'.capture.json')).write_text(json.dumps(result,indent=2)+'\n')
print(name,result['status'],result['exit_code'])
```

<!-- file: compile/store_index_pal.ll -->
```llvm
; Same work for the PAL ABI: compute shader writes its x index through a fixed buffer pointer.
define amdgpu_cs void @store_index_cs(ptr addrspace(1) inreg %out, <3 x i32> %id) {
  %x = extractelement <3 x i32> %id, i32 0
  %slot = getelementptr i32, ptr addrspace(1) %out, i32 %x
  store i32 %x, ptr addrspace(1) %slot, align 4
  ret void
}
```

<!-- file: compile/mix.s -->
```asm
v_mad_mix_f32 v0, v1, v2, v3
```

<!-- file: compile/run.sh -->
```sh
#!/bin/sh
# Offline compile study. Writes files under $STUDY_ROOT only; nothing is loaded or dispatched.
set -u
: "${STUDY_ROOT:?}" "${LLVM_BIN:?}"
B=$LLVM_BIN
W=$STUDY_ROOT/compile
C="python3 $STUDY_ROOT/capture.py"
hsa() { # name cpu [extra llc args]
  n=$1; cpu=$2; shift 2
  $C cc-$n-llc $B/llc -mtriple=amdgcn-amd-amdhsa -mcpu=$cpu -filetype=obj "$@" -o $W/$n.o $W/store_index.ll
  [ -s $W/$n.o ] || return 0
  $C cc-$n-asm $B/llc -mtriple=amdgcn-amd-amdhsa -mcpu=$cpu "$@" -o $W/$n.s $W/store_index.ll
  $C cc-$n-link $B/ld.lld -shared -o $W/$n.hsaco $W/$n.o
  $C cc-$n-header $B/llvm-readelf -h $W/$n.hsaco
  $C cc-$n-notes $B/llvm-readelf --notes $W/$n.hsaco
  $C cc-$n-disasm $B/llvm-objdump -d --no-show-raw-insn --mcpu=$cpu $W/$n.hsaco
}
for cpu in gfx90c gfx909 gfx902; do hsa $cpu $cpu; done
hsa gfx90c-wave32 gfx90c -mattr=+wavefrontsize32
hsa gfx9-generic-cov5 gfx9-generic
hsa gfx9-generic-cov6 gfx9-generic --amdhsa-code-object-version=6
hsa gfx90c-cov6 gfx90c --amdhsa-code-object-version=6
# llc has no target-ID syntax in -mcpu; use -mattr, then clang's target-ID form.
hsa gfx90c-mattr-xnack-on gfx90c -mattr=+xnack
hsa gfx90c-mattr-xnack-off gfx90c -mattr=-xnack
hsa gfx90c-mattr-sramecc-on gfx90c -mattr=+sramecc
for id in 'gfx90c:xnack+' 'gfx90c:xnack-' 'gfx90c:sramecc+'; do
  # -nogpulib: without it clang requires ROCm device libraries.
  n=clang-$(echo "$id" | tr ':+-' '_pm')
  $C cc-$n $B/clang -target amdgcn-amd-amdhsa -mcpu=$id -nogpulib -c -o $W/$n.o $W/store_index.ll
  [ -s $W/$n.o ] && $C cc-$n-header $B/llvm-readelf -h $W/$n.o
done
$C cc-pal-gfx90c-llc $B/llc -mtriple=amdgcn-amd-amdpal -mcpu=gfx90c -filetype=obj -o $W/pal-gfx90c.o $W/store_index_pal.ll
$C cc-pal-gfx90c-header $B/llvm-readelf -h $W/pal-gfx90c.o
$C cc-pal-gfx90c-notes $B/llvm-readelf --notes $W/pal-gfx90c.o
$C cc-pal-gfx90c-disasm $B/llvm-objdump -d --no-show-raw-insn --mcpu=gfx90c $W/pal-gfx90c.o
for cpu in gfx90c gfx909 gfx902 gfx9-generic gfx900; do
  $C mc-mix-$cpu $B/llvm-mc -triple=amdgcn-amd-amdhsa -mcpu=$cpu -show-encoding $W/mix.s
done
```

The checker reads only saved files and exits nonzero on any departure from the
results above or a missing capture:

<!-- file: compile/check-compile.py -->
```python
# Verifies the saved offline compile study. Reads files under ROOT only;
# executes nothing. Usage: check-compile.py ROOT   (ROOT = out/shader-target)
import json,re,struct,sys
from pathlib import Path
root=Path(sys.argv[1]); work=root/'compile'

def capture(name):
    meta=json.loads((root/(name+'.capture.json')).read_text())
    return meta['exit_code'],(root/(name+'.stdout')).read_text(),(root/(name+'.stderr')).read_text()

def elf(path):
    d=path.read_bytes()
    assert d[:4]==b'\x7fELF' and d[4]==2 and d[5]==1, 'not little-endian ELF64: %s'%path
    shoff,=struct.unpack_from('<Q',d,0x28); size,count,names_index=struct.unpack_from('<HHH',d,0x3a)
    rows=[struct.unpack_from('<IIQQQQIIQQ',d,shoff+i*size) for i in range(count)]
    table=rows[names_index]; names=d[table[4]:table[4]+table[5]]
    sections={}
    for name,kind,_,_,offset,length,*_ in rows:
        key=names[name:names.index(b'\0',name)].decode()
        sections[key]=b'' if kind==8 else d[offset:offset+length]
    return {'osabi':d[7],'abi_version':d[8],'machine':struct.unpack_from('<H',d,18)[0],
            'flags':struct.unpack_from('<I',d,0x30)[0],'sections':sections}

def descriptor(kd):
    # AMDGPUUsage "Code Object V3 Kernel Descriptor" layout (64 bytes).
    assert len(kd)==64, 'kernel descriptor is %d bytes'%len(kd)
    group,private,kernarg=struct.unpack_from('<III',kd,0)
    # Bytes: 12-15 and 24-43 reserved, 44 RSRC3, 48 RSRC1, 52 RSRC2, 56 code
    # properties (u16), 58 kernarg preload (u16), 60-63 reserved (src20 AMDGPUUsage.rst).
    rsrc3,rsrc1,rsrc2,properties,preload=struct.unpack_from('<IIIHH',kd,44)
    assert kd[12:16]==bytes(4) and kd[24:44]==bytes(20) and kd[60:]==bytes(4), 'reserved descriptor bytes set'
    return {'group_segment_fixed_size':group,'private_segment_fixed_size':private,'kernarg_size':kernarg,
            'kernel_code_entry_byte_offset':struct.unpack_from('<q',kd,16)[0],
            'compute_pgm_rsrc1':rsrc1,'compute_pgm_rsrc2':rsrc2,'compute_pgm_rsrc3':rsrc3,
            'kernel_code_properties':properties,'kernarg_preload':preload}

EM_AMDGPU,OSABI_HSA,OSABI_PAL=0xE0,64,65
# variant -> (e_flags, ABI version byte, amdhsa.target)
EXPECTED={
    'gfx90c':(0x132,3,'amdgcn-amd-amdhsa--gfx90c'),
    'gfx909':(0x131,3,'amdgcn-amd-amdhsa--gfx909'),
    'gfx902':(0x12D,3,'amdgcn-amd-amdhsa--gfx902'),
    'gfx90c-cov6':(0x132,4,'amdgcn-amd-amdhsa--gfx90c'),
    'gfx9-generic-cov6':(0x1000151,4,'amdgcn-amd-amdhsa--gfx9-generic'),
    'gfx90c-mattr-xnack-on':(0x332,3,"'amdgcn-amd-amdhsa--gfx90c:xnack+'"),
    'gfx90c-mattr-xnack-off':(0x232,3,"'amdgcn-amd-amdhsa--gfx90c:xnack-'"),
    'gfx90c-mattr-sramecc-on':(0x132,3,'amdgcn-amd-amdhsa--gfx90c')}
ISA=['s_load_dwordx2 s[0:1], s[8:9], 0x0','v_lshlrev_b32_e32 v1, 2, v0','s_waitcnt lgkmcnt(0)',
     'global_store_dword v1, v0, s[0:1]','s_endpgm']
summary={'variants':{}}
reference=None
for name,(flags,abi,target) in EXPECTED.items():
    for step in ('llc','asm','link','header','notes','disasm'):
        code,_,_=capture('cc-%s-%s'%(name,step))
        assert code==0, '%s %s exited %d'%(name,step,code)
    image=elf(work/(name+'.hsaco'))
    assert image['machine']==EM_AMDGPU and image['osabi']==OSABI_HSA, name
    assert image['flags']==flags, '%s e_flags %#x != %#x'%(name,image['flags'],flags)
    assert image['abi_version']==abi, '%s ABI version %d != %d'%(name,image['abi_version'],abi)
    _,notes,_=capture('cc-%s-notes'%name)
    assert re.search(r'^amdhsa\.target:\s+%s$'%re.escape(target),notes,re.M), name+' target note'
    # xnack-off drops the xnack mask reservation; 10 and 14 share one 16-SGPR granule.
    sgprs=10 if name=='gfx90c-mattr-xnack-off' else 14
    for field in ('.name:           store_index','.wavefront_size: 64','.vgpr_count:     2',
                  '.sgpr_count:     %d'%sgprs,'.kernarg_segment_size: 264','.private_segment_fixed_size: 0'):
        assert notes.count(field)==1, '%s metadata lacks %r'%(name,field)
    _,listing,_=capture('cc-%s-disasm'%name)
    body=listing.split('<store_index>:',1)[1]
    assert [l.split('//')[0].strip() for l in body.splitlines() if l.strip()]==ISA, name+' ISA'
    kd=descriptor(image['sections']['.rodata'])
    record={'e_flags':hex(flags),'abi_version':abi,'sgpr_count':sgprs,'text_bytes':len(image['sections']['.text']),'descriptor':kd}
    if reference is None: reference=(image['sections']['.text'],image['sections']['.rodata'])
    assert (image['sections']['.text'],image['sections']['.rodata'])==reference, name+' code or descriptor differs from gfx90c'
    summary['variants'][name]=record
kd=summary['variants']['gfx90c']['descriptor']
assert kd['kernarg_size']==264 and kd['private_segment_fixed_size']==0 and kd['group_segment_fixed_size']==0
assert kd['compute_pgm_rsrc3']==0 and kd['kernarg_preload']==0 and kd['kernel_code_entry_byte_offset']==0x10C0
assert kd['compute_pgm_rsrc1']==0x00AF0040 and kd['compute_pgm_rsrc2']==0x1398 and kd['kernel_code_properties']==0x1F
rsrc2=kd['compute_pgm_rsrc2']
assert (rsrc2>>1)&0x1F==12 and (rsrc2>>7)&7==7 and (rsrc2>>11)&3==2 and rsrc2&1==0, 'rsrc2 fields'

# Clang target IDs agree with llc -mattr; sramecc is not a gfx90c target-ID feature.
for name,flags in (('clang-gfx90c_xnackp',0x332),('clang-gfx90c_xnackm',0x232)):
    assert capture('cc-'+name)[0]==0 and elf(work/(name+'.o'))['flags']==flags, name
code,_,err=capture('cc-clang-gfx90c_srameccp')
assert code==1 and "invalid target ID 'gfx90c:sramecc+'" in err
# Rejections and silent outcomes the study must keep observing.
code,_,err=capture('cc-gfx9-generic-cov5-llc')
assert code==1 and 'gfx9-generic is only available on code object version 6 or better' in err
_,wave32,_=capture('cc-gfx90c-wave32-notes')
assert 'amdhsa.kernels:  []' in wave32 and '.name:' not in wave32, 'wave32 request emitted a kernel'
for cpu in ('gfx90c','gfx909','gfx902','gfx900'):
    code,out,_=capture('mc-mix-'+cpu)
    assert code==0 and '[0x00,0x00,0xa0,0xd3,0x01,0x05,0x0e,0x04]' in out, cpu+' v_mad_mix_f32'
code,_,err=capture('mc-mix-gfx9-generic')
assert code==1 and 'instruction not supported on this GPU' in err
# PAL: same target, PAL OS/ABI, ISA-name note and pipeline metadata.
pal=elf(work/'pal-gfx90c.o'); _,notes,_=capture('cc-pal-gfx90c-notes')
assert pal['osabi']==OSABI_PAL and pal['flags']==0x132 and 'amdgcn-amd-amdpal--gfx90c' in notes
assert '.entry_point_symbol: store_index_cs' in notes and '.vgpr_count:     4' in notes
summary['rejections']=['gfx9-generic code object v5','gfx90c:sramecc+ target ID','v_mad_mix_f32 on gfx9-generic']
summary['silent']=['wave32 on gfx90c emits no kernel','-mattr=+sramecc leaves e_flags unchanged']
print(json.dumps(summary,indent=1,sort_keys=True))
```

```sh
sh "$STUDY_ROOT/compile/run.sh"
python3 "$STUDY_ROOT/compile/check-compile.py" "$STUDY_ROOT"
```

`run.sh` reports three expected failures: `gfx9-generic` with code object v5,
the `gfx90c:sramecc+` clang target ID and `v_mad_mix_f32` on `gfx9-generic`. The
checker exits 0 only if every result above holds.

## Verification record

- Archive digest equals the GitHub asset digest; the offline attestation check
  passes and names the tag's commit.
- The checker passes on the original evidence. A mutation check copied the
  evidence and applied 15 single defects: wrong `e_flags`, ABI version, code
  byte, `COMPUTE_PGM_RSRC2`, reserved descriptor byte, wave size note, SGPR
  count, ISA line, failed and missing link captures, a wave32 kernel appearing,
  and the three expected rejections or the PAL OS/ABI changing. The checker
  rejected all 15; the unmodified copy passed.
- The capture wrapper, scripts and kernels above were extracted from this
  document into a fresh directory, `out/shader-target-reproduced/`, and rerun.
  The 1.4 GB download was not repeated: the saved archive's digest was checked
  again, and the attestation check exited 0 (it prints nothing without a
  terminal unless given `--format json`). The rerun produced the same three
  expected failures, 21 byte-identical objects and a checker summary identical
  to the original run.

## Remaining questions and gates

- **The host's GC version is unmeasured.** GC 9.3.0 comes from Linux's handling
  of PCI ID `1638`. Reading the IP discovery table needs hardware access and
  belongs to target IP validation in the experimental environment.
- **No execution evidence.** The descriptor and metadata are compiler outputs. A
  CPU-reference comparison on our own queue is stage 4 of the
  [discovery plan](discovery-plan.md).
- **XNACK policy.** The driver must choose XNACK replay on or off and accept
  matching code objects; "any" code runs under either.
- **Metal frontend.** LLVM's AMDGPU target compiles LLVM IR. Whether a permitted
  Metal frontend exposes enough information (for example AIR) for an independent
  backend is the open half of the offline shader boundary study.
- **Version spread.** The compiler is 20.1.7; the 23.1.2 tables were compared and
  agree for these targets, but 23.1.2 output was not produced.
