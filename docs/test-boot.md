# USB test boot

Status, 2026-10-06: **stages 0 to 14 succeeded** (see
[Test boot log](#test-boot-log)). Stage 1 read both boot-state registers with
the expected values. The first stage 0 attempt stalled in OpenCore file
logging, which the test EFI no longer does. Stage 2 read and validated the IP
discovery table from the carveout without writes. Stage 3 read the GC
configuration: 7 of 8 CUs and both RBs active. Stage 4's root-only, read-only
diagnostic interface re-read every register from the running system. Stage 5
read 38 power, clock-gating, engine and memory-hub registers through it.
Stage 6, the first reviewed write, wrote `0xCAFEDEAD` to `SCRATCH_REG0`,
read it back, and restored the original value. Stage 7 sent the first SMU
messages: driver-interface version 14, SMU firmware 64.74.0. One stage 7 boot
attempt reset before reaching macOS, cause unknown. Stage 8 sent
`DisallowGfxOff`: response OK, GFX stayed on. Stage 9 (the SMU metrics
table) stopped at its own check: the chosen carveout region is not all zero,
so no SMU message was sent. Its check needs a better criterion before
another attempt. The [SysReport dump boot](#sysreport-dump-boot) gave the
VBIOS: the firmware reserves no carveout memory. With the
[revised check](#revision-stage-9-free-page-check), stage 9 read the SMU
metrics table in boot 13. Stage 10 (boot 14) found the PSP ready with no
ring, and both memory hubs mapping the carveout identically. Stage 11 (the
first PSP commands: create and destroy a ring) stopped safely in boot 15:
the PSP answered its first command, `GBR_IH_SET`, with "unknown command". A
revised stage 11 succeeded in boot 16: the PSP created (`0x80020000`) and
destroyed (`0x80030000`) a kernel-mode ring without touching its memory.
Stage 12 succeeded in boot 17: the first ring frames, `SETUP_TMR` and
`DESTROY_TMR`, both fenced with status 0. Stage 13 succeeded in boot 18:
the PSP accepted and loaded the SDMA0 firmware (`SDMA0_UCODE_CHECKSUM` 0 →
`0x25a1ba79`) with the engine left halted. Stage 14 succeeded in boot 19:
`PowerUpSdma`/`PowerDownSdma` answered `0x01`, and the 31-register SDMA
inventory is recorded. Stage 15 (the first SDMA copy) is approved and
built, not yet booted. No later stage is authorized.

The host keeps booting from its **known-good** OpenCore EFI on the internal
macOS disk. Driver experiments run only after choosing a separate **test EFI**
on a USB drive from the firmware boot menu. Unplugging the drive and powering
off restores the known-good boot. This is the experimental environment
[AGENTS.md](../AGENTS.md) requires before any driver is loaded.

## Rules

1. Never modify the internal EFI partition. After each test session, confirm
   it still matches the hashes recorded at build time (see
   [Return to the known-good boot](#return-to-the-known-good-boot)).
2. Experimental kexts load only from the test EFI's `Kernel → Add` list. Never
   install them into `/Library/Extensions`, load them with `kmutil` or
   `kextload`, or add them to the known-good EFI. Anything installed on the
   macOS volume would also load in the known-good boot.
3. The driver declines to attach unless the boot arguments contain
   `cezanne-stage=N`, which only the test EFI sets and the known-good EFI
   deletes. It also declines a stage above the one it was built for
   (`kMaxStage` in `driver/core/cezanne_core.h`), and `tools/test_efi.py`
   refuses to build a test EFI for a stage this document does not authorize.
4. Only authorized stages may be built into a test EFI or booted:
   - **Stage 0** (authorized): passive. Attach to the Cezanne PCI device and
     record what the registry already reports.
   - **Stage 1** (authorized by the user on 2026-10-05): read-only device
     access, described [below](#stage-1-read-only-device-access). Boot it only
     after stage 0 has met its success criteria.
   - **Stage 2** (authorized by the user on 2026-10-05): stage 1 plus one more
     register read and a read-only mapping of the IP discovery binary in the
     carveout, described [below](#stage-2-write-free-discovery-table-read).
     Still no register, configuration or memory write.
   - **Stage 3** (authorized by the user on 2026-10-05): stage 2 plus seven
     read-only GC configuration registers, described
     [below](#stage-3-read-only-gc-configuration). Still no write of any kind.
   - **Stage 4** (authorized by the user on 2026-10-05): stage 3 plus a
     root-only diagnostic interface that re-reads stage 3 registers on
     request, described [below](#stage-4-diagnostic-interface). Still no write.
   - **Stage 5** (authorized by the user on 2026-10-05): stage 4 plus 38
     read-only power, clock-gating, engine and memory-hub registers, read only
     through the diagnostic interface, described
     [below](#stage-5-power-clock-and-engine-state). Still no write.
   - **Stage 6** (proposed and approved by the user on 2026-10-05): stage 5
     plus the first register write, a reversible `SCRATCH_REG0` test run only
     on request, described [below](#stage-6-first-reviewed-write).
   - **Stage 7** (proposed and approved by the user on 2026-10-05): stage 6
     plus the first SMU messages, `GetDriverIfVersion` and `GetSmuVersion`, run
     only on request, described [below](#stage-7-first-smu-query).
   - **Stage 8** (proposed and approved by the user on 2026-10-05): stage 7
     plus `DisallowGfxOff`, sent only on request, described
     [below](#stage-8-disallow-gfxoff).
   - **Stage 9** (proposed and approved by the user on 2026-10-05): stage 8
     plus the SMU metrics table, written by the SMU into one checked carveout
     page on request, described [below](#stage-9-smu-metrics-table).
   - **Stage 10** (proposed 2026-10-06 and approved by the user the same
     day): stage 9 plus 21 read-only PSP mailbox and memory-aperture
     registers, read only through the diagnostic interface, described
     [below](#stage-10-psp-and-memory-aperture-state-proposal). No new write.
   - **Stage 11** (proposed 2026-10-06 and approved by the user the same
     day): stage 10 plus the first PSP commands, creating and destroying the
     kernel-mode ring at one checked carveout page on request, described
     [below](#stage-11-create-and-destroy-a-psp-ring-proposal).
   - **Stage 12** (proposed 2026-10-06 and approved by the user the same
     day): stage 11 plus the first CPU writes to carveout memory and the
     first ring frame, `SETUP_TMR` then `DESTROY_TMR`, on request, described
     [below](#stage-12-first-psp-ring-frame-tmr-setup-proposal).
   - **Stage 13** (proposed 2026-10-06 and approved by the user the same
     day): stage 12 plus the first firmware load, the pinned SDMA0 image
     through `LOAD_IP_FW`, on request, with SDMA0 left halted, described
     [below](#stage-13-first-firmware-load-sdma0-proposal).
   - **Stage 14** (proposed 2026-10-06 and approved by the user the same
     day): stage 13 plus `PowerUpSdma`/`PowerDownSdma` and 25 read-only SDMA
     registers, read three times on request, described
     [below](#stage-14-sdma0-power-up-and-register-inventory-proposal).
   - **Stage 15** (proposed 2026-10-06 and approved by the user the same
     day, with the golden `GB_ADDR_CONFIG` applied and the default page left
     for later): stage 14 plus starting SDMA0 with exact register values, a
     ring test and the first 4 KiB copy with a fence, on request, described
     [below](#stage-15-first-sdma-copy-proposal).
   - Each later stage (indexed register reads, any register or configuration
     write, firmware, memory mapping, DMA, interrupts) needs its own reviewed
     update to this document and the user's approval before it is built.
5. After any test boot from stage 1 on, shut down completely and wait a few
   seconds before booting the known-good EFI, so GPU and firmware state cannot
   carry over a warm reboot. Stage 0 touches no hardware. Do not sleep the
   machine during a test boot; the driver does not handle power transitions.
6. Record every test boot: date, stage, build, test EFI manifest time, what was
   chosen at each menu, what the screen showed, and the commands and outputs
   listed below. Preserve failures.

## What the test EFI changes

`tools/test_efi.py` derives the test EFI from a copy of the known-good EFI and
rejects the result unless the configurations differ in exactly these values:

| Setting | Known-good | Test | Why |
| --- | --- | --- | --- |
| `Kernel → Add` | includes NootedRed, SMCRadeonSensors | both removed; `CezanneGPU.kext` appended (`MinKernel` 25.0.0, x86_64) | NootedRed drives the iGPU through Apple's AMD kexts; SMCRadeonSensors reads GPU sensor registers itself. Nothing else may touch the GPU. |
| `NVRAM → Add → 7C436110-…:boot-args` | `-NRedRBPlus` | `-v keepsyms=1 debug=0x100 msgbuf=1048576 cezanne-stage=N` | Verbose boot, symbolized panics, a halt on panic instead of a reboot, a larger kernel log buffer, and the driver's stage |
| `Misc → Security → AllowSetDefault` | `true` | `false` | The test picker cannot store a new default boot entry in shared NVRAM |
| `Misc → Debug → SysReport` (only with `--sysreport`) | `false` | `true` | One-time OpenCore DEBUG dump of ACPI tables (including `VFCT`, the VBIOS), SMBIOS and PCI information to `SysReport/` on the USB drive, for the [SysReport dump boot](#sysreport-dump-boot) |
| `Misc → Debug → Target` | `67` (`0x43`) | `3` | Drops bit `0x40`, the `opencore-*.txt` log file. On the USB 2.0 stick each flush took about 0.7 s, and the first boot spent 51 s logging before the filesystem scan. On-screen logging (warnings and errors) stays. |

Everything else is byte-identical: ACPI tables, quirks, kernel patches, device
properties, UEFI drivers, OpenCore binaries and `PlatformInfo` (SMBIOS), so
macOS and iCloud see the same machine. The tool also enforces these properties:

- **Each boot resets the other's NVRAM.** Both EFIs share firmware NVRAM.
  Every value the test EFI sets differently must be in both configs'
  `NVRAM → Delete`. Today both delete `boot-args` and `csr-active-config` before
  adding their own, so neither boot inherits the other's arguments.
- **The test EFI never registers itself.** `Misc → Boot → LauncherOption` must
  be `Disabled`, so the USB OpenCore never adds a firmware boot entry and cannot
  become the default.
- **No kept kext depends on a removed one,** judged by each enabled kext's
  `OSBundleLibraries`.
- **Files outside the config are copied unchanged.** The test tree must equal the
  known-good tree minus the two removed bundles and the `Config.plist.bak-*`
  backups, plus the driver; every common file must hash identically.
- **The result passes OpenCore's own validator,** `ocvalidate` from the same
  release.

Both configs keep SIP fully enabled (`csr-active-config` `00000000`). OpenCore
injects kexts at boot, so the driver needs no signing and no SIP change.

## Known-good OpenCore provenance

The known-good EFI reports `opencore-version` `DBG-107-2026-03-20` (OpenCore 1.0.7
DEBUG). The official `OpenCore-1.0.7-DEBUG.zip` from `acidanthera/OpenCorePkg`
release `1.0.7` was downloaded into `out/test-efi/opencore/`. Its SHA-256,
`3644db831dd18344896d7a86077b8c338c0eaa01b1579d7fa00785598cac1f2b`, matches the
GitHub asset digest. The installed `OpenCore.efi`, `BOOTx64.efi` and
`OpenRuntime.efi` hash identically to that release. The installed
`ResetNvramEntry.efi` does **not** match it, so its origin is unknown;
`HfsPlus.efi` is not part of the release and was not checked. The test EFI
copies all of them unchanged. The release's `ocvalidate` reports no issues for
either config.

## The driver

`driver/kext/build.sh` builds `CezanneGPU.kext` (`org.cezanne-driver.gpu`
0.1.0) from the IOKit adapter in `driver/kext/` and the hardware core in
`driver/core/`. It matches `IOPCIMatch` `0x16381002` and, in `probe()`, checks
the stage argument and that the registry reports vendor `1002`, device `1638`
and revision `c9`. It has no `OSBundleRequired`, so it is not marked as needed
for safe-mode boots. All checks and results are logged with the prefix
`CezanneGPU:` and published as `CezanneGPU …` registry properties.

### Stage 0: passive

The driver publishes the registry's `vendor-id`, `device-id`, `revision-id`,
subsystem IDs and `class-code` (missing values are logged as unavailable). It
does not open its provider, touch PCI configuration space or BARs, change
decoding or bus mastering, register interrupts or join power management.

### Stage 1: read-only device access

In `start()`, the driver additionally:

1. Opens the PCI device, so no other driver can claim it while the reads run.
2. Reads the standard configuration header (identity, revision, class,
   command, status, BAR5) and walks the capability list, bounded to 48
   entries, to the power-management capability's control register (PMCSR).
3. Stops unless identity and revision match, the device is already in D0,
   memory decoding is already enabled and BAR5 is a 32-bit memory BAR. It never
   changes the device to make a check pass.
4. Maps BAR5, the register aperture (Linux v6.12 `amdgpu_device_init` uses
   BAR5 for CHIP_BONAIRE and later), **read-only and uncached**
   (`kIOMapReadOnly | kIOMapInhibitCache`), so a stray store faults in the
   kernel instead of reaching the GPU. It checks that the mapping's physical
   address equals BAR5 and that it covers the registers below.
5. Reads exactly two 32-bit registers, in this order:

   | Register | BAR5 byte offset | Meaning |
   | --- | --- | --- |
   | `MP0_SMN_C2PMSG_33` | `0x58184` (dword `0x16061`) | Bit 31 set once IFWI initialization has completed |
   | `RCC_CONFIG_MEMSIZE` | `0x378c` (dword `0xde3`) | Firmware-reserved VRAM size in MiB |

   These are the registers Linux v6.12 reads in
   `amdgpu_discovery_read_binary_from_mem` for discovery-based chips, before
   that function writes any register. Renoir
   (which includes `1638`) takes that path through the `default` case of
   `amdgpu_discovery_set_ip_blocks`.
   [amdgpu_discovery.c](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/amd/amdgpu/amdgpu_discovery.c)
6. Releases the mapping, closes the device and stays attached with the results
   published. A failed check is recorded as `CezanneGPU stage 1 result` and
   does not unload the driver.

Stage 1 never writes PCI configuration space or any register. In particular
it does not use the `MM_INDEX`/`MM_DATA` indexed access Linux uses next to
fetch the IP discovery table: that requires writing an index register. Linux
looks for the table near the top of the carveout, which the known-good
registry reports as 2 GiB, beyond this host's 256 MiB BAR0 aperture, so plain
loads through BAR0 cannot reach it either.

Guards, each tested offline:

- The core (`driver/core/`) has read callbacks only; it has no write function,
  no `volatile` access and no includes beyond `stdint.h`. Both the core and the
  adapter refuse any register offset not on the stage 1 list, unaligned
  offsets and offsets beyond the mapping.
- `tests/test_core.py` runs the core against fake configuration space and
  registers under AddressSanitizer and UBSan, and checks that five weakened
  copies of the core (D0, decoding, capability bound, aperture bound,
  all-ones detection) each fail the suite.
- `tests/test_kext.py` builds the kext and checks:
  - Source: none of the denylisted calls (configuration or register writes,
    decode or bus-master changes, DMA, interrupts, power management). Every
    BAR mapping carries `kIOMapReadOnly`, every `volatile` pointer is `const`,
    and nothing is stored through the aperture. A planted violation of each
    rule is detected.
  - Binary: every direct call target is on a fixed list (metaclass plumbing,
    logging, the boot argument, configuration reads, the mapping's physical
    address and the core). Virtual calls are indirect, so the source check is
    the guard for them. The compiled register read was also inspected: a
    single 32-bit load from the mapping.
  - Symbols: every undefined symbol resolves through `kmutil libraries` to a
    declared library on this host. `kmutil` silently omits a symbol it cannot
    resolve, so the test compares against `nm -u`, and it needs a resolved path
    (it reported `No extension` for the same bundle under the `/var` symlink).

Building only produces files; no test loads the kext. The linker warns that
`libkmod.a` was built for macOS 26.5 while the kext targets 26.0. The host runs
26.4.1; whether the kernel accepts the kext is checked at the first test boot.

Expected stage 1 values, from the known-good boot's registry (an observation
of the NootedRed stack, not proof of what the registers hold): BAR5 at
`0xfca00000` with 512 KiB (`assigned-addresses`), and `VRAM,totalMB` 2048, so
`RCC_CONFIG_MEMSIZE` should read `0x800`. Bit 31 of `C2PMSG_33` should be set,
because firmware has finished before macOS starts.

Stage 1 risks: the reads run while no driver has initialized the GPU since the
firmware's GOP. A device that has stopped decoding returns all ones, which is
reported as `device-not-responding`. A read that hangs the bus would freeze the
machine during boot; power it off and boot the known-good EFI. Linux performs
these same reads early in every boot of this chip family, before its discovery
code writes a register, which is why they were chosen.

### Stage 2: write-free discovery table read

On an APU the "VRAM" is a carveout of system RAM. Linux v6.12
`gmc_v9_0_mc_init` treats it as directly addressable on x86-64: for APUs it
sets the aperture base to `gfxhub_v1_0_get_mc_fb_offset`, which is
`MC_VM_FB_OFFSET << 24`. Linux only reads that register; the firmware sets it.
So the discovery binary can be read from system memory instead of through the
`MM_INDEX`/`MM_DATA` write that Linux uses at discovery time (Linux reads it
before the aperture is set up). Stage 2 runs only after stage 1 returned `ok`,
in the same `start()`:

1. Reads one more register through the same read-only BAR5 mapping:

   | Register | BAR5 byte offset | Source |
   | --- | --- | --- |
   | `MC_VM_FB_OFFSET` (GC) | `0xa5ac`: GC segment 0 base `0x2000` + dword `0x96b` | `renoir_ip_offset.h` `GC_BASE`, `gc_9_0_offset.h`; field mask `0x00FFFFFF` (`gc_9_0_sh_mask.h`) |

   The same header's `MP0_BASE` segment 0 (`0x16000`) locates the stage 1
   register `MP0_SMN_C2PMSG_33` (`0x16061`), which read as expected.
2. Derives the carveout base (`MC_VM_FB_OFFSET << 24`), its size
   (`RCC_CONFIG_MEMSIZE << 20`) and the table address, `base + size - 64 KiB`
   (`DISCOVERY_TMR_OFFSET`, `amdgpu_discovery.h`). It stops on all ones,
   reserved bits, zero, a carveout under 64 KiB or one ending above 2^48.
3. Parses the device's `assigned-addresses` registry property and stops if the
   carveout overlaps any of its memory BARs.
4. Maps exactly the 10 KiB table (`DISCOVERY_TMR_SIZE`) with
   `IODeviceMemory::withRange` **read-only and uncached**, copies it with
   32-bit loads, and releases the mapping.
5. Validates the copy as `amdgpu_discovery_init` does: binary signature
   `0x28211407`, the byte-sum binary checksum, the IP discovery table signature
   `IPDS` and its checksum. It walks every die's IP list with bounds checks and
   records GC and MP0 instance 0. Their bases must match `renoir_ip_offset.h`
   (GC `0x2000`/`0xA000`, MP0 `0x16000`), which confirms after the fact that
   step 1 read the right register.
6. Publishes `CezanneGPU MC_VM_FB_OFFSET`, `carveout base`, `discovery
   address`, `discovery signature`, the versions, base addresses and IP count,
   and `CezanneGPU stage 2 result`. The raw binary is published as `CezanneGPU
   discovery binary` only after the signature and checksum pass, so arbitrary
   memory is never exposed in the registry.

Guards, each tested offline: the stage 2 register is refused at stage 1
(`registerAllowed(offset, stage)`); the core tests cover the carveout
arithmetic, every refusal, the `assigned-addresses` parser, BAR overlap, and a
synthetic discovery binary with corrupted signatures, checksums, sizes, die
ids, bases and 64-bit tables. Four more weakened cores (stage gating, BAR
overlap, table checksum, base cross-check) must fail the suite.
`tests/test_kext.py` now also requires every `->map(` to carry
`kIOMapReadOnly` and every `withRange` to use `kDiscoveryTmrSize`, and allows
`IODeviceMemory::withRange` as the one new direct call.

Expected stage 2 values: `MC_VM_FB_OFFSET` non-zero with the carveout outside
BAR0 (`0x640000000` at stage 1), BAR2 and BAR5; discovery binary signature
`0x28211407`; GC version 9.3.x (`gfx90c`); GC bases `0x2000`/`0xA000`; MP0
base `0x16000`. The kernel reported 24 GiB (`mem_actual 0x600000000`) and
22 GiB usable (`sane_size 0x580000000`) at stage 1, so a carveout at
`0x580000000` (`MC_VM_FB_OFFSET` `0x580`) is plausible, not predicted.

Stage 2 risks:

- **TMR protection.** The table lives in a trusted memory region the PSP may
  protect from CPU access. Linux v6.12 reads this region with the CPU only in
  `amdgpu_discovery_read_binary_from_sysmem` (other APUs, via ACPI), and on
  Renoir reaches it through the GPU at discovery time. A protected read may
  return all ones or zeros (reported as `discovery-signature`) or, at worst,
  raise a machine check, which panics; power off and boot the known-good EFI.
- **Wrong register.** If `0xa5ac` is not `MC_VM_FB_OFFSET` on this chip, the
  derived address points elsewhere. Reads of system RAM have no side effects;
  the BAR overlap check refuses the device's own ranges; non-RAM physical
  addresses outside those are not excluded. The base cross-check reports the
  error afterwards.
- Nothing is written, so a failure ends that boot's experiment without
  changing GPU state.

### Stage 3: read-only GC configuration

Runs only after stage 2 returned `ok`, because that cross-check confirmed the
GC base addresses these registers use. Through the same read-only BAR5
mapping, in this order:

| Register | BAR5 byte offset | Linux v6.12 use |
| --- | --- | --- |
| `GRBM_STATUS` | `0x8010` (`0x2000` + `0x0004`) | Status; bit 31 `GUI_ACTIVE` |
| `GRBM_GFX_INDEX` | `0x30800` (`0xA000` + `0x2200`) | Selects the SE/SH/instance per-instance registers read from |
| `CC_GC_SHADER_ARRAY_CONFIG` | `0x89bc` (`0x2000` + `0x026f`) | `gfx_v9_0_get_cu_active_bitmap`: fused-off CUs, bits 31:16 |
| `GC_USER_SHADER_ARRAY_CONFIG` | `0x89c0` (`0x2000` + `0x0270`) | Same function: driver-disabled CUs, bits 31:16 |
| `CC_RB_BACKEND_DISABLE` | `0x98f4` (`0x2000` + `0x063d`) | `gfx_v9_0_get_rb_active_bitmap`, bits 23:16 |
| `GC_USER_RB_BACKEND_DISABLE` | `0x9b7c` (`0x2000` + `0x06df`) | Same function |
| `GB_ADDR_CONFIG` | `0x98f8` (`0x2000` + `0x063e`) | `gfx_v9_0_gpu_early_init` reads it for GC 9.3.0 before the golden-register writes |

Offsets and fields are from `gc_9_0_offset.h` and `gc_9_0_sh_mask.h`. The
driver computes the masks as Linux does: active CUs are
`~(CC | USER) >> 16` limited to the GC info table's CUs per SH (8); active RBs
are `~(CC | USER) >> 16` limited to RBs per SE / SHs per SE (2).

**Difference from Linux:** Linux writes `GRBM_GFX_INDEX` to select SE 0 / SH
0 before reading the CU and RB registers. Stage 3 does not write it. It reads
the current index and publishes the masks only if its `SE_INDEX` (23:16) and
`SH_INDEX` (15:8) are already 0 and the GC info table reports one SE with one
SH, as stage 2 measured; otherwise the result is `gfx-index-not-se0-sh0` or
`gc-info-unavailable` and only the raw values are published. The
`*_BROADCAST_WRITES` bits affect writes only.

Guards, each tested offline: the stage 3 registers are refused at stages 1 and
2; each stage's register list is a prefix of the next; the core tests cover the
masks, user-disabled CUs and RBs, a non-zero index, an unexpected SE count,
and a short mapping. Two more weakened cores (index check, stage 2/3 gating)
must fail the suite.

Expected stage 3 values: the CPU is a Ryzen 5 5600GT, specified with 7 GPU
CUs, so `active CU count` 7 with one bit clear in the 8-bit mask;
`active RB count` 2; `GUI_ACTIVE` clear (nothing has started the graphics
engine); `GB_ADDR_CONFIG` near Linux's Renoir golden value `0x24000042` under
mask `0xf3e777ff` (an expectation, not a requirement).

Stage 3 risks: these are configuration and status registers Linux reads
without side effects, in the same register window as stages 1 and 2. A hang
would freeze the boot; power off and boot the known-good EFI. Nothing is
written.

### SysReport dump boot

**Status:** approved by the user on 2026-10-05 and done in boot 12. This is a
test-EFI configuration change, not a driver stage.

**Purpose.** Stage 9 needs to know which carveout memory the firmware uses.
Linux reads this from the VBIOS firmware-usage table
(`vram_usagebyfirmware`); on APUs the VBIOS reaches the OS in the ACPI `VFCT`
table. macOS 26 publishes neither in the registry (boot 11), so stage 9's
check cannot use it yet.

**What changes.**

- `tools/test_efi.py build --sysreport` adds exactly one config change to
  the stage 0 test EFI: `Misc → Debug → SysReport` `true`. The tool's
  expected-change check includes it only when the flag is given. The
  manifest records `"sysreport": true`.
- The driver runs at stage 0 (passive): no device access in this boot.
- OpenCore DEBUG writes `SysReport/` to the root of the USB drive once at boot.
  - Expect a pause at the picker while it writes.
  - It is a single dump, not per-line file logging, which stays off.

**Steps.**

1. Copy `out/test-efi/usb-sysreport/EFI` to the drive and run `verify` as
   for any stage.
2. Cold-boot it. Reaching the desktop is not required, since the dump happens
   in OpenCore, but it shows the boot path is unchanged.
3. Back on either EFI, copy the dump into ignored `out/` and remove it from
   the drive. It contains SMBIOS serials.

   ```sh
   ditto /Volumes/CZTEST/SysReport out/test-efi/sysreport
   rm -rf /Volumes/CZTEST/SysReport   # the USB copy only; check the path
   ```
4. Offline: extract the VBIOS from `VFCT`, then read its firmware-usage table
   (`vram_usagebyfirmware`: start and size in KiB) as
   `amdgpu_atomfirmware_allocate_fb_scratch` does. Revise stage 9's
   free-page check from it for the user's review.

**Return:** put the stage 9 EFI (or any other) back on the drive before the
next driver test. The SysReport EFI is only for this dump.

**Result (boot 12, 2026-10-05, stage 0 with SysReport).** The boot reached the
desktop (`cezanne-stage=0`). OpenCore wrote 33 files: 25 ACPI tables
including `VFCT-1.aml`, SMBIOS, PCI, CPU, GOP and driver information. The
user copied them to ignored `out/test-efi/sysreport/`.

- **`VFCT`.** 55 428 bytes, image offset 76. One image: bus 10, device 0,
  function 0, `1002:1638`, 55 296 bytes, starting `55AA` with an `ATOM`
  header at `0x188`. It is saved as `out/test-efi/sysreport/vbios-1638.rom`
  (ignored).
- **Firmware-usage table.** The master data table (`0x94a0`) points
  `vram_usagebyfirmware` at `0xcf2c`, revision 2.1. It reads
  `start_address_in_kb` `0`, `used_by_firmware_in_kb` `0`,
  `used_by_driver_in_kb` `0`. **The firmware reserves no carveout memory.**
- **What Linux does with it.** Linux v6.12
  `amdgpu_atomfirmware_allocate_fb_v2_1` reserves the firmware region only
  when it carries the SR-IOV message-share flag. Otherwise the table only sizes
  the AtomBIOS scratch area. On this host Linux would therefore reserve
  nothing for the firmware.
- **Boot framebuffer.** `GOPInfo.txt` reports 1920×1080, 4 bytes per pixel, at
  `0xFFE0000000`, `0x7E9000` bytes (about 7.9 MiB). That is where the firmware
  had placed BAR0, so the framebuffer is the first 7.9 MiB of the carveout.

### Revision: stage 9 free-page check

**Status: approved by the user on 2026-10-05, implemented, and run
successfully in boot 13.**
It replaces stage 9 step 1's "all zero" test, which boot 11 showed is wrong
for stale DRAM.

**What is known about the target page.** It lies 1 GiB into the carveout,
GPU `0xF440000000`, CPU `0x600000000`. By the regions Linux reserves on this
host, it is free:

- **No firmware region.** The VBIOS reports none (boot 12).
- **The boot framebuffer** (stolen VGA memory) is the first 7.9 MiB. The
  check already avoids the lowest 64 MiB.
- **The discovery binary** is in the top 64 KiB. The check already avoids the
  highest 64 MiB.
- **Everything else** Linux allocates itself. No GPU driver runs in a test
  boot.

**Revised step 1.** As before, with the zero test replaced:

1. Mailbox idle; `MC_VM_FB_LOCATION_BASE` `0xf400` and `MC_VM_FB_OFFSET`
   `0x5c0`; the region inside the carveout, outside both 64 MiB reserves and
   every BAR.
2. **Stability instead of zero.** Read the 64 KiB region twice, 1 s apart.
   Require every word to be identical (`table-region-in-use` otherwise):
   memory that something is actively writing changes; stale data does not.
3. **Snapshot.** Keep a copy of the 4 KiB page as it was before the
   transfer.

**Revised step 4.** It compares against the snapshot rather than against
zero:

- Bytes 148–4095 must equal the snapshot (`table-overflow`).
- Bytes 0–147 must differ from it somewhere (`table-not-written`).
- If stale data happened to match the new table exactly, step 4 would report
  `table-not-written`. That is a false alarm on the safe side.

Steps 2 and 3 (the three messages) and every other guard are unchanged. The
driver keeps the 4 KiB snapshot in its own memory and never writes the
carveout.

**As built:**

- **Core.** `checkRegionUnused` is replaced by `checkRegionStable(memory,
  length, writer, pauses, snapshot)`.
  - It reads the 64 KiB into the snapshot, waits `kMetricsStablePauses`
    (1000) pauses of 1 ms, then compares a second read word by word.
  - `verifyMetricsPage(page, snapshot, metrics)` compares with the snapshot
    instead of zero.
- **Adapter.**
  - The snapshot is a 64 KiB array inside the driver object; its first 4 KiB
    is the page.
  - The 1 ms pause now sleeps (`IOSleep(1)`) instead of busy-waiting
    (`IODelay`), so the 1 s wait does not spin a CPU. This also applies to the
    SMU polls.
- **Tests.**
  - The core tests use stale non-zero data throughout: it passes when stable
    and fails when one word changes during the wait.
  - After the transfer, unchanged bytes past 148 pass. A write past 148 fails
    (`table-overflow`), and a page the SMU did not write fails
    (`table-not-written`).
  - Two more weakened cores must fail: the change check removed, and the wait
    shortened to one pause.

**What this does not establish.** A region that something writes rarely, or
holds unchanged, would pass the stability test. That is acceptable here
because no reserved region covers the page, and the write is 148 bytes into
reserved, OS-unused memory. The misdirected-write response (power off at once
on `table-not-written` or `table-overflow`) and the backup recommendation
stay.

### Stage 4: diagnostic interface

Stage 4 runs stages 1–3 at boot unchanged. If stage 3 returned `ok`, the
driver also accepts connections from `cezanne-diag` (`tools/diag/`), so the
same registers can be read again from the running system without a reboot,
for example to watch `GRBM_STATUS` over time. It does not add any register.

- **Who can connect:** `newUserClient` refuses unless the driver runs at stage
  4 or later, stage 3 succeeded (`CezanneGPU diagnostics` is `true`), the
  connection type is 0, and the caller is root (`clientHasPrivilege`,
  `kIOClientPrivilegeAdministrator`). These checks run before a client is
  created.
- **What it can do:** two selectors with fixed scalar counts, which
  `IOUserClient::externalMethod` enforces. `kDiagnosticGetInfo` returns the
  interface version and stage. `kDiagnosticReadRegister` takes a byte offset
  and returns a status and a value. No memory mapping, notification port,
  structure argument or asynchronous call is offered.
- **Each read:** under a lock, the driver opens the PCI device, re-reads and
  re-checks configuration space (identity, D0, memory decoding, BAR5), maps
  BAR5 read-only and uncached, checks the mapping, reads the one register if
  the stage allows it (otherwise `register-not-allowed`), releases the mapping
  and closes the device. A changed device state is reported, not corrected.
- **Guards, tested offline:** the core rejects unlisted, unaligned and
  out-of-range offsets; `tests/test_kext.py` checks that the privilege and
  stage checks precede client creation, that reads use the allowlist with the
  driver's own stage, and the binary's direct calls (now also
  `clientHasPrivilege`, the `IOUserClient` constructor and destructor, and
  `IOLock`). `tests/test_diag_tool.py` builds the tool, checks it calls only the
  two selectors, names every stage 3 register in order, and reports a missing
  driver.

Expected stage 4 values: the boot-time results as in boot 5, `CezanneGPU
diagnostics` `true`, and `sudo cezanne-diag` reading the same values (with
`GRBM_STATUS` possibly varying).

### Stage 5: power, clock and engine state

Prepares the first reviewed write by recording what the firmware left running.
The boot does exactly what stage 4 does; the 38 new registers are read **only
on demand** through `cezanne-diag`, never during boot. The tool prints and
flushes each register's name before reading it, so if a read ever hangs the
machine, the last line on screen names the register.

Clock frequencies are not among them: on Renoir, Linux gets clocks from the
SMU's metrics table, which takes SMU messages (register writes). Stage 5 reads
the clock-gating and power-gating state instead, from the same registers
Linux's `get_clockgating_state` functions read.

Each register is read by Linux v6.12 as noted. Offsets come from the
`asic_reg` headers that the Linux block includes (`smuio_12_0_0`, `mp_12_0_0`,
`gc_9_0`, `sdma0_4_0`/`4_2` (equal), `hdp_4_0`, `athub_1_0`, `mmhub_1_0`,
`osssys_4_0`). The block bases are the ones measured in this host's discovery
table at stage 2, and each equals `renoir_ip_offset.h`. All lie inside BAR5.

| Register | BAR5 byte offset | Linux v6.12 use |
| --- | --- | --- |
| `SMUIO_GFX_MISC_CNTL` | `0x5a320` | smu_v12_0_get_gfxoff_status: PWR_GFXOFF_STATUS 2:1, 2 = GFX on |
| `MP1_SMN_C2PMSG_66` | `0x58a08` | renoir_ppt SMU message register |
| `MP1_SMN_C2PMSG_82` | `0x58a48` | renoir_ppt SMU argument register |
| `MP1_SMN_C2PMSG_90` | `0x58a68` | renoir_ppt SMU response register |
| `MP0_SMN_C2PMSG_35` | `0x5818c` | psp_v12_0: bootloader ready, bit 31 |
| `MP0_SMN_C2PMSG_81` | `0x58244` | psp_v12_0: secure OS sign of life |
| `RLC_CGTT_MGCG_OVERRIDE` | `0x3b120` | gfx_v9_0_get_clockgating_state (GFX-gated) |
| `RLC_CGCG_CGLS_CTRL` | `0x3b124` | gfx_v9_0_get_clockgating_state (GFX-gated) |
| `RLC_CGCG_CGLS_CTRL_3D` | `0x3b314` | gfx_v9_0_get_clockgating_state (GFX-gated) |
| `RLC_MEM_SLP_CNTL` | `0x3b018` | gfx_v9_0_get_clockgating_state (GFX-gated) |
| `CP_MEM_SLP_CNTL` | `0x0c1e4` | gfx_v9_0_get_clockgating_state (GFX-gated) |
| `RLC_PG_CNTL` | `0x3b10c` | gfx_v9_0 power gating (read-modify-write) (GFX-gated) |
| `GRBM_STATUS2` | `0x08008` | gc_reg_list_9 (IP dump) (GFX-gated) |
| `GRBM_STATUS_SE0` | `0x08014` | gc_reg_list_9 (IP dump) (GFX-gated) |
| `CP_BUSY_STAT` | `0x0867c` | gc_reg_list_9 (IP dump) (GFX-gated) |
| `CP_CPF_STATUS` | `0x0821c` | gc_reg_list_9 (IP dump) (GFX-gated) |
| `CP_ME_CNTL` | `0x086d8` | gfx_v9_0_cp_gfx_enable (read-modify-write) (GFX-gated) |
| `CP_MEC_CNTL` | `0x08234` | gc_reg_list_9 (IP dump) (GFX-gated) |
| `RLC_CNTL` | `0x3b000` | gfx_v9_0 RLC state read (GFX-gated) |
| `RLC_STAT` | `0x3b010` | gc_reg_list_9 (IP dump) (GFX-gated) |
| `CP_PFP_INSTR_PNTR` | `0x08694` | gc_reg_list_9 (IP dump) (GFX-gated) |
| `CP_ME_INSTR_PNTR` | `0x08698` | gc_reg_list_9 (IP dump) (GFX-gated) |
| `CP_MEC1_INSTR_PNTR` | `0x086a0` | gc_reg_list_9 (IP dump) (GFX-gated) |
| `SDMA0_CLK_CTRL` | `0x049ec` | sdma_v4_0_get_clockgating_state |
| `SDMA0_POWER_CNTL` | `0x049e8` | sdma_v4_0_get_clockgating_state |
| `SDMA0_F32_CNTL` | `0x04a28` | sdma_v4_0: engine halt state |
| `SDMA0_STATUS_REG` | `0x04a14` | sdma_v4_0 idle checks |
| `SDMA0_GFX_RB_CNTL` | `0x04b80` | sdma_v4_0 ring enable |
| `HDP_MEM_POWER_LS` | `0x03fd0` | hdp_v4_0_get_clockgating_state |
| `ATHUB_MISC_CNTL` | `0x030a8` | athub_v1_0_get_clockgating |
| `ATC_L2_MISC_CG` | `0x69928` | mmhub_v1_0_get_clockgating |
| `DAGB0_CNTL_MISC2` | `0x6818c` | mmhub_v1_0_get_clockgating |
| `MC_VM_FB_LOCATION_BASE (MMHUB)` | `0x6a0b0` | mmhub_v1_0_get_fb_location |
| `MC_VM_FB_LOCATION_TOP (MMHUB)` | `0x6a0b4` | mmhub_v1_0_get_fb_location |
| `VM_L2_CNTL (MMHUB)` | `0x69a00` | mmhub_v1_0 cache setup (read-modify-write) |
| `VM_CONTEXT0_CNTL (MMHUB)` | `0x69b00` | mmhub_v1_0 (read-modify-write) |
| `MC_VM_MX_L1_TLB_CNTL (MMHUB)` | `0x6a0cc` | mmhub_v1_0 (read-modify-write) |
| `IH_RB_CNTL` | `0x04480` | vega10_ih ring control |

**GFX-gated registers.** Linux disables GFXOFF before its GC IP dump. Stage 5
cannot send that SMU message, so for each GC register the driver first reads
`SMUIO_GFX_MISC_CNTL` and reads the register only if `PWR_GFXOFF_STATUS` is
2 (GFX on); otherwise it returns `gfx-not-on`. The stage 1–3 GC registers
were read at boot without this gate and returned plausible values.

**What the values answer before a write:** the GFXOFF state and the SMU
mailbox (whether something already talked to the SMU, and its last response);
whether the PSP's secure OS is running (`C2PMSG_81`), which decides how
firmware gets loaded; which clock and power gating features the firmware
enabled; whether the CP, RLC, SDMA and IH engines are halted or have rings
enabled; and how the firmware configured the memory hub's framebuffer
location, L2 and context 0.

Guards, tested offline: the stage 5 registers are refused at stage 4 and
below; the stage 3 list is a prefix of the stage 5 list; every offset is
aligned, unique and inside BAR5; a GFX-gated read consults SMUIO first and is
refused unless the status is 2; an ungated read does not consult it. Two more
weakened cores (the GFX gate, stage 4/5 gating) must fail the suite. The tool
names every stage 5 register in list order.

Expected stage 5 values: stages 1–4 as in boot 6. `PWR_GFXOFF_STATUS` 2
(nothing enabled GFXOFF). For the others there is no prediction beyond
"engines idle": `GRBM_STATUS` has been idle in every boot.

Stage 5 risks: each register is a status or control register Linux reads; none
is a data port, FIFO or counter that clears on read (`HDP_EDC_CNT` and indexed
`*_DATA` registers were deliberately left out). A read that hangs would freeze
the machine with that register's name on screen; power off and boot the
known-good EFI. Nothing is written.

### Stage 6: first reviewed write

**Status: approved by the user on 2026-10-05, implemented, and run
successfully in boot 8.** The proposal below is kept as approved. The implementation section
after it records one refinement: the test runs as three ordered calls.

The purpose is to prove a register write works and is reversible, with
nothing in the GPU depending on the result, before any write that changes
GPU behavior.

**The register.** `SCRATCH_REG0` (GC): dword `0x2040`, base index 1
(`gc_9_0_offset.h`), so BAR5 byte `0x30100` (`0xA000` + `0x2040`, times 4).
It is a scratch register with no hardware function.

**Why it is safe:**

- Linux v6.12 writes it from the CPU: `gfx_v9_0_ring_test_ring` does
  `WREG32(scratch, 0xCAFEDEAD)`, then has the CP overwrite it to prove the ring
  works. Its only other use, `gfx_v9_0_init_rlcg_reg_access_ctrl`, is the
  indirect register path for SR-IOV virtual functions, which this host is not.
- Nothing can be reading it. Stage 5 showed the CP's graphics and compute
  engines halted with no microcode ever run, the RLC disabled, and no rings
  enabled; the stage 6 code re-checks that before writing.
- It is restored. The original value is written back and read back, so the
  register ends as the firmware left it. A cold boot resets the GPU in any
  case.

**What the stage 6 code would do.** It runs only when `cezanne-diag
--scratch-test` asks for it, never at boot. The boot does exactly what stage 5
does. One new diagnostic selector would:

1. Take the same lock and re-run the per-read checks: identity, D0, memory
   decoding and BAR5.
2. Check the preconditions with reads, stopping with a named status if any
   fails:
   - `PWR_GFXOFF_STATUS` is 2 (GFX on);
   - `CP_ME_CNTL` has `ME_HALT`, `PFP_HALT` and `CE_HALT` set;
   - `CP_MEC_CNTL` has `MEC_ME1_HALT` and `MEC_ME2_HALT` set;
   - `RLC_CNTL` is 0;
   - `GRBM_STATUS` `GUI_ACTIVE` is clear.
3. Read `SCRATCH_REG0` twice about 1 ms apart and stop if the values differ,
   since that would mean something else writes it.
4. Write `0xCAFEDEAD` (Linux's value) and read it back.
5. Write the original value back and read it back.
6. Return the original value, both read-backs and a status.

**How the write path is confined:**

- The core gains one write callback and a write allowlist containing only
  `SCRATCH_REG0`. The scratch test is the only caller, and the values written
  are limited to `0xCAFEDEAD` and the value read in step 3.
- The adapter maps only the 4 KiB page holding `SCRATCH_REG0` writable
  (`IODeviceMemory::withRange` at BAR5 + `0x30000`, uncached). Every read
  still uses the read-only BAR5 mapping.
  - That page also holds other GC registers, including `GRBM_GFX_INDEX` at
    `0x30800`. Only the allowlist and the tests keep the code off them.
  - The writable mapping is released before the call returns.
- `tests/test_core.py` and `tests/test_kext.py` replace their "no write path"
  checks with "exactly one write site, to the allowlisted offset". Weakened
  copies must fail:
  - a widened allowlist;
  - a missing precondition;
  - a missing restore.
- Interface: still root-only and stage-gated (stage 6). `cezanne-diag` runs
  the test only with the explicit `--scratch-test` flag, prints each step
  before doing it, and flushes.

**Expected result:**

- **Original value:** most likely 0, though this is not predicted; whatever it
  is, it must be stable across both reads.
- **First read-back:** `0xCAFEDEAD`.
- **Second read-back:** equals the original.
- **All other diagnostics** read the same as boot 7.

**Outcomes and responses:**

- **Read-back differs from what was written:** the register is not writable
  from the host in this state. Record it; it is a finding, not a reason to
  retry or try another register.
- **The original value changes between reads:** a consumer exists. The test
  stops without writing; record it and re-review.
- **A hang or panic** (the last line on screen names the step): power off,
  unplug the drive, and cold-boot the known-good EFI. GPU state is reset by
  the power cycle. Record it as rule 6 requires.
- **After the test,** `cezanne-diag` must read all 48 registers with the boot 7
  values. Then shut down completely before returning to the known-good boot.

**Implementation (as built):**

- **Core** (`driver/core/`):
  - `writeAllowed(offset, stage)` is true only for `SCRATCH_REG0` at stage 6
    or later.
  - `writeRegister` is the only call of the `RegisterWriter::write32`
    callback, and it checks that allowlist first.
  - `checkScratch` covers steps 2 and 3 and writes nothing.
  - `writeScratchPattern` re-runs `checkScratch`, requires the same original
    value, then writes `0xCAFEDEAD` and reads it back.
  - `restoreScratch` writes the original value back and reads it back.
  - New statuses: `cp-not-halted`, `rlc-enabled`, `gfx-busy`,
    `scratch-unstable`, `register-write-failed`, `scratch-readback-mismatch`,
    `scratch-restore-mismatch` and `scratch-out-of-order`.
- **Three ordered calls (refinement):** a single driver call cannot report
  steps, so the test is three selectors, each printed and flushed by the tool
  before it is sent:
  - `kDiagnosticScratchCheck` (steps 1–3);
  - `kDiagnosticScratchWrite` (step 4);
  - `kDiagnosticScratchRestore` (step 5).

  The driver accepts them only in that order and only from the connection
  that ran the check. A write is refused (`scratch-out-of-order`) unless it
  follows a passing check. If a connection closes after the write without
  restoring, the driver restores the original value itself and records
  `CezanneGPU scratch abandoned restore`.
- **Adapter** (`driver/kext/`):
  - Every diagnostic operation runs through `accessDevice`, which does the
    per-read checks and maps BAR5 read-only.
  - Only the write and restore steps also map the scratch page writable:
    `IODeviceMemory::withRange` at BAR5 + `0x30000`, 4 KiB, mapped
    `kIOMapInhibitCache`.
  - `registerWrite` refuses any offset outside the allowlist or the page
    before storing. The 1 ms pause uses `IODelay`.
- **Tests:**
  - `tests/test_core.py` allows exactly one `write32` call, after the
    allowlist. It also requires five more weakened cores to fail: widened
    allowlist, CP-halt check, RLC check, stability check and restore check.
  - The core tests cover each precondition stopping before any write, a value
    changed between reads, a stale original, refusal at stage 5, ignored
    writes, failed writes and a failed restore.
  - `tests/test_kext.py` allows exactly one writable map, one register store
    and two non-const `volatile` pointers. Writable access may be requested
    only by the write and restore steps, and closing the connection must
    restore.
  - `tests/test_diag_tool.py` checks that the scratch selectors run only
    under `--scratch-test` and only with a stage 6 driver.

**What it does not do:**

- It writes no register that controls hardware.
- It does not select SE/SH, program golden settings, or send SMU or PSP
  messages.
- It does not enable an engine or touch the memory hub.

Each of those is a later stage needing its own review.

### Stage 7: first SMU query

**Status: approved by the user on 2026-10-05 ("go straight to stage 7"),
implemented, and run successfully in boot 9.** The proposal below is kept as
approved. An implementation section follows it.

**Purpose.** This is the first conversation with the SMU (MP1 12.0.1). The
SMU is the power firmware that every later step depends on: clocks, power
gating, GFXOFF control and the metrics table. Stage 7 asks it two questions
whose answers change nothing: its firmware version and its driver-interface
version.

**What Linux does first.** On Renoir, `smu_v12_0_check_fw_version`
(`renoir_ppt.c` `.check_fw_version`) calls `smu_cmn_get_smc_version`. That
sends exactly these two messages, before any message that changes state:

| Message | Index (`smu_v12_0_ppsmc.h`) | Parameter | Answer |
| --- | --- | --- | --- |
| `PPSMC_MSG_GetDriverIfVersion` | `0x3` | 0 | Driver-interface version, read from the argument register |
| `PPSMC_MSG_GetSmuVersion` | `0x2` | 0 | Firmware version `program.major.minor.debug`, one byte each |

The mailbox protocol (`smu_cmn.c`: `smu_cmn_send_smc_msg_with_param`,
`__smu_cmn_send_msg`, `__smu_cmn_poll_stat`, `smu_cmn_read_arg`), using the
registers stage 5 already reads:

1. Poll `MP1_SMN_C2PMSG_90` (response) until non-zero, so no message is in
   flight. Linux skips this poll for its very first message after init
   (`SMU_FW_INIT`). Stage 7 requires it anyway, since stage 5 showed `0x1`
   there.
2. Write `C2PMSG_90` ← 0, then `C2PMSG_82` (argument) ← parameter, then
   `C2PMSG_66` (message) ← index.
3. Poll `C2PMSG_90` until non-zero; Linux allows up to 2 s
   (`usec_timeout` × 20).
4. Read the answer from `C2PMSG_82`.

Response `0x1` is OK. The others are `0xFF` failed, `0xFE` unknown command,
`0xFD` bad prerequisites and `0xFC` busy (`PPSMC_Result_*`).

**Why it is safe enough:**

- **Queries only.** Both messages only return values. They are the first
  messages Linux sends, and its later state-changing messages depend on
  them, not the other way round.
- **Mailbox idle.** Stage 5 read message 0, argument 0 and response `0x1` in
  boots 7 and 8, so no message was pending and the last one succeeded.
  Nothing else in the test boot drives this mailbox: NootedRed is not loaded.
  This is the graphics driver's MP1 mailbox, distinct from the SMN/RSMU
  mailbox CPU tools use. The test EFI's `SMCProcessorAMD.kext` is believed to
  read MSRs only; this is **not verified**, and step 1 detects a busy
  mailbox.
- **No restore needed.** These registers carry messages. Linux leaves them
  holding the last exchange, and so would stage 7.

**What the stage 7 code would do.** It runs on request through `cezanne-diag
--smu-query`, never at boot, with the boot unchanged from stage 6. Each step
is printed and flushed before it is sent:

1. **Check:** run the per-read device checks, then read `C2PMSG_66`, `_82`
   and `_90`. Stop with `smu-busy` unless `_90` is non-zero.
2. **Query `GetDriverIfVersion`:** the three writes, poll `_90` with a 2 s
   limit, read `_82`. Stop on any response other than `0x1`, or on timeout
   (`smu-timeout`).
3. **Query `GetSmuVersion`:** the same.
4. **Show the result:** the answers and responses, followed by the usual
   register dump.

**How the write path is confined** (as in stage 6):

- **A value-level allowlist.** The core's write allowlist would grow from
  `SCRATCH_REG0` to exactly these writes, from stage 7 on:
  - `C2PMSG_90` ← `0`;
  - `C2PMSG_82` ← `0`;
  - `C2PMSG_66` ← `0x2` or `0x3`.

  Any other register or value is refused.
- **One writable page.** The adapter maps only the 4 KiB page holding the
  three registers writable (BAR5 + `0x58000`), only during a query.
  - That page also holds PSP mailbox registers (`MP0_SMN_C2PMSG_33`/`35`/`81`
    and the PSP ring registers). Only the allowlist keeps the code off them.
    That makes the per-value allowlist and its tests the main guard.
- **Ordering.** As in stage 6, the driver accepts the steps only in order,
  per connection.
- **Tests.**
  - Every refused register and value.
  - Write order: response clear, argument, message.
  - The busy precondition, timeout and each error response.
  - That the version is read only after an OK response.
  - Weakened cores must fail: a widened register list, a widened value list,
    a missing busy check, a missing timeout.

**Expected result:**

- **Both responses:** `0x1`.
- **`GetDriverIfVersion`:** about 14 (`SMU12_DRIVER_IF_VERSION` in v6.12).
  A different value is not an error: Linux treats a mismatch as non-critical.
- **`GetSmuVersion`:** a plausible version with program, major and minor
  bytes. There is no exact prediction.
- **Afterwards:**
  - `C2PMSG_66` reads `0x2`, the last message sent;
  - `C2PMSG_90` reads `0x1`;
  - `C2PMSG_82` holds the version;
  - every other register matches boot 8, apart from the PSP counter.

**Risks and responses:**

- **The SMU manages the whole SoC.** On an APU, MP1 controls CPU and GPU power
  together. A wedged SMU could affect more than graphics: clocks, fans or a
  hang. This is the largest risk of any stage so far.
  - Mitigations: these are the queries Linux sends first on every boot of this
    chip family; the busy check; the 2 s timeout; and stopping on the first
    non-OK response.
  - Recovery: power off completely (a full cold boot resets the SMU), unplug
    the drive and boot the known-good EFI. Record it as rule 6 requires.
- **A non-OK response** (`0xFE`, `0xFD`, `0xFC`, `0xFF`) or a timeout is a
  finding. Record it, and do not retry or send another message without a new
  review.
- **Mailbox contention.** If something else uses the mailbox, the busy check
  or an unexpected response shows it. Stop and re-review.

**Implementation (as built):**

- **Core:**
  - `writeAllowed(offset, value, stage)` now checks values. It allows
    `SCRATCH_REG0` from stage 6 and, from stage 7, `C2PMSG_90` ← 0,
    `C2PMSG_82` ← 0 and `C2PMSG_66` ← `0x2`/`0x3`. Nothing else in the mailbox
    page passes, including the PSP registers.
  - `checkSmu` reads `C2PMSG_66`/`82`/`90` and returns `smu-busy` if `_90` is 0.
  - `sendSmuQuery` does the following in order:
    - re-checks that the mailbox is idle;
    - writes response, argument, then message;
    - polls the response with up to 2000 pauses of 1 ms (`smu-timeout`);
    - reads the answer only after `0x1` (`smu-response-not-ok` otherwise).
  - `writeRegister` is still the only call of `write32`, now reached from five
    call sites.
- **Adapter:**
  - `accessDevice` takes the page to map writable: none, the scratch page, or
    the SMU page (BAR5 + `0x58000`). It refuses any other page.
  - `registerWrite` repeats the value allowlist and the page bounds before its
    single store.
  - The driver accepts a query (`kDiagnosticSmuQuery`, message `0x2` or `0x3`
    only) only after a passing `kDiagnosticSmuCheck` by the same connection.
    Any failure ends the sequence (`smu-out-of-order` until a new check).
- **Tool:** `cezanne-diag --smu-query` runs the check, then
  `GetDriverIfVersion`, then `GetSmuVersion`, printing and flushing each step
  first. It decodes the firmware version as `program.major.minor.debug`.
- **Tests:**
  - The value allowlist: every other message index and value, every other
    offset in the mailbox page, and refusal before stage 7.
  - Write order; a slow, a silent and a busy SMU; each error response with no
    answer read; a failed write.
  - Seven more weakened cores must fail: widened scratch offset, widened
    message values, widened zero values, widened page, busy check, timeout,
    response check.
  - `tests/test_kext.py` pins which operations get which writable page.
    `tests/test_diag_tool.py` pins the two message constants the tool can send.

**What it does not do:**

- It sends no message that sets clocks, power, GFXOFF or tables.
- It does not use the PSP or touch the SMN or RSMU mailbox.
- It writes no other register.
- The `SCRATCH_REG0` test is unchanged.

### Stage 8: disallow GFXOFF

**Status: approved by the user on 2026-10-05, implemented, and run
successfully in boot 10.** The proposal below is kept as approved. An
implementation section follows it.

**Purpose.** GFXOFF lets the SMU power the graphics block down while it is
idle. Every later step that programs the GC (golden settings, RLC, CP
microcode, rings) needs GFX to stay on. Linux makes sure of this by sending
`DisallowGfxOff`, for example before its GC IP dump. Stage 8 sends that
message once and confirms GFX stays on. It also proves the SMU path for a
message that sets a policy, not just a query.

**Linux v6.12:**

- **The message.** `renoir_ppt.c` maps `DisallowGfxOff` to
  `PPSMC_MSG_DisableGfxOff` = `0x8` (`smu_v12_0_ppsmc.h`), parameter 0.
  `smu_v12_0_gfx_off_control(smu, false)` sends it, then polls
  `SMUIO_GFX_MISC_CNTL` `PWR_GFXOFF_STATUS` until it is 2 (GFX on), for at
  most 500 ms.
- **Initial state.** `amdgpu_device_init` sets `gfx_off_req_count = 1`: Linux
  treats GFXOFF as disallowed at start and sends no message at init. It allows
  GFXOFF later (`AllowGfxOff`, `0x7`), and sends `DisallowGfxOff` only to undo
  that.

**What we have measured.** `PWR_GFXOFF_STATUS` was 2 (not in GFXOFF) in boots
7, 8 and 9, with GFX idle for minutes, so nothing has allowed GFXOFF. Sending
`DisallowGfxOff` therefore confirms the state the firmware is already in; it
is not expected to change behaviour. **This differs from Linux:** Linux never
sends this message from the initial state.

**What the stage 8 code would do.** It runs on request through `cezanne-diag
--gfxoff-disallow`, never at boot, with the boot unchanged from stage 7. Each
step is printed and flushed first:

1. **Check:** read the mailbox (stop with `smu-busy` unless `C2PMSG_90` is
   non-zero) and `PWR_GFXOFF_STATUS`.
2. **Send:** `DisableGfxOff` (`0x8`, argument 0), with the same writes, 2 s
   poll and stop-on-non-OK as the stage 7 queries.
3. **Confirm:** poll `PWR_GFXOFF_STATUS` until it is 2, for at most 500 ms
   (`gfxoff-timeout` otherwise), as Linux does.
4. **Dump:** the usual register dump.

**Confinement:**

- **The allowlist.** It grows by exactly one value, from stage 8: `C2PMSG_66` ←
  `0x8`. The other mailbox writes stay as in stage 7, and `AllowGfxOff`
  (`0x7`) and every other message stay refused.
- **One writable page.** The same SMU mailbox page as stage 7, and the same
  per-connection ordering.
- **Tests.** `0x8` is allowed only from stage 8, and `0x7` and every other
  message are refused. The tests also cover the status poll, its timeout and
  each error response. Weakened cores must fail: `0x7` allowed, a missing
  confirmation poll, a missing timeout.

**Expected result:**

- **Check:** idle mailbox, and status 2.
- **Send:** response `0x01`.
- **Confirm:** status 2 at once.
- **Dump:** matches boot 9, with the mailbox holding `0x8`/`0`/`0x1`.
- **Afterwards:** the machine stays as before: fans, temperatures, desktop.

**Risks and responses:**

- **A policy message.** Unlike stage 7, this message sets SMU state. It is the
  state already measured and the one Linux assumes at start, so no change is
  expected. A full power-off resets it in any case.
- **A non-OK response.** `0xFD` (bad prerequisites) is plausible if the
  firmware has not enabled the GFXOFF feature. That would be a finding: do
  not retry, and do not try `AllowGfxOff`.
- **A status other than 2 afterwards, or a timeout.** Unexpected; record it and
  stop. GC register reads were already gated on status 2 since stage 5.
- **Instability.** A hang, unusual fan behaviour or temperature changes: power
  off completely, unplug the drive and boot the known-good EFI.
- **The stage 7 reset.** One stage 7 boot attempt reset before macOS logged
  anything. It is unexplained and unrelated to the boot path stage 8 changes,
  but note the screen if it recurs.

**Implementation (as built):**

- **Core:**
  - `writeAllowed` adds `C2PMSG_66` ← `0x8` from stage 8; `0x7` and every
    other message stay refused.
  - The stage 7 send-and-poll code is now `sendSmuMessage`, shared by
    `sendSmuQuery` (`0x2`/`0x3`) and `disallowGfxOff`. It reads no answer
    unless asked, matching Linux's `read_arg` of `NULL`.
  - `disallowGfxOff` polls `PWR_GFXOFF_STATUS` for up to 500 pauses of 1 ms
    (`gfxoff-timeout`), and only after an OK response.
- **Adapter:** selector `kDiagnosticGfxOffDisallow`, accepted only after a
  passing `kDiagnosticSmuCheck` by the same connection, with the SMU page
  writable during it. Diagnostics version 4.
- **Tool:** `cezanne-diag --gfxoff-disallow` runs the check, then the message,
  printing each step first, and reports the response and the GFXOFF status.
- **Tests:**
  - `0x8` is allowed only from stage 8, `0x7` is refused, and the queries
    cannot send `0x8`.
  - GFX turning on after a delay, never turning on (timeout), a `0xFD` reply
    with no confirmation poll, and a busy mailbox.
  - Four more weakened cores must fail: widened message values, `0x7`
    allowed, a missing timeout, a missing status check.

**What it does not do:**

- It does not allow GFXOFF.
- It sends no clock, power-gating or table message.
- It writes no GC register.
- The scratch test and the version queries are unchanged.

### Stage 9: SMU metrics table

**Status: approved by the user on 2026-10-05, implemented, and booted in
boot 11, where it stopped at step 1 (`table-region-in-use`) before any
message.** The proposal below is kept as approved. The "all zero" criterion
proved unsuitable (see boot 11).

**Purpose.** Read live clocks, activity, voltages, currents, power and
temperatures from the SMU. On Renoir these exist only in the SMU's metrics
table, not in registers. Every later step can then be judged by measurement:
heat, clocks, throttling. This is also the first time the GPU side (the SMU)
**writes into memory**.

**Linux v6.12:**

- **Table and ID.** `SmuMetrics_t` (`smu12_driver_if.h`, 148 bytes). It holds:
  - 18 `ClockFrequency` entries, plus average GFX, SoC, VCN and fabric clocks
    and GFX and UVD activity;
  - VDD and SoC voltage, current and power, fan PWM and socket power;
  - per-core frequency, power and temperature for 8 cores, and L3 frequency
    and temperature;
  - GFX and SoC temperature, throttler status, STAPM limits, APU power, and
    TDC/EDC values.

  The table ID is `TABLE_SMU_METRICS` = 7. `renoir_ppt.c` places it in a
  `PAGE_SIZE`-aligned VRAM buffer (`AMDGPU_GEM_DOMAIN_VRAM`).
- **Setting the address.** `smu_v12_0_set_driver_table_location` sends
  `SetDriverDramAddrHigh` (`0x1A`) and `SetDriverDramAddrLow` (`0x1B`) with
  the buffer's GPU (MC) address. Linux does this once at hardware setup
  (`amdgpu_smu.c` `smu_set_driver_table_location`).
- **Reading the table.** `smu_cmn_update_table(..., drv2smu = false)` sends
  `TransferTableSmu2Dram` (`0x1C`) with argument `table_id | (0 << 16)` = 7.
  It then invalidates HDP and copies the buffer.
- **Where VRAM is.** For this APU, VRAM is the carveout. GPU address
  `0xF400000000` + *offset* (MMHUB `MC_VM_FB_LOCATION_BASE` `0xf400`, stage
  5) is CPU physical `0x5C0000000` + *offset* (`MC_VM_FB_OFFSET` `0x5c0`,
  stage 2). That is how `gmc_v9_0_mc_init` sets `aper_base` for APUs.

**Where the table goes.** There is no VRAM allocator yet, so stage 9 uses one
fixed 4 KiB page in the middle of the carveout: offset `0x40000000` (1 GiB),
GPU address `0xF440000000`, CPU physical `0x600000000`.

- It is far from the regions known to be in use:
  - the boot framebuffer at the start of VRAM (`IONDRVFramebuffer`, about
    8 MB);
  - the discovery binary in the top 64 KiB;
  - the firmware-reserved and PSP regions, which Linux places near the top.
- **Not proven unused.** Linux would find this out from the VBIOS
  firmware-usage table, which this driver does not read. The checks below
  look for any sign that the page is in use, and stop if they find one.

**What the stage 9 code would do.** It runs on request through `cezanne-diag
--smu-metrics`, never at boot, with the boot unchanged from stage 8. Each step
is printed and flushed first:

1. **Check:**
   - per-read device checks; the mailbox is idle;
   - `MC_VM_FB_LOCATION_BASE` reads `0xf400` and `MC_VM_FB_OFFSET` reads
     `0x5c0`, otherwise the fixed addresses would be wrong;
   - the GPU and CPU addresses fall inside the carveout and outside every
     BAR, the boot framebuffer and the discovery binary;
   - the 64 KiB around the page reads all zero, twice, 1 ms apart, through a
     read-only uncached mapping (`table-region-in-use` otherwise).

   It keeps a copy of the page.
2. **Set the address:** `SetDriverDramAddrHigh` (`0xF4`), then
   `SetDriverDramAddrLow` (`0x40000000`), each with the stage 7 send, poll and
   stop-on-non-OK.
3. **Transfer:** `TransferTableSmu2Dram` (argument 7).
4. **Read and verify:**
   - read the page through the read-only uncached mapping;
   - require that bytes 148–4095 are unchanged and bytes 0–147 are not all
     zero (`table-not-written` or `table-overflow` otherwise);
   - copy the 148 bytes out.
5. **Show:** decoded values, including clocks in MHz, temperatures in °C,
   power in mW/W, and throttler bits.

**Differences from Linux:**

- **The address.** Linux takes it from its VRAM allocator; stage 9 uses a
  fixed, checked page.
- **HDP.** Linux invalidates HDP before copying. HDP is the host data path for
  CPU access through the BAR. Stage 9 reads the carveout directly by physical
  address, uncached, not through BAR0, so HDP is not involved and no HDP
  register is written.
- **Repeating.** Linux sets the address once per boot. Stage 9 sends both
  address messages on every `--smu-metrics` run, so each run stands alone.
  The values are the same each time.

**Confinement:**

- **The allowlist** grows by exact values, from stage 9:
  - `C2PMSG_66` ← `0x1A`, `0x1B`, `0x1C`;
  - `C2PMSG_82` ← `0xF4`, `0x40000000` or `7`, each only with its own message.
    The core pairs the argument with the message before writing.

  `TransferTableDram2Smu` (`0x1D`) and every other table and message stay
  refused.
- **No CPU writes to memory.** The CPU never writes the table page. Its only
  mapping is read-only (`IODeviceMemory::withRange` at `0x600000000`, 4 KiB,
  `kIOMapReadOnly | kIOMapInhibitCache`), plus one read-only 64 KiB mapping
  for the check.
- **Register writes** stay limited to the SMU mailbox page, with the same
  per-connection ordering as stages 7 and 8.
- **Tests:**
  - each message and argument pair, and refusal of `0x1D` and other tables;
  - the address arithmetic and the inside-carveout and outside-BAR checks;
  - a non-zero region, an unchanged page and writes past 148 bytes;
  - decoding, against a synthetic table.

  Weakened cores must fail: unpaired arguments, a missing in-use check, a
  missing overflow check.

**Expected result:**

- **The region:** all zero before.
- **The messages:** responses `0x01`.
- **The table:** bytes 0–147 change, the rest of the page stays zero.
- **The values:** plausible readings, for example:
  - `GfxTemperature` and `SocTemperature` near the "Hot" readings;
  - eight `CoreFrequency` entries in the CPU's range, for the 5600GT's six
    cores plus two absent;
  - `AverageGfxActivity` near 0.
- **Afterwards:** the mailbox holds `0x1C`/`7`/`0x1`, and the machine behaves
  as before.

**Risks and responses:**

- **Memory corruption (the main risk).** If the address translation were
  wrong, the SMU would write 148 bytes somewhere else in physical memory,
  possibly into macOS's memory, which could crash it or corrupt data.
  - Mitigations:
    - the translation is the one Linux uses for APUs, from two registers
      measured in every boot;
    - the target lies inside the reserved carveout, which macOS does not
      use;
    - the write is only 148 bytes;
    - step 4 confirms the bytes landed exactly where expected.
  - If step 4 reports `table-not-written`, assume they landed elsewhere: save
    nothing, power off at once and boot the known-good EFI.
  - **Make a Time Machine backup before this boot.**
- **The page is in use after all.** The zero check makes this unlikely, but
  the SMU's 148 bytes would overwrite whatever was there, inside the carveout.
  The GPU and display are not running, so the likely effect is none or a
  visual glitch. A power-off clears it.
- **A non-OK response or a timeout.** Record it and stop. Do not retry with a
  different address.
- **Persistence.** The SMU keeps the table address until power-off. Nothing in
  the test boot uses it; a full power-off resets it before the known-good
  boot, where NootedRed sets its own.

**Implementation (as built):**

- **Core:**
  - `smuArgumentAllowed(message, argument, stage)` pairs every SMU message with
    its exact argument. `sendSmuMessage` checks the pair before any write.
  - `writeAllowed` adds `C2PMSG_66` ← `0x1A`/`0x1B`/`0x1C` and `C2PMSG_82` ←
    `0xF4`/`0x40000000`/`7`, from stage 9.
  - `checkMetricsTarget`:
    - reads MMHUB `MC_VM_FB_LOCATION_BASE`, `MC_VM_FB_OFFSET` and
      `RCC_CONFIG_MEMSIZE`;
    - requires `0xf400` and `0x5c0`;
    - keeps the 64 KiB check region clear of the carveout's lowest and highest
      64 MiB and of every device range.
  - `checkRegionUnused` reads the region twice through a read callback, a
    pause apart.
  - `requestMetrics` sends the three messages in order and stops at the first
    failure.
  - `verifyMetricsPage` requires bytes 148–4095 to be zero and bytes 0–147
    not all zero, then decodes 74 words (`SmuMetrics`, word indices
    `kMetrics*`).
- **Adapter:**
  - Three selectors, accepted only in order per connection: check (needs the
    idle mailbox and the target and zero checks), transfer (SMU page
    writable), and read.
  - Carveout memory is mapped only at `kMetricsPhysical`, 64 KiB for the check
    or 4 KiB for the read, `kIOMapReadOnly | kIOMapInhibitCache`, and released
    at once. The table goes to the tool as a fixed 148-byte structure.
  - Diagnostics version 5.
- **Tool:** `cezanne-diag --smu-metrics` runs the three steps, printing each
  first. It prints the clocks by name in MHz, the average clocks, activity,
  VDD/SoC voltage, current and power, and socket and APU power. It also
  prints each core's MHz, mW and °C, L3, GFX and SoC temperatures, throttler
  status, and the STAPM, TDC and EDC values.
- **Tests:**
  - Message and argument pairs, including every other table ID and `0x1D`.
  - The address arithmetic, the register checks, the carveout bounds and
    device-range overlap.
  - The zero check: a dirty last word and a failed read.
  - Three messages with exact arguments, and stopping after a failure.
  - The fake SMU writes only to the address it was given; the tests cover
    verification, overflow and an unwritten page.
  - Three more weakened cores must fail: an unpaired argument, the in-use
    check removed, the overflow check removed.
  - The compiler's `bzero` for the zero-initialised table buffer is the one
    new direct call.

**What it does not do:**

- It sends no table to the SMU (`Dram2Smu`) and no clock, power or watermark
  message.
- It does not allocate VRAM, set up GART or touch display memory.
- It writes no GC or HDP register.

Stage 4 risks: it adds a kernel entry point. It is limited to root and to the
reads stages 1–3 already made, but a defect in the user client could panic
the kernel. Reads happen while the system runs, still with no graphics
driver; the per-read checks stop on a device that left D0 or stopped decoding.
Do not sleep the machine.

### Stage 10: PSP and memory-aperture state (proposal)

**Status: proposed 2026-10-06, approved by the user the same day, and
implemented as proposed; succeeded in boot 14.** The heading keeps "proposal" so
links stay stable.

**Purpose.** Prepare the first firmware load. Linux v6.12 loads every
Renoir/Green Sardine engine firmware (SDMA, CP, RLC, …) through the PSP
(`amdgpu_ucode_get_load_type`: the default `-1` gives `AMDGPU_FW_LOAD_PSP`).
The first useful engine is SDMA, which gives the "verified DMA copy and
fence" milestone. Before any PSP write, stage 10 reads the PSP mailbox
registers that the PSP route writes. It also reads the address apertures that
decide which GPU addresses the PSP and engines can reach. Like stage 5, it
only reads.

**The PSP route in Linux v6.12** (`amdgpu_psp.c` `psp_hw_start`,
`psp_v12_0.c`), for orientation only. Each step will get its own proposal:

1. The bootloader steps are skipped: `C2PMSG_81` is non-zero (stage 5:
   the secure OS is already running).
2. `psp_v12_0_ring_create` first sends two `GFX_CTRL_CMD_ID_GBR_IH_SET`
   commands (`C2PMSG_69`/`70`, then `C2PMSG_64`). It then creates the
   kernel-mode ring: its GPU address goes in `C2PMSG_69`/`70`, its size in
   `C2PMSG_71`, and the command in `C2PMSG_64`. It waits for bit 31 of
   `C2PMSG_64`. The write pointer is `C2PMSG_67`.
3. A trusted memory region (TMR) is reserved in VRAM and handed to the PSP
   with a ring command (`psp_tmr_init`, `psp_tmr_load`).
4. Each firmware image is copied to a 1 MiB buffer and loaded with a ring
   command (`psp_load_non_psp_fw`). SDMA would come first.

Stage 10 tells us whether step 2 starts from a clean mailbox. It also shows
whether something (firmware, GOP) already created a ring after a cold boot.

**Registers.** These are read only on demand through `cezanne-diag`, never
at boot. Offsets come from `mp_12_0_0_offset.h`, `mmhub_1_0_offset.h` and
`gc_9_0_offset.h`, with the measured bases (MP0 `0x16000`, MMHUB `0x1A000`,
GC `0x2000`). All lie inside BAR5.

| Register | BAR5 byte offset | Linux v6.12 use |
| --- | --- | --- |
| `MP0_SMN_C2PMSG_36` | `0x58190` | psp_v12_0 bootloader: firmware address |
| `MP0_SMN_C2PMSG_64` | `0x58200` | psp_v12_0 ring command and response (bit 31 ready) |
| `MP0_SMN_C2PMSG_67` | `0x5820c` | psp_v12_0 ring write pointer |
| `MP0_SMN_C2PMSG_69` | `0x58214` | psp_v12_0 ring address low / IH reroute argument |
| `MP0_SMN_C2PMSG_70` | `0x58218` | psp_v12_0 ring address high / IH reroute argument |
| `MP0_SMN_C2PMSG_71` | `0x5821c` | psp_v12_0 ring size |
| `MC_VM_FB_OFFSET (MMHUB)` | `0x6a05c` | mmhub_v1_0 (compare with GC hub, stage 2) |
| `MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB (MMHUB)` | `0x6a060` | mmhub_v1_0_init_system_aperture_regs |
| `MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB (MMHUB)` | `0x6a064` | mmhub_v1_0_init_system_aperture_regs |
| `MC_VM_AGP_TOP (MMHUB)` | `0x6a0b8` | mmhub_v1_0_init_system_aperture_regs |
| `MC_VM_AGP_BOT (MMHUB)` | `0x6a0bc` | mmhub_v1_0_init_system_aperture_regs |
| `MC_VM_AGP_BASE (MMHUB)` | `0x6a0c0` | mmhub_v1_0_init_system_aperture_regs |
| `MC_VM_SYSTEM_APERTURE_LOW_ADDR (MMHUB)` | `0x6a0c4` | mmhub_v1_0_init_system_aperture_regs |
| `MC_VM_SYSTEM_APERTURE_HIGH_ADDR (MMHUB)` | `0x6a0c8` | mmhub_v1_0_init_system_aperture_regs (Renoir/Green Sardine +1 workaround) |
| `MC_VM_FB_LOCATION_BASE (GC)` | `0x0a600` | gfxhub_v1_0 (GFX-gated) |
| `MC_VM_FB_LOCATION_TOP (GC)` | `0x0a604` | gfxhub_v1_0 (GFX-gated) |
| `MC_VM_AGP_TOP (GC)` | `0x0a608` | gfxhub_v1_0_init_system_aperture_regs (GFX-gated) |
| `MC_VM_AGP_BOT (GC)` | `0x0a60c` | gfxhub_v1_0_init_system_aperture_regs (GFX-gated) |
| `MC_VM_AGP_BASE (GC)` | `0x0a610` | gfxhub_v1_0_init_system_aperture_regs (GFX-gated) |
| `MC_VM_SYSTEM_APERTURE_LOW_ADDR (GC)` | `0x0a614` | gfxhub_v1_0_init_system_aperture_regs (GFX-gated) |
| `MC_VM_SYSTEM_APERTURE_HIGH_ADDR (GC)` | `0x0a618` | gfxhub_v1_0_init_system_aperture_regs (GFX-gated) |

The GC-hub registers use the stage 5 GFX gate (read only while
`PWR_GFXOFF_STATUS` is 2). Stage 8's `DisallowGfxOff` keeps GFX on if run
first.

**Changes:**

- **Core.** `kMaxStage` 10. A stage 10 list that extends the stage 6 list by
  these 21 registers. The 7 GC-hub registers are added to the GFX-gated set.
  No write, message or memory access changes; the write allowlist is
  unchanged.
- **Adapter.** None beyond the stage limit. The boot runs stages 1–3 as
  before.
- **Tool.** `cezanne-diag` names the new registers. A `--psp-state` summary
  decodes `C2PMSG_64` (bit 31 ready, low 16 bits status) and whether a ring
  address or size is set. It also shows the MMHUB and GC apertures as GPU
  address ranges.
- **Tests.** The stage 10 list is a prefix-extension of the stage 6 list;
  offsets are aligned, unique and inside BAR5. The new registers are refused
  at stage 9. The GC-hub registers are gated and the others are not. Stage
  11 is rejected. Two weakened cores must fail: stage 10 registers allowed at
  stage 9, and the GC-hub gate removed.

**Expected values.** Stages 1–9 as in boot 13.

- `C2PMSG_64`: bit 31 set with status 0, meaning the secure OS accepts ring
  commands (Linux `psp_v12_0_mode1_reset` waits for exactly that).
- `C2PMSG_69`/`70`/`71`/`67`: no prediction. Zero would mean no ring.
- MMHUB FB offset `0x5c0`, the same as the GC hub.
- The system aperture should cover the FB range `0xF400000000`–`0xF47FFFFFFF`.
  The SMU metrics write in boot 13 suggests the MMHUB does.

**What the values decide.** If the mailbox is idle and no ring exists, the
next proposal (stage 11) would be the two IH reroute commands plus creating
and destroying a kernel-mode ring in one fixed carveout page, with no
firmware loaded. A ring that already exists, or a status other than ready,
means stage 11 must be designed around that state first.

**Risks.** As in stage 5: every register is a mailbox or configuration
register that Linux reads or polls. None is a FIFO, data port or
clear-on-read counter. A read that hangs freezes the machine with the
register's name on screen; power off and boot the known-good EFI. Nothing is
written.

### Stage 11: create and destroy a PSP ring (proposal)

**Status: proposed 2026-10-06, approved by the user the same day, and
implemented. The first build stopped safely in boot 15. Revised, it
succeeded in boot 16.** The proposal is kept as approved. The
implementation notes follow it.

**Purpose.** The first commands to the PSP, and the first step of the route
by which Linux loads all engine firmware. Stage 11 creates the PSP's
kernel-mode command ring in one checked carveout page, confirms the PSP
accepted it, and destroys it again. It submits no ring frame and loads no
firmware. A later stage (TMR setup, then SDMA firmware) needs a working ring.
Stage 10 (boot 14) showed the starting state: the PSP is ready
(`C2PMSG_64` `0x80000000`) and no ring exists.

**Linux v6.12:**

- `psp_v12_0_ring_create` (non-SR-IOV path), called from `psp_hw_start`
  once `C2PMSG_81` shows the secure OS running:
  1. **`psp_v12_0_reroute_ih`**: two `GFX_CTRL_CMD_ID_GBR_IH_SET`
     (`0x00080000`, `psp_gfx_if.h`) commands. For each, it writes the client
     to `C2PMSG_69` and an `IH_CLIENT_CFG_DATA` value to `C2PMSG_70`, writes
     the command to `C2PMSG_64`, waits 20 ms, then polls `C2PMSG_64` until
     `(value & 0x8000FFFF) == 0x80000000`:
     - VMC: client 3, value `0x0015244b` (credit return address `0x1244b`,
       client type 1, ring ID 1; field shifts from `osssys_4_0_sh_mask.h`);
     - UMC: client 4, value `0x0011216b` (`0x1216b`, ring ID 1).
     Linux ignores both results.
  2. **Ring create.** Ring address low to `C2PMSG_69`, high to `C2PMSG_70`,
     size to `C2PMSG_71`, and `PSP_RING_TYPE__KM << 16` = `0x00020000`
     (`GFX_CTRL_CMD_ID_INIT_GPCOM_RING`) to `C2PMSG_64`. Then 20 ms and the
     same poll.
- `psp_ring_init`: the ring is one 4 KiB page (`ring_size = 0x1000`) of
  VRAM, page-aligned.
- `psp_v12_0_ring_stop`: `GFX_CTRL_CMD_ID_DESTROY_RINGS` (`0x00030000`) to
  `C2PMSG_64`, 20 ms, then poll for bit 31 alone.
- `psp_wait_for` polls every 1 µs for `adev->usec_timeout`, which is
  `AMDGPU_MAX_USEC_TIMEOUT` = 100 ms (`amdgpu.h`).
- The write pointer (`C2PMSG_67`) changes only when a frame is submitted
  (`psp_ring_cmd_submit`). Stage 11 never writes it.

**Where the ring goes.** It goes at carveout offset `0x40100000`: GPU
`0xF440100000`, physical `0x600100000`.

- This is 1 MiB above the stage 9 metrics page and inside the checked
  middle of the carveout, far from the boot framebuffer at offset 0 and the
  firmware, PSP and discovery regions at the top.
- Both hubs map it (boot 14: FB and system aperture `0xF400000000`–
  `0xF47FFFFFFF`).
- The CPU never writes the page. It is checked like the stage 9 page: the
  64 KiB at it must read the same twice about 1 s apart, and a snapshot is
  kept.

**Steps** (on request only, `sudo cezanne-diag --psp-ring`, each printed
before it runs; ordered selectors as in stage 9):

1. **Check (no writes).** Stop with a named status unless all of these hold:
   - `C2PMSG_81` is non-zero;
   - `C2PMSG_64` is exactly `0x80000000`;
   - `C2PMSG_67`, `69`, `70` and `71` are 0;
   - the stage 9 FB checks hold (MMHUB FB base `0xf400`, FB offset `0x5c0`),
     the page is inside the carveout away from its reserved ends, and it
     overlaps no device range;
   - the 64 KiB is stable over about 1 s.
2. **IH reroute.** Two `GBR_IH_SET` commands with Linux's exact values, each
   followed by a 20 ms sleep and a poll of up to 100 ms in 1 ms sleeps.
   Stop unless each response is exactly `0x80000000` (flag set, status 0).
   Unlike Linux, which ignores the result, any other value is a stop.
3. **Create.** `C2PMSG_69` ← `0x40100000`, `C2PMSG_70` ← `0xF4`,
   `C2PMSG_71` ← `0x1000`, `C2PMSG_64` ← `0x00020000`, then 20 ms and the
   same poll. It is ok only on exactly `0x80000000`.
4. **Observe (no writes).** Read `C2PMSG_64`, `67`, `69`, `70` and `71`.
   Compare the page and the rest of its 64 KiB with the snapshot through a
   read-only mapping, and report which bytes changed. A change outside the
   ring page is a finding, as is any change in the page: the PSP is not
   expected to write the ring until a frame is submitted.
5. **Destroy.** `C2PMSG_64` ← `0x00030000`, then 20 ms and a poll for bit 31
   of up to 100 ms. Report the final `C2PMSG_64`, `67`, `69`, `70` and `71`.

If the tool exits, or the connection closes, after a successful create but
before destroy, the driver sends the destroy itself, as stage 6 restores the
scratch register. If step 2 or 3 fails, nothing more is sent. The ring may
then be half-configured until the next cold boot; that is recorded as a
finding.

**Write allowlist additions** (stage 11 and up). Every write goes to the
MP0 mailbox, in the BAR5 page `0x58000` already used for the SMU, so no new
writable mapping is needed.

| Register | Values |
| --- | --- |
| `C2PMSG_64` | `0x00080000`, `0x00020000`, `0x00030000` |
| `C2PMSG_69` | `3`, `4`, `0x40100000` |
| `C2PMSG_70` | `0x0015244b`, `0x0011216b`, `0xF4` |
| `C2PMSG_71` | `0x1000` |

A pairing check, like stage 9's `smuArgumentAllowed`, ties each command to
its arguments:
- `GBR_IH_SET` is allowed only with (3, `0x15244b`) or (4, `0x11216b`) in
  `69`/`70`;
- `INIT_GPCOM_RING` only with (`0x40100000`, `0xF4`, `0x1000`);
- `DESTROY_RINGS` only after a successful create on the same connection.

**Changes:**

- **Core:**
  - `kMaxStage` 11, plus the ring constants.
  - The allowlist and pairing check; `checkPspRing`, `pspReroute`,
    `pspCreateRing` and `pspDestroyRing`.
  - A poll helper with a 20 ms settle time and a 100-pause limit.
  - The page compare reuses the stage 9 snapshot code.
- **Adapter:** four selectors (check, reroute plus create, observe, destroy),
  accepted only in order per connection, and the destroy on abandon.
  Read-only carveout mapping at the ring page only. Diagnostics version 6.
- **Tool:** `--psp-ring`, which prints each step and the mailbox values.
- **Tests:**
  - Exact write sequences against a fake PSP, including every value and
    order.
  - A stop at each check and after a non-ok or timed-out response.
  - The destroy on abandon only after a create.
  - The pairing check: a create with a reroute argument, a reroute with ring
    arguments, a destroy without a create.
  - Weakened cores that must fail: an unpaired create, a removed ready
    check, a removed timeout, a destroy without a create.
  - Stage 12 rejected.

**Expected values.**
- Responses of `0x80000000` for both reroutes, the create and the destroy.
- After create: `C2PMSG_69`/`70`/`71` hold whatever the PSP leaves there (no
  prediction), and `C2PMSG_67` is 0.
- No change in the 64 KiB region.

**Risks.**

- These are the first writes to the PSP, the security processor. A PSP that
  hangs or rejects commands could stop the GPU working until a cold boot.
  The display is still the firmware's framebuffer and does not depend on the
  PSP, but that is untested.
- An unexpected response stops the sequence. A poll that times out after
  step 2 or 3 leaves the PSP in an unknown state: shut down fully and boot
  the known-good EFI.
- If the PSP wrote somewhere other than the page, the compare in step 4
  could catch it only inside the 64 KiB. Treat any change outside the ring
  page as a misdirected write: power off at once.
- Make a Time Machine backup before this boot, as for stage 9.

**What it does not do.** It writes no ring frame and no write pointer. It
sends no TMR, firmware load or mode 1 reset. It makes no IH, GART or default
page change.

**Implementation notes:**

- **Core:**
  - `pspCommandAllowed` holds the pairs. `sendPspCommand` checks the pair,
    reads `C2PMSG_64` (it must be exactly ready), writes the arguments
    (`69`/`70`, and `71` for the create; none for the destroy), then the
    command.
  - It then pauses 20 times and polls up to 100 times in 1 ms pauses for
    bit 31 (`psp-timeout`). The response must be exactly `0x80000000`
    (`psp-response-not-ok`).
  - `createPspRing` re-checks the idle mailbox before the first command.
  - `destroyPspRing` refuses unless told a ring was created
    (`psp-out-of-order`).
  - `compareRegion` counts changed words in the ring page and in the 60 KiB
    after it.
  - The stage 9 page checks are shared (`checkCarveoutPage`), so a failed
    address or range check reports `metrics-address-mismatch` or
    `metrics-target-invalid`. An unstable region reports
    `table-region-in-use`.
- **Adapter:**
  - Selectors 11–14 (check, create, observe, destroy). Diagnostics version
    6.
  - Create needs a passing check on the same connection, and observe needs
    a create.
  - Destroy is accepted after a create, with or without observe, so a
    failed observe cannot block it.
  - A new check is refused while a ring this driver created exists.
  - Closing the connection after a create sends the destroy and publishes
    `CezanneGPU PSP ring abandoned destroy`.
  - Carveout memory is mapped read-only only at the metrics or ring page,
    `withCarveoutMemory`.
- **Tool:** `--psp-ring` prints each step first and then the mailbox
  values. It sends the destroy after any successful create, even if the
  observe step fails.
- **Tests:**
  - Exact write sequences (10 writes for the create, 1 for the destroy) and
    the settle pause count.
  - A stop on status 5 after one command, a timeout after 20 + 100 pauses,
    and a refused create when a ring address is set.
  - `not-ready` before a destroy, the pairs, the values and their stages,
    and the compare counts.
  - Five more weakened cores must fail: an unpaired create, the ready check
    removed, the timeout removed, the destroy order check removed, and the
    ring-exists check removed.

### Revision: stage 11 PSP responses (proposal)

**Status: proposed 2026-10-06 after boot 15, approved by the user the same
day, and implemented; succeeded in boot 16.** The heading keeps "proposal" so
links stay stable.

**One deliberate difference from the text below.** Before each command, the
ready check requires only bit 31 (the PSP has answered its previous
command), whatever that answer's status. Requiring status 0 there would
block the destroy after a create the PSP rejected, which this revision
exists to guarantee. Linux checks nothing before writing. The clean-start
check before the create still requires
`(C2PMSG_64 & 0x8000FFFF) == 0x80000000`.

**Implementation notes:**

- **Core:**
  - `createPspRing` sends only `INIT_GPCOM_RING` and reports `written`.
  - Responses are judged with `kPspResponseMask` (`0x8000FFFF`).
  - `GBR_IH_SET` and the IH values are gone from `pspCommandAllowed` and
    `writeAllowed`.
- **Adapter:**
  - It tracks the ring from `written`. The create selector returns status,
    response and written.
  - Diagnostics version 7.
- **Tool:** after a written create it always runs the destroy. It skips
  observe if the create was not ok.
- **Tests:**
  - A create answered `0x80020000` is ok. `0x80020100` is not ok, but is
    written and still destroyed.
  - A timeout is still written.
  - The ready check accepts an echoed ID with status 0 and refuses
    `0x80080100` at the clean-start check.
  - Two new weakened cores fail: an exact-match response, and `written`
    never set.

**What boot 15 showed.** The first `GBR_IH_SET` (VMC) was answered
`0x80080100`:

- bit 31 set (answered);
- bits 19:16 = `0x8`, the command ID echoed back (`psp_gfx_if.h` defines
  `GFX_CMD_ID_MASK` `0x000F0000`);
- status `0x0100` = `PSP_ERR_UNKNOWN_COMMAND`.

This secure OS does not know `GBR_IH_SET`. The core stopped there, as
designed. Nothing more was sent, no ring was created, and no destroy was
needed.

**What Linux v6.12 would do with the same firmware.**

- `psp_wait_for(..., 0x80000000, 0x8000FFFF)` would read `0x80000100`, wait
  the full 100 ms and time out. `psp_v12_0_reroute_ih` ignores the result, so
  it would send the UMC reroute (same outcome) and then create the ring
  regardless.
- Linux's effective behaviour on this host is therefore: two rejected
  commands, then the create.

**Two defects this exposes in stage 11 as built:**

1. **The response check is stricter than Linux's.** It requires
   `C2PMSG_64` to be exactly `0x80000000`. The PSP echoes the command ID in
   bits 19:16, and Linux masks them out with `0x8000FFFF`. A successful
   create would probably read `0x80020000`, which stage 11 would call
   `psp-response-not-ok`.
2. **That misreport would leave an untracked ring.** The driver tracks a
   ring only after an ok create. A successful create misreported as
   not-ok would leave a ring the driver neither destroys nor refuses to
   re-check.

**Proposed changes:**

- **Judge readiness and responses as Linux does:**
  `(C2PMSG_64 & 0x8000FFFF) == 0x80000000`. The tool still prints the whole
  value. The no-ring check stays (`C2PMSG_67`/`69`/`70`/`71` all 0).
- **Drop the two `GBR_IH_SET` commands.** This firmware rejects them, and
  Linux continues as if they had not been sent. They only steer IH routing,
  which no stage uses yet; revisit them when interrupts are proposed. The
  `C2PMSG_64` ← `0x00080000` value and the IH reroute values leave the write
  allowlist.
- **Track the ring from the moment the create command is written**, not
  only after an ok response. Any create attempt then gets a destroy, from the
  tool or on abandon. `DESTROY_RINGS` on a PSP without a ring is expected to
  be answered with an error status, which is recorded and harmless.
- Tests: a create answered `0x80020000` is ok. One answered `0x80020100` is
  `psp-response-not-ok` but still tracked and destroyed. No `GBR_IH_SET`
  write remains. Two new weakened cores must fail: the response mask
  replaced by an exact match, and create tracking moved back after the
  response.

**Steps after the revision:**
1. Check, as before.
2. Create: `C2PMSG_69` ← `0x40100000`, `C2PMSG_70` ← `0xF4`,
   `C2PMSG_71` ← `0x1000`, `C2PMSG_64` ← `0x00020000`.
3. Observe.
4. Destroy.

That is four writes to the PSP in total, all values approved for stage 11.
The risks are as in the stage 11 section. The boot must be a cold boot, so
the PSP starts from the boot 14 state, not boot 15's rejected command.

### Stage 12: first PSP ring frame, TMR setup (proposal)

**Status: proposed 2026-10-06, approved by the user the same day,
implemented, and succeeded in boot 17.** The proposal is kept as approved.
The implementation notes follow it.

**Purpose.** Submit the first command through the ring stage 11 proved: set
up the PSP's trusted memory region (TMR). Linux v6.12 does this right after
creating the ring, and every firmware load after it (SDMA first) needs it.
It is also the first time:
- the CPU writes GPU memory (the command buffer, fence and ring frame);
- the PSP reads a command from memory and writes a result back.

**Linux v6.12 for this host** (MP0 12.0.1):

- **Setup.** `psp_early_init` sets `autoload_supported = false` and
  `boot_time_tmr = false`, so `psp_hw_start` runs `psp_tmr_init`. With no
  TOC for this firmware, that allocates `PSP_TMR_SIZE` = 4 MiB in VRAM,
  aligned to `PSP_TMR_ALIGNMENT` (1 MiB). The comment there says the PSP
  prefers natural alignment, meaning aligned to the size itself.
- **The command.** `psp_tmr_load` → `psp_prep_tmr_cmd_buf` builds
  `GFX_CMD_ID_SETUP_TMR` (5) with these fields:
  - `buf_phy_addr_lo`/`hi` = the TMR's GPU (MC) address;
  - `buf_size` = 4 MiB;
  - `tmr_flags` bit 1 (`virt_phy_addr`) = 1;
  - `system_phy_addr_lo`/`hi` = `amdgpu_gmc_vram_pa`, i.e. MC address −
    `vram_start` (`0xF400000000`) + `vram_base_offset` (`0x5C0000000`,
    from `MC_VM_FB_OFFSET`, `gmc_v9_0.c`).
- **Submitting it.** `psp_cmd_submit_buf` → `psp_ring_cmd_submit`:
  1. zero the 4 KiB command buffer and copy in the 1024-byte
     `psp_gfx_cmd_resp`. Linux sets only `cmd_id` and the command fields;
     `buf_size` and `buf_version` stay 0.
  2. Take the next fence value (1 for the first command).
  3. At the current write pointer (`C2PMSG_67`, in dwords), zero one 64-byte
     `psp_gfx_rb_frame` and fill in the command buffer address, the fence
     address and the fence value. `cmd_buf_size` stays 0.
  4. Advance the write pointer by 16 dwords, modulo 1024, and write it to
     `C2PMSG_67`.
  5. Poll the fence buffer's first dword until it equals the fence value,
     up to `psp_timeout` = 20000 tries 10–100 µs apart. Then read the
     response at command buffer offset 864 (`psp_gfx_resp.status`). A
     non-zero status is only a warning in Linux.
- **No cache maintenance on this host.** On x86-64 APUs,
  `amdgpu_device_flush_hdp` and `amdgpu_device_invalidate_hdp` return
  immediately, so the CPU and PSP share the carveout with no HDP
  maintenance.
- **Teardown.** `psp_hw_fini` sends `GFX_CMD_ID_DESTROY_TMR` (7, no
  fields) through the ring, then destroys the ring.

**Placement** (carveout offsets; GPU address = `0xF400000000` + offset,
physical = `0x5C0000000` + offset):

| Buffer | Offset | Size | GPU address |
| --- | --- | --- | --- |
| Ring (stage 11) | `0x40100000` | 4 KiB | `0xF440100000` |
| Command buffer | `0x40101000` | 4 KiB | `0xF440101000` |
| Fence buffer | `0x40102000` | 4 KiB | `0xF440102000` |
| TMR | `0x40400000` | 4 MiB (4 MiB-aligned) | `0xF440400000` |

All of these lie inside the 64 KiB stage 11 checks, or in a 4 MiB region
starting 3 MiB above it. All are in the middle of the carveout, clear of
the stage 9 metrics page.

**Steps** (on request only, `sudo cezanne-diag --psp-tmr`, each printed
first; ordered selectors):

1. **Check (no writes).**
   - The stage 11 check: secure OS running, PSP ready, no ring, and the
     64 KiB at the ring stable over about 1 s, snapshotted.
   - The TMR region inside the carveout and outside every device range.
   - The 4 MiB TMR region read twice about 1 s apart, with a checksum
     instead of a snapshot; it must not change.
2. **Create the ring**, exactly as stage 11.
3. **Write the command.** These are the first CPU writes to carveout memory,
   through one new writable, uncached mapping of the three pages at
   `0x40100000`–`0x40102FFF`, and nowhere else:
   - zero the command and fence pages, and frame 0 (64 bytes);
   - fill the command buffer: `cmd_id` 5, then `0x40400000`, `0xF4`,
     `0x00400000`, flags `0x2`, and `0x00400000`, `0x6` (physical
     `0x600400000`);
   - fill frame 0: command buffer `0xF440101000`, fence `0xF440102000`,
     fence value 1;
   - read all three pages back, and stop unless they match exactly.
4. **Submit.** `C2PMSG_67` ← `16`. Then poll the fence dword every 1 ms, up
   to 2000 times (2 s, Linux's upper bound).
5. **Result.** Read the response status (offset 864) and `fw_addr`/`tmr_size`
   for the record. Compare the 64 KiB around the ring with the snapshot.
   Only the bytes this step wrote may differ, plus the 96-byte response area
   (`+864`–`+959`) and the fence dword.
6. **Teardown.**
   - `DESTROY_TMR` as frame 1: `cmd_id` 7, fence value 2, then
     `C2PMSG_67` ← `32`, and the same fence wait.
   - Then the stage 11 `DESTROY_RINGS`.
   - If the connection closes after step 4, the driver does the same
     teardown itself.

**The CPU never reads or writes the TMR after step 1.** Once it is set up,
the PSP may protect it; it is reserved until the next cold boot.

**New writes:**

| Target | Values |
| --- | --- |
| `C2PMSG_67` (write pointer) | `16`, `32` only, in that order after a create |
| Memory | only the three pages at physical `0x600100000`–`0x600102FFF`, with exactly the contents above (frames 0 and 1, the two command buffers, zeroed fence) |

Everything else (the ring create and destroy values) is stage 11's.

**Changes:**

- **Core:**
  - A `MemoryWriter` callback.
  - `buildSetupTmr`, `buildDestroyTmr` and `buildFrame`, which produce exact
    word images that the tests check.
  - `submitPspFrame` (write pointer and fence poll), `checkTmrRegion` (a
    stability checksum), and the response decode.
  - The memory-write allowlist (the three pages) and the write-pointer
    values.
- **Adapter:**
  - Selectors for check, create, submit SETUP_TMR, observe, and teardown
    (DESTROY_TMR then DESTROY_RINGS). Diagnostics version 8.
  - The one new writable carveout mapping, of the three pages, created only
    during write steps.
  - Teardown on abandon.
- **Tool:** `--psp-tmr` prints each step, the fence value, the response
  status, `fw_addr` and `tmr_size`, and the region comparison.
- **Tests:**
  - Exact page images (cross-checked with the `psp_gfx_if.h` offsets: the
    command at +8, the fields at +28, the response at +864; the frame
    layout).
  - The write-pointer arithmetic and the fence timeout.
  - Teardown order, and teardown on abandon after the submit only.
  - Memory writes refused outside the three pages and in any other order.
  - Weakened cores that must fail: the readback check removed, the
    fence-timeout removed, a write-pointer value other than 16/32, and a
    memory write outside the pages.

**Expected results:**
- The fence reads 1 within milliseconds. The response status is 0.
- `DESTROY_TMR` fences 2 with status 0, and the ring destroy is as in boot
  16.
- Only the expected words change around the ring.

**Risks:**

- These are the first CPU writes to GPU memory and the first command the
  PSP reads from memory. If the frame or command is malformed, the PSP may
  hang the ring. Shut down fully afterwards.
- If `SETUP_TMR` succeeds, the PSP may protect the 4 MiB TMR region from
  the CPU until the next cold boot. macOS does not use the carveout beyond
  the boot framebuffer at offset 0, and the driver never touches the TMR
  after step 1.
- A misdirected PSP write could land outside the three pages. Only the
  64 KiB around them is compared; any unexpected change there means power
  off at once.
- A fence that never arrives means the PSP did not process the frame:
  teardown is still attempted, then shut down fully.
- Make a Time Machine backup before this boot.

**What it does not do.** It loads no firmware (`LOAD_IP_FW` is the next
stage), and no TOC, ASD or TA. It makes no IH, GART or default-page
change. It sends no second command while the first is unfenced.

**Implementation notes:**

- **Core:**
  - `pspCommandWord` and `pspFrameWord` give the exact words.
  - `pspWorkWriteAllowed` allows only those words or 0, only in the command
    and fence pages and frames 0 and 1.
  - `writeWork` is the only memory write site; it checks the allowlist
    first.
  - `writePspCommand` writes the whole command page, the fence page (setup
    only) and the frame, then reads every written word back
    (`psp-readback-mismatch`).
  - `submitPspFrame` requires `C2PMSG_67` = frame × 16 (`psp-out-of-order`),
    writes 16 or 32 (the only allowed values, from stage 12), and polls the
    fence (`psp-fence-timeout`).
  - `readPspResponse` returns `psp-command-failed` on a non-zero status.
  - `verifyPspWorkArea` checks all 64 KiB:
    - the frames and the command page hold the driver's words, except the
      96-byte response area;
    - the fence dword holds the fence value and the rest of its page is 0;
    - everything else matches the snapshot.
  - `checkPspTmrTarget` and `checkRegionChecksum` check the TMR placement
    and its stability (a position-dependent sum, read twice about 1 s
    apart).
- **Adapter:**
  - The stage 11 check also checks the TMR at stage 12.
  - Selectors 15–17: submit, observe, teardown. Diagnostics version 8.
  - Submit needs a create answered with status 0.
  - A submit counts as sent once `C2PMSG_67` reads 16.
  - Teardown sends `DESTROY_TMR` only if `SETUP_TMR` fenced, then always
    destroys the ring.
  - Abandon after a submit runs the teardown.
  - The work area is the only writable carveout mapping, created only inside
    the submit and teardown operations.
  - The TMR is mapped read-only only for the check.
- **Tool:** `--psp-tmr` runs check, create, submit, observe and teardown. It
  destroys the ring through the stage 11 selector if the frame never
  reached the PSP.
- **Tests:**
  - The page images against the `psp_gfx_if.h` offsets.
  - The allowlist, including frame 2, wrong words, offsets past the work
    area and stage 11.
  - A full fake-PSP run: SETUP_TMR, fence 1, response, verify, then
    DESTROY_TMR, fence 2, verify.
  - A stray write in the fence page and beyond the work area, a fence
    timeout, a failed status, lost writes, and an out-of-order submit.
  - Four more weakened cores must fail.
  - The kext tests now name both writable mappings and both stores. Their
    mapping regex was tightened: before, it did not match the stage 6
    page's `map` call at all.

### Stage 13: first firmware load, SDMA0 (proposal)

**Status: proposed 2026-10-06, approved by the user the same day,
implemented, and succeeded in boot 18.** The proposal is kept as approved. The
implementation notes follow it.

**Purpose.** Load the first engine firmware: SDMA0, the DMA engine needed
for the "verified DMA copy and fence" milestone. The PSP loads it into the
TMR set up in stage 12. The engine stays halted: starting it is a later
stage.

**Linux v6.12:**

- **Order.** After `SETUP_TMR`, `psp_load_non_psp_fw` loads each firmware
  in `AMDGPU_UCODE_ID` order (`amdgpu_ucode.h`). `CAP` comes first and is
  absent on this APU, so `SDMA0` is the first firmware loaded.
- **The bytes.** `amdgpu_sdma_init_microcode` (header v1, PSP load) adds
  `AMDGPU_UCODE_ID_SDMA0`. `amdgpu_ucode_init_single_fw` (default case)
  then copies `ucode_size_bytes` from `ucode_array_offset_bytes` into a
  page-aligned slot of the firmware buffer (`amdgpu_ucode_init_bo`, at
  `ALIGN(ucode_size, PAGE_SIZE)`).
- **The command.** `psp_prep_load_ip_fw_cmd_buf` builds
  `GFX_CMD_ID_LOAD_IP_FW` (6) at +28 (`psp_gfx_cmd_load_ip_fw`):
  `fw_phy_addr_lo`/`hi` = the slot's GPU address, `fw_size` =
  `ucode_size`, and `fw_type` = `GFX_FW_TYPE_SDMA0` (9).
  - It is submitted like stage 12's commands.
  - The response's `fw_addr_lo`/`hi` gives the firmware's location in the
    TMR (`psp_cmd_submit_buf` stores it in `ucode->tmr_mc_addr_*`).
- **Starting the engine** happens later. With PSP loading,
  `sdma_v4_0_start` skips `sdma_v4_0_load_microcode`; the engine stays
  halted until `sdma_v4_0_enable` clears `SDMA0_F32_CNTL.HALT`. Stage 13
  does not do that.

**The firmware.** This is the pinned `green_sardine_sdma.bin` (linux-firmware
`20260916`, SHA-256 `cba8658e…9e09de`, 17,408 bytes;
[firmware provenance](firmware-provenance.md)).
`tools/amdgpu_firmware.py` accepts it:
- header v1.0, IP 4.1;
- `ucode_version` 40, feature version 41;
- `ucode_size_bytes` 17,152 at `ucode_array_offset_bytes` 256.

The payload stays opaque. AMD's license allows binary redistribution with
its notice and forbids reverse engineering, so it is copied, never
interpreted.

**How the firmware reaches the driver.** It is embedded at build time.

- `driver/kext/build.sh` takes the pinned file from ignored `out/`, checks
  its SHA-256, and generates a C array into the build directory. The kext
  binary then contains the image. It lives only in `out/` and on the USB
  stick, never in Git.
- The core accepts the image only if its header matches the pinned values
  above.
- The alternative, passing it from `cezanne-diag` through a structure
  input, would need `IOMemoryDescriptor` in the kext, which its tests
  forbid.
- A packaged driver would have to carry `LICENSE.amdgpu`; this test build
  is not distributed.

**Placement**

| Buffer | Carveout offset | Size | GPU address |
| --- | --- | --- | --- |
| Ring, command, fence (stage 12) | `0x40100000` | 12 KiB | `0xF440100000` |
| Firmware buffer | `0x40200000` | 20 KiB (17,152 + zero padding) | `0xF440200000` |
| TMR (stage 12) | `0x40400000` | 4 MiB | `0xF440400000` |

**Steps** (on request only, `sudo cezanne-diag --psp-sdma`, each printed
first; ordered selectors):

1. **Check (no writes).**
   - Stage 12's check.
   - The embedded image's header equals the pinned values.
   - The 64 KiB at the firmware buffer is stable over about 1 s,
     snapshotted.
   - Read `SDMA0_UCODE_CHECKSUM` and `SDMA0_F32_CNTL` (halted).
2. **Create the ring** (stage 11).
3. **`SETUP_TMR`** as frame 0, fence 1 (stage 12).
4. **Copy the firmware.** Write the 17,152 bytes and the zero padding to
   20 KiB through a new writable, uncached mapping of exactly those five
   pages, then read them back (stop on any mismatch).
5. **`LOAD_IP_FW`** as frame 1 (`C2PMSG_67` ← 32, fence 2):
   - `cmd_id` 6, `0x40200000`, `0xF4`, `17152`, `9`.
   - Read the response status and `fw_addr`.
6. **Observe (no writes).**
   - Read `SDMA0_UCODE_CHECKSUM` again and `SDMA0_F32_CNTL`, which must
     still be halted.
   - Compare the 64 KiB at the work area (stage 12 rules, now with frames
     0–1 and the `LOAD_IP_FW` command).
   - Compare the 64 KiB at the firmware buffer: the image, then the
     snapshot.
7. **Teardown.** `DESTROY_TMR` as frame 2 (`C2PMSG_67` ← 48, fence 3), then
   `DESTROY_RINGS`. Abandon after step 3 does the same.

**New writes:**

| Target | Values |
| --- | --- |
| `C2PMSG_67` | adds `48` (frames 0, 1, 2 → 16, 32, 48) |
| Work area | adds the `LOAD_IP_FW` command words and frame 2 (fence 3); `DESTROY_TMR` moves to frame 2 |
| Firmware buffer (new) | physical `0x600200000`–`0x600204FFF`, only the validated image's words, or 0 in the padding |

No new register is written beyond the write-pointer value. `SDMA0_F32_CNTL`
is only read.

**Changes:**

- **Build:**
  - `build.sh` gains a firmware input, verified by SHA-256, and a generated
    header in the build output.
  - The kext tests check that no firmware bytes are tracked and the build
    refuses a wrong hash.
- **Core:**
  - `checkSdmaImage` (pinned header fields).
  - The firmware-buffer write allowlist and `writeSdmaFirmware` (with
    readback).
  - `LOAD_IP_FW` words, frames 0–2, and `submitPspFrame` for frame 2.
  - Read-only registers `SDMA0_UCODE_CHECKSUM` (`0x4a24`, in Linux's
    `sdma_reg_list_4_0`) and `SDMA0_F32_CNTL` (stage 5).
- **Adapter:**
  - Selectors for the SDMA steps. Diagnostics version 9.
  - One more writable carveout mapping (the five firmware pages), created
    only inside the copy step.
  - Teardown on abandon.
- **Tool:** `--psp-sdma` prints:
  - each step;
  - the fence values and response statuses;
  - `fw_addr` (and whether it lies inside the TMR);
  - the checksum before and after;
  - `F32_CNTL`;
  - both region comparisons.
- **Tests:**
  - The command words and image checks, including a wrong size, offset or
    version.
  - The firmware-buffer allowlist, including a wrong word and padding past
    20 KiB.
  - A full fake-PSP run over three frames.
  - Teardown orders.
  - Weakened cores that must fail: the header check removed, the
    firmware-buffer bounds removed, and a write pointer of 64 allowed.

**Expected results:**
- Fences 1, 2 and 3, all with status 0.
- `fw_addr` inside the TMR (`0xF440400000`–`0xF4407FFFFF`) or a TMR offset;
  either way recorded.
- `SDMA0_F32_CNTL` still halted.
- Only the expected words change in either region.
- `SDMA0_UCODE_CHECKSUM` may or may not change. It is recorded, not
  required.

**Risks:**

- The PSP validates the firmware's signature. A rejected image gives a
  non-zero status (a finding, not a fault).
- An accepted image puts code into the SDMA engine, which stays halted, as
  in Linux between loading and `sdma_v4_0_start`.
- `DESTROY_TMR` with firmware loaded is also what `psp_hw_fini` does. If the
  PSP refuses it, the status is recorded; the cold boot clears everything.
- A misdirected write is checked across 64 KiB at each of the two regions
  only. Unexpected words beyond the work area or the firmware image mean:
  power off at once.
- Make a Time Machine backup before this boot.

**What it does not do.** It does not unhalt SDMA, set up an SDMA ring or
copy anything with SDMA. It loads no other firmware (no CP, RLC or VCN). It
makes no IH, GART or default-page change.

**Implementation notes:**

- **Build:**
  - `build.sh` reads `out/firmware-provenance/fw/green_sardine_sdma.bin`
    (or `CEZANNE_SDMA_FW`) and refuses any SHA-256 other than the pin.
  - It generates `obj/sdma_image.cpp` with `xxd -i` in the build
    directory. The kext binary grows to about 112 KiB.
  - The kext build tests skip when the firmware is absent from `out/`.
  - Tests check that no `.bin` or generated image is tracked, and that a
    one-bit change to the firmware fails the build.
- **Core:**
  - `checkSdmaImage` compares the header fields with the pins (size, header
    version 1.0, IP 4.1, `ucode_version` 40, size 17,152, offset 256).
  - `sdmaFirmwareWord` and `sdmaFirmwareWriteAllowed` permit only the
    image's word at its offset, or 0 in the padding, inside the 20 KiB
    buffer.
  - `writeFirmwareWord` is the third write site, checked first.
    `writeSdmaFirmware` writes the whole buffer and reads it back.
  - `verifySdmaFirmwareRegion` compares the image and then the snapshot.
  - `writePspCommand` now takes the frame. The allowed pairs are
    `SETUP_TMR`/0, `DESTROY_TMR`/1 or 2 (2 from stage 13), and
    `LOAD_IP_FW`/1 (stage 13).
  - `C2PMSG_67` ← 48 is allowed from stage 13.
  - `SDMA0_UCODE_CHECKSUM` becomes readable at stage 13, the only new
    diagnostic register.
- **Adapter:**
  - The stage 11 check also validates the image, the firmware-buffer
    placement, and its 64 KiB stability (with a snapshot) at stage 13.
  - Selectors 18 (load) and 19 (observe). Diagnostics version 9.
  - Load needs a `SETUP_TMR` that fenced with status 0.
  - The teardown sends `DESTROY_TMR` as the next frame only if the last
    frame fenced.
  - Observe reports `sdma-not-halted` if `SDMA0_F32_CNTL.HALT` is clear.
  - The firmware buffer is the second writable carveout mapping, created
    only inside the load step.
- **Tool:** `--psp-sdma` prints:
  - each step;
  - the checksum and `F32_CNTL` before and after;
  - the fences and statuses;
  - `fw_addr`, and whether it lies inside the TMR;
  - both region comparisons.
- **Tests:**
  - A synthetic image (never the real firmware): header mutations, the
    allowlist, the copy, a lost write, the region compare, and the
    command and frame pairs.
  - Three frames against the fake PSP.
  - Three more weakened cores must fail.

### Stage 14: SDMA0 power-up and register inventory (proposal)

**Status: proposed 2026-10-06, approved by the user the same day,
implemented, and succeeded in boot 19.**

**Implementation notes:**

- **Core:**
  - `kStage14Registers` extends the stage 13 list by the 25 registers.
  - `kSdmaInventory` names the 31 read in each pass.
  - `writeAllowed` and `smuArgumentAllowed` accept `0xE`/`0xD` with
    argument 0 from stage 14.
  - `runSdmaInventory` reads, sends up, reads, sends down, then reads. It
    always sends `PowerDownSdma` after an OK `PowerUpSdma`, even if the
    middle reading fails.
- **Adapter:** selector 20, accepted only after a `LOAD_IP_FW` that fenced
  on the same connection. It returns the three readings as one structure.
  Diagnostics version 10.
- **Tool:** `--sdma-inventory` runs the stage 13 flow and the inventory
  before the teardown. It marks rows that changed between readings with
  `*`.
- **Tests:**
  - Offsets, prefix, uniqueness, not-writable.
  - The exact six mailbox writes.
  - A stop after a non-`0x01` response.
  - One more weakened core must fail.

**Purpose.** Prepare the first DMA copy (stage 15). Linux starts SDMA0 by
writing about 25 SDMA registers, most of them read-modify-write. The values
it writes therefore depend on what each register holds. Stage 14 measures
those registers on this host, with the firmware loaded and SDMA powered up,
so that stage 15's write allowlist can name exact values. It also sends the
two SMU messages Linux uses to power SDMA up and down on APUs. It writes no
SDMA register.

**Linux v6.12, the path stage 15 will follow** (`sdma_v4_0.c`):

1. **`sdma_v4_0_hw_init`.** On APUs it first calls
   `amdgpu_dpm_set_powergating_by_smu(SDMA, false)`, which reaches
   `smu_v12_0_powergate_sdma` and sends `PowerUpSdma` (`0xE`, argument 0;
   `smu_v12_0_ppsmc.h` says "SDMA is power gated by default"). Then
   `sdma_v4_0_init_golden_registers` applies `golden_settings_sdma_4_3` for
   SDMA 4.1.2: ten registers, each by `soc15_program_register_sequence`
   (a read, `& ~and_mask | (or_mask & and_mask)`, or a plain write for an
   and-mask of `0xffffffff`).
2. **`sdma_v4_0_start`.**
   - Unhalt (`F32_CNTL.HALT` = 0) and `ctx_switch_enable`.
   - `SEM_WAIT_FAIL_TIMER_CNTL` ← 0.
   - `sdma_v4_0_gfx_resume`: ring size, read and write pointers 0, the
     read-pointer write-back address, the ring base, `MINOR_PTR_UPDATE`,
     doorbell enable and offset, write-pointer poll off, `RB_ENABLE`, and
     `IB_ENABLE`.
   - `CNTL.UTC_L1_ENABLE` = 1, then unhalt again.
   - The page queue is not used on 4.1.2:
     `sdma_v4_0_fw_support_paging_queue` returns false.
3. **The ring test** (`sdma_v4_0_ring_test_ring`). One `WRITE_LINEAR` packet
   writes `0xDEADBEEF` to a write-back dword. The write pointer goes through
   `SDMA0_GFX_RB_WPTR`/`_HI` in bytes when no doorbell is used
   (`sdma_v4_0_ring_set_wptr`). Commits pad with NOPs to the ring's
   `align_mask`.
4. **Copy and fence.** `sdma_v4_0_emit_copy_buffer` builds a 7-dword
   `COPY_LINEAR` (byte count − 1, swap 0, source, destination).
   `sdma_v4_0_ring_emit_fence` builds a 4-dword `FENCE` (address, value),
   followed by a `TRAP`. Opcodes come from `vega10_sdma_pkt_open.h`: NOP 0,
   COPY 1, WRITE 2, FENCE 5, TRAP 6.
5. **Teardown** (`sdma_v4_0_hw_fini`): `ctx_switch_enable(false)`, then
   `sdma_v4_0_enable(false)`: `RB_ENABLE` and `IB_ENABLE` 0, then
   `HALT` = 1. On APUs it then sends `PowerDownSdma` (`0xD`).

**Steps** (on request only, `sudo cezanne-diag --sdma-inventory`, each
printed first):

1. **Stage 13 up to the load.** Check, create, `SETUP_TMR`, copy, and
   `LOAD_IP_FW` as in boot 18.
2. **Read** the 25 registers below, plus `F32_CNTL`, `CLK_CTRL`,
   `POWER_CNTL`, `STATUS_REG`, `GFX_RB_CNTL` and `UCODE_CHECKSUM` from
   stages 5 and 13: the "loaded, gated" state.
3. **`PowerUpSdma`** (`0xE`, argument 0) through the stage 7 mailbox path.
   The response must be `0x01`.
4. **Read the same 31 registers** again: the "powered" state that stage 15
   will start from.
5. **`PowerDownSdma`** (`0xD`, argument 0); the response must be `0x01`.
   Then **read them a third time**, to learn whether power gating keeps the
   firmware (checksum) and settings.
6. **Teardown** as in stage 13: `DESTROY_TMR`, then the ring destroy.

**New readable registers** (diagnostic interface, from stage 14; SDMA0 base
`0x1260`, `sdma0_4_0_offset.h`):

| Register | BAR5 byte offset | Linux v6.12 use |
| --- | --- | --- |
| `SDMA0_CNTL` | `0x049f0` | sdma_v4_0_start, ctx_switch_enable (UTC_L1_ENABLE, AUTO_CTXSW_ENABLE) |
| `SDMA0_CHICKEN_BITS` | `0x049f4` | golden_settings_sdma_4_3 |
| `SDMA0_GB_ADDR_CONFIG` | `0x049f8` | golden_settings_sdma_4_3 |
| `SDMA0_GB_ADDR_CONFIG_READ` | `0x049fc` | golden_settings_sdma_4_3 |
| `SDMA0_SEM_WAIT_FAIL_TIMER_CNTL` | `0x04a04` | sdma_v4_0_start (written 0) |
| `SDMA0_UTCL1_WATERMK` | `0x04a74` | golden_settings_sdma_4_3 |
| `SDMA0_UTCL1_TIMEOUT` | `0x04a9c` | ctx_switch_enable (written 0x00800080) |
| `SDMA0_UTCL1_PAGE` | `0x04aa0` | golden_settings_sdma_4_3 |
| `SDMA0_GFX_RB_BASE` | `0x04b84` | gfx_resume |
| `SDMA0_GFX_RB_BASE_HI` | `0x04b88` | gfx_resume |
| `SDMA0_GFX_RB_RPTR` | `0x04b8c` | gfx_resume; sdma_reg_list_4_0 |
| `SDMA0_GFX_RB_RPTR_HI` | `0x04b90` | gfx_resume; sdma_reg_list_4_0 |
| `SDMA0_GFX_RB_WPTR` | `0x04b94` | gfx_resume, ring_set_wptr (no doorbell) |
| `SDMA0_GFX_RB_WPTR_HI` | `0x04b98` | gfx_resume, ring_set_wptr |
| `SDMA0_GFX_RB_WPTR_POLL_CNTL` | `0x04b9c` | gfx_resume (read-modify-write); golden |
| `SDMA0_GFX_RB_RPTR_ADDR_HI` | `0x04ba0` | gfx_resume |
| `SDMA0_GFX_RB_RPTR_ADDR_LO` | `0x04ba4` | gfx_resume |
| `SDMA0_GFX_IB_CNTL` | `0x04ba8` | gfx_resume (read-modify-write) |
| `SDMA0_GFX_DOORBELL` | `0x04bc8` | gfx_resume (read-modify-write) |
| `SDMA0_GFX_DOORBELL_OFFSET` | `0x04c2c` | gfx_resume (read-modify-write) |
| `SDMA0_GFX_RB_WPTR_POLL_ADDR_HI` | `0x04c48` | gfx_resume |
| `SDMA0_GFX_RB_WPTR_POLL_ADDR_LO` | `0x04c4c` | gfx_resume |
| `SDMA0_GFX_MINOR_PTR_UPDATE` | `0x04c54` | gfx_resume |
| `SDMA0_RLC0_RB_WPTR_POLL_CNTL` | `0x04e9c` | golden_settings_sdma_4_3 |
| `SDMA0_RLC1_RB_WPTR_POLL_CNTL` | `0x0501c` | golden_settings_sdma_4_3 |

None of them is a data port or a clear-on-read counter. `SDMA0_UCODE_DATA`
(auto-incrementing) is deliberately left out.

**New writes:** the SMU message register (`C2PMSG_66`) may also take `0xD`
and `0xE`, each paired with argument 0. Nothing else is new. No SDMA
register is written, and `F32_CNTL` stays halted throughout.

**Changes:**

- **Core:**
  - The stage 14 register list.
  - The two SMU messages in `writeAllowed`/`smuArgumentAllowed`, sent with
    the stage 7 send-and-poll.
- **Adapter:** one selector that runs steps 2–5 after the stage 13 load, and
  returns the three readings and both responses. Diagnostics version 10.
  The stage 13 teardown still applies on abandon.
- **Tool:** `--sdma-inventory` prints the 31 registers in three columns
  (loaded, powered up, powered down).
- **Tests:**
  - The new offsets and that they are not writable.
  - The message and argument pairs, and the message order.
  - A weakened core allowing `0xE` with a non-zero argument must fail.

**Expected values.**
- Both responses `0x01`.
- `F32_CNTL` halted in all three readings.
- The checksum stays `0x25a1ba79`, at least while powered.
- No prediction for the others. They are what stage 15 needs.

**What stage 15 will then propose** (for orientation; it gets its own
proposal with the measured values):

- Keep the TMR and the firmware. Apply the golden settings and the
  `sdma_v4_0_start` writes, with exact values computed from the stage 14
  readings. Use no doorbell; write the write pointer to the registers.
- Set up a ring and a write-back page at carveout `0x40300000`, and a source
  and destination page next to them.
- Run the ring test (`WRITE_LINEAR` `0xDEADBEEF`), then a 4 KiB
  `COPY_LINEAR` and a `FENCE` (no `TRAP`, since no interrupts are set up).
- Verify the destination against the source, the fence, and the 64 KiB
  around the pages. Then halt, `PowerDownSdma`, and tear down.
- **Addressing:** SDMA uses MMHUB, VMID 0. Context 0 is disabled (boot 14),
  so every address must lie in the FB aperture. The default page is 0
  (boot 14 finding): stage 15 must use only checked carveout addresses,
  and point the default page at a scratch page first if review requires it.

**Risks.**
- `PowerUpSdma` and `PowerDownSdma` are what Linux sends on every APU
  start and stop. A non-`0x01` response is a finding. If the SDMA register
  reads hang after power changes, power off.
- Make a Time Machine backup before this boot.

### Stage 15: first SDMA copy (proposal)

**Status: proposed 2026-10-06, approved by the user the same day (golden
`GB_ADDR_CONFIG` applied, default page left for later), and implemented; not
yet booted.**

**Implementation notes:**

- **Core:**
  - `kSdmaBoot19` pins the 31 boot 19 values, `STATUS_REG` excepted.
  - The register tables are `kSdmaGolden` (10), `kSdmaStart` (24) and
    `kSdmaStop` (3). `sdmaWriteListed` derives the allowlist from them,
    plus write-pointer values 1024 and 2048.
  - `sdmaRingWord` and `sdmaWorkWord` give the exact images.
    `sdmaWorkWriteAllowed` permits only those words in the 16 KiB work
    area. `writeSdmaWorkWord` is the fourth memory write site.
  - `startSdma` records progress (powered, registers written), and
    `stopSdma` undoes exactly that much.
  - `submitSdma` requires the previous write pointer.
  - `verifySdmaCopy` checks the read pointer, the destination against the
    source, and the 64 KiB region.
- **Adapter:**
  - `accessDevice` can now map a fixed set of three writable BAR5 pages
    (`kSdmaPages`: `0x4000` and `0x5000` for SDMA0, `0x58000` for the SMU).
    It is used only by the start, submit and stop operations, and every
    write still passes `writeAllowed`.
  - The copy work area is the third writable carveout mapping.
  - Selectors 21–25 (check, start, submit, verify, stop), accepted in
    order. Diagnostics version 11.
  - The stage 13 teardown selector is refused while a copy is under way.
    Stop, and abandon after the check, halt and power SDMA down before the
    PSP teardown.
- **Tool:** `--sdma-copy` runs the stage 13 load, then the copy steps. Once
  the copy check passes it always ends with the stop.
- **Tests:**
  - The golden values are re-derived in the test from Linux's masks and
    the boot 19 values.
  - The `gfx_resume` arithmetic, every write inside the mapped pages, and
    the packet images.
  - A fake SDMA engine executes WRITE, COPY and FENCE from the fake ring:
    the full run, a corrupted copy plus a stray write (both caught), a
    halted engine (timeout), out-of-order submits, and stops at each
    progress.
  - Four more weakened cores must fail.
  - The kext tests now name four writable carveout mappings and stores,
    and match the two-index register store. Their store pattern was
    tightened so a `base[i][...]` store cannot slip past.

**Purpose.** This is the "verified DMA copy and fence" milestone: SDMA0, running
the firmware stage 13 loaded, copies 4 KiB from one checked carveout page to
another and writes a fence. Every SDMA register write has the exact value
Linux v6.12 computes from the registers boot 19 measured. The check refuses
to start if any of those registers reads differently.

**Sequence** (on request only, `sudo cezanne-diag --sdma-copy`; each step
printed first). Linux functions are in `sdma_v4_0.c` unless noted.

1. **Stage 13 up to the load.** Check, create, `SETUP_TMR`, firmware copy,
   `LOAD_IP_FW` as in boot 19, but **no teardown**: the TMR and firmware
   stay for the copy. The check also requires:
   - the 31 boot 19 SDMA values exactly;
   - the 64 KiB at the copy work area (below) stable, snapshotted.
2. **`PowerUpSdma`** (SMU `0xE`, stage 14). This is `sdma_v4_0_hw_init` on
   APUs.
3. **Golden settings** (`golden_settings_sdma_4_3`, exact results from boot
   19):

   | Register | Write |
   | --- | --- |
   | `CHICKEN_BITS` | `0x02831f07` |
   | `CLK_CTRL` | `0x3f000100` |
   | `GB_ADDR_CONFIG` | `0x00000002` |
   | `GB_ADDR_CONFIG_READ` | `0x00000002` |
   | `GFX_RB_WPTR_POLL_CNTL` | `0x00403000` |
   | `POWER_CNTL` | `0x40000051` |
   | `RLC0_RB_WPTR_POLL_CNTL` | `0x00403000` |
   | `RLC1_RB_WPTR_POLL_CNTL` | `0x00403000` |
   | `UTCL1_PAGE` | `0x000003e0` (unchanged; Linux writes it anyway) |
   | `UTCL1_WATERMK` | `0x03fbe1fe` |

   **The `GB_ADDR_CONFIG` decision.** Recommended: apply the golden value
   as Linux does for this IP (4.1.2). The field (`NUM_PIPES` and related)
   only affects tiled addressing, so a linear copy does not depend on it
   either way. The alternative, keeping the firmware's `0x00100012`, would
   deviate from the configuration Linux runs on this chip.
4. **`sdma_v4_0_start` and `sdma_v4_0_gfx_resume`** (no doorbell, so the
   write pointer goes through registers, `sdma_v4_0_ring_set_wptr`):

   | # | Register | Write | Linux |
   | --- | --- | --- | --- |
   | 1 | `F32_CNTL` | `0x00000000` | `sdma_v4_0_enable(true)`: HALT 0 |
   | 2 | `SEM_WAIT_FAIL_TIMER_CNTL` | `0` | `_start` |
   | 3 | `GFX_RB_CNTL` | `0x00040014` | `RB_SIZE` = log2(1024 dwords) = 10 |
   | 4 | `GFX_RB_RPTR`, `_HI`, `GFX_RB_WPTR`, `_HI` | `0` each | pointers 0 |
   | 5 | `GFX_RB_RPTR_ADDR_HI` / `_LO` | `0xF4` / `0x40301000` | read-pointer write-back |
   | 6 | `GFX_RB_BASE` / `_HI` | `0xF4403000` / `0x0` | ring at `0xF440300000`, `>> 8` and `>> 40` |
   | 7 | `GFX_MINOR_PTR_UPDATE` | `1` | before the pointer write |
   | 8 | `GFX_DOORBELL` / `_OFFSET` | `0` / `0` | doorbell off |
   | 9 | `GFX_RB_WPTR`, `_HI` | `0`, `0` | `ring_set_wptr` |
   | 10 | `GFX_MINOR_PTR_UPDATE` | `0` | after |
   | 11 | `GFX_RB_WPTR_POLL_ADDR_LO` / `_HI` | `0x40301008` / `0xF4` | poll address (polling stays off) |
   | 12 | `GFX_RB_WPTR_POLL_CNTL` | `0x00403000` | `F32_POLL_ENABLE` 0 |
   | 13 | `GFX_RB_CNTL` | `0x00041015` | + `RPTR_WRITEBACK_ENABLE`, `RB_ENABLE` |
   | 14 | `GFX_IB_CNTL` | `0x00000101` | + `IB_ENABLE` |
   | 15 | `CNTL` | `0x00000002` | `UTC_L1_ENABLE` (already 1) |
   | 16 | `F32_CNTL` | `0x00000000` | unhalt |

   **Left out, on purpose:**
   - `sdma_v4_0_ctx_switch_enable`: `AUTO_CTXSW_ENABLE`, the phase quantum
     and `UTCL1_TIMEOUT`. They only matter with several queues; this stage
     has one.
   - `sdma_v4_1_init_power_gating`.
   - The page queue: unused on 4.1.2.
5. **Ring test** (`sdma_v4_0_ring_test_ring`). Frame 0 of the SDMA ring is
   `WRITE_LINEAR` (`0x00000002`), `0x40301100`, `0xF4`, count 0, then
   `0xDEADBEEF`, padded with plain NOPs (`0x00000000`) to 256 dwords
   (`align_mask` `0xff`). Then `GFX_RB_WPTR` ← `1024` (bytes), and poll the
   write-back dword for `0xDEADBEEF`, up to 100 × 1 ms.
6. **Copy and fence.** Frame 1 (dwords 256–511):
   - `COPY_LINEAR` (`0x00000001`), `4095`, `0`, source `0x40302000`/`0xF4`,
     destination `0x40303000`/`0xF4` (`sdma_v4_0_emit_copy_buffer`).
   - `FENCE` (`0x00000005`), `0x40301200`, `0xF4`, `1`
     (`sdma_v4_0_ring_emit_fence`, without the `TRAP`: no interrupts are
     set up).
   - Plain NOPs to 512 dwords.

   Then `GFX_RB_WPTR` ← `2048` and poll the fence for 1, up to 100 × 1 ms.
7. **Verify (no writes):**
   - `GFX_RB_RPTR` equals 2048;
   - the destination page equals the source page byte for byte;
   - the source page is unchanged;
   - the 64 KiB region holds only the expected words (below);
   - `STATUS_REG` is recorded.
8. **Teardown** (`sdma_v4_0_hw_fini`, then stage 13):
   - `GFX_RB_CNTL` ← `0x00041014` and `GFX_IB_CNTL` ← `0x00000100`
     (`gfx_enable(false)`);
   - `F32_CNTL` ← `0x00000001` (halt);
   - `PowerDownSdma`, `DESTROY_TMR`, then the ring destroy.
   - Abandon after step 4 does the same.

**The copy work area** (carveout `0x40300000`, GPU `0xF440300000`, physical
`0x600300000`; between the firmware buffer's check region and the TMR):

| Page | Offset | CPU writes | SDMA writes |
| --- | --- | --- | --- |
| Ring | `+0x0000` | frames 0 and 1 exactly, rest of page 0 | none (it reads) |
| Write-back | `+0x1000` | all 0 | read pointer at `+0x000` (8 bytes), `0xDEADBEEF` at `+0x100`, fence 1 at `+0x200` |
| Source | `+0x2000` | a fixed pattern: word *i* = `0x5A5A0000 + i` | none |
| Destination | `+0x3000` | all 0 | the 4 KiB copy |

The CPU writes are read back before the first write-pointer write. The
rest of the 64 KiB must match the snapshot afterwards.

**Addressing and the default page.**
- SDMA uses MMHUB, VMID 0 (`ring->vm_hub`). Context 0 is disabled (boot
  14), so these addresses resolve through the FB aperture, like the SMU's
  and PSP's writes in stages 9 to 13.
- The zero default page (boot 14) applies only to system-aperture
  addresses outside FB, the last 256 KiB above `0xF47FFFFFFF`. No packet
  names such an address; the packets are exact and read back before they
  run.
- Recommended: leave the default page for the GART stage rather than add
  MMHUB writes now.

**New writes:**
- **SDMA registers:** the 26 named above, only with the listed values, from
  stage 15. Each is pinned by the boot 19 precondition.
- **Memory:** the four work-area pages, only with the words above.
- **Messages:** none beyond stages 13 and 14.

**Changes:**
- **Core:**
  - `SdmaStart`, a table of `{register, value}` in order, with the
    allowlist derived from it.
  - Packet builders and an exact ring image, and the work-area images and
    allowlist.
  - `startSdma`, `submitSdma` (write pointer, poll), `verifySdmaCopy`,
    `stopSdma`.
  - A precondition check against the boot 19 values.
- **Adapter:**
  - Selectors for start, ring test, copy and stop. Diagnostics version 11.
  - A third writable carveout mapping (the four pages), only during the
    write steps.
  - The full teardown on abandon.
- **Tool:** `--sdma-copy`: each step, read pointer, fence, compare results.
- **Tests:**
  - Exact register sequences and packet images, cross-checked against
    `vega10_sdma_pkt_open.h`.
  - A fake SDMA engine that executes WRITE, COPY and FENCE from the fake
    ring.
  - A stop at each precondition, timeouts, a corrupted copy and a stray
    write.
  - Weakened cores that must fail: an unpinned precondition, a register
    value outside the table, a packet address outside the work area, and a
    missing halt in the teardown.

**Expected:**
- Write-back `0xDEADBEEF`, then fence 1.
- `GFX_RB_RPTR` 2048, the destination equal to the source, 0 unexpected
  words.
- SDMA halted again after the teardown.

**Risks:**
- This is the first time a GPU engine executes commands the driver wrote.
  A malformed packet could make SDMA read or write the wrong memory. That
  is mitigated by exact images, readback before the pointer write, and
  addresses confined to one checked 64 KiB region.
- A hang (no write-back or fence within 100 ms) leaves SDMA running. The
  teardown halts it. If that also fails, shut down fully.
- Unexpected words outside the work area mean: power off at once.
- Make a Time Machine backup before this boot.

## Build the test EFIs

On this Mac, with the internal EFI mounted read-only only for the copy (the
`diskutil mount` command needs `sudo`):

```sh
sudo diskutil mount readOnly disk1s1     # internal EFI; identify it with diskutil list first
mkdir -p out/test-efi
cp -Rp /Volumes/EFI/EFI out/test-efi/known-good-EFI
diskutil unmount /Volumes/EFI
driver/kext/build.sh out/test-efi/driver
for stage in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
  python3 tools/test_efi.py build --known-good out/test-efi/known-good-EFI \
    --kext out/test-efi/driver/CezanneGPU.kext --stage $stage --output out/test-efi/usb-stage$stage \
    --ocvalidate out/test-efi/opencore/DEBUG/Utilities/ocvalidate/ocvalidate
done
```

`build` refuses an existing output. If any check fails it exits 2 and leaves
the tree under `usb-stageN.rejected-<UTC time>`, never under the name the next
steps copy. Each `manifest.json` records the stage, the boot arguments, the
SHA-256 of every file in both trees, the removed bundles and the `ocvalidate`
output. The test EFIs differ only in the `cezanne-stage` value. They
contain the host's SMBIOS serials; keep them in ignored `out/`.

## Prepare the USB drive

Erasing destroys the drive's contents. Identify it carefully: it must be the
`external, physical` disk whose size matches the stick, never `disk0` or `disk1`.
Start with stage 0:

```sh
diskutil list external physical
diskutil eraseDisk FAT32 CZTEST GPT /dev/diskN      # N from the line above
ditto --norsrc --noextattr out/test-efi/usb-stage0/EFI /Volumes/CZTEST/EFI
python3 tools/test_efi.py verify --manifest out/test-efi/usb-stage0/manifest.json --side test /Volumes/CZTEST/EFI
diskutil eject /dev/diskN
```

`verify` must report `"match": true` (exit 0). It ignores macOS `._*` and
`.DS_Store` files.

To switch the drive to stage 1 after stage 0 succeeded, replace its `EFI`
folder and verify against the stage 1 manifest:

```sh
rm -rf /Volumes/CZTEST/EFI                          # the USB copy only; check the path
ditto --norsrc --noextattr out/test-efi/usb-stage1/EFI /Volumes/CZTEST/EFI
python3 tools/test_efi.py verify --manifest out/test-efi/usb-stage1/manifest.json --side test /Volumes/CZTEST/EFI
```

`tools/update_stick.sh N` does the same three steps for stage N.

- **Before removing anything**, it refuses unless `out/test-efi/usb-stageN`
  has an `EFI` folder and a manifest, and `/Volumes/CZTEST` is mounted at
  exactly that path, named `CZTEST`, and on an external disk (`diskutil
  info`: `Internal` false, `RemovableMediaOrExternalDevice` true).
- It exits non-zero if `verify` does not report a match.
- The user runs it, as with every disk step.

## Before the first test boot

- Make a Time Machine (or equivalent) backup. A panic during a disk write can
  damage the shared macOS volume.
- Turn on Remote Login (System Settings → General → Sharing) and confirm SSH
  works from another device. Without NootedRed, the display may stay on the
  firmware framebuffer or stay black; whether macOS 26 reaches a usable desktop
  without GPU acceleration is unknown.
- Find the motherboard's boot-menu key and confirm the internal disk remains the
  default boot device in firmware setup.

## Run a test boot

1. Plug in the drive, power on, open the firmware boot menu and choose the USB
   entry (often `UEFI: <drive name>`).
2. The OpenCore picker looks the same as usual. Choose the macOS volume.
   Verbose text instead of the Apple logo confirms the test boot arguments.
3. If boot stops, photograph the screen. `debug=0x100` keeps a panic on screen.
   Power off, unplug the drive, and boot the known-good EFI to collect logs.

Once macOS is up (locally or over SSH), record:

```sh
sysctl kern.bootargs                               # must contain cezanne-stage=N
kmutil showloaded --list-only | grep -i -E 'cezanne|nootedred|radeon'
ioreg -r -c CezanneGPU -l -w0
/usr/bin/log show --last boot --predicate 'eventMessage CONTAINS "CezanneGPU:"'
sudo dmesg | grep 'CezanneGPU:'                    # kernel buffer; wraps within minutes
```

Use `/usr/bin/log`: in zsh, a bare `log` is a shell builtin.

Stage 0 succeeds when:

- `org.cezanne-driver.gpu` is loaded and NootedRed is not.
- `ioreg` shows a `CezanneGPU` instance under the Cezanne PCI device with
  `CezanneGPU stage` 0 and `CezanneGPU registry vendor-id` `0x1002`,
  `device-id` `0x1638` and `revision-id` `0xc9`.
- The `CezanneGPU` entry is `registered`. `start()` calls `registerService()`
  only after logging `attached at stage N`, so this proves `start()` completed
  even when that line is unavailable: the driver's `IOLog` lines do not reach
  the unified log, and the kernel buffer is 128 KiB despite `msgbuf=1048576`
  and had wrapped within about 195 s of boot 2. The `ioreg` properties are the
  record; read `dmesg` within the first minute to catch the lines.

It also answers two unknowns: what the display does without NootedRed, and
whether the 26.0-targeted kext loads.

Stage 1 succeeds when:

- The stage 0 conditions hold with `CezanneGPU stage` 1, and the machine
  boots to the same point it reached at stage 0.
- `CezanneGPU stage 1 result` is `ok`, and `CezanneGPU config bar5` and
  `CezanneGPU bar5 length` agree with the registry's `assigned-addresses`.
- `CezanneGPU MP0_SMN_C2PMSG_33` and `CezanneGPU RCC_CONFIG_MEMSIZE` are
  recorded. Compare them with the expected values above; a mismatch is a
  finding to investigate, not a reason to retry with writes.

Stage 2 succeeds when:

- The stage 1 conditions hold with `CezanneGPU stage` 2.
- `CezanneGPU stage 2 result` is `ok` and `CezanneGPU discovery binary` is
  published; save it with
  `ioreg -r -c CezanneGPU -a > out/test-efi/boot-N-stage2/ioreg.plist`
  (ignored; it is a raw capture).
- `MC_VM_FB_OFFSET`, `carveout base`, `discovery address`, the GC version and
  the bases are recorded and compared with the expected values above.

Stage 15 succeeds when:

- the stage 14 conditions hold with `CezanneGPU stage` 15;
- `sudo cezanne-diag --gfxoff-disallow --sdma-copy --psp-state` reports
  `ok` for:
  - the stage 13 load;
  - the copy check;
  - the start: progress 2, `PowerUpSdma` `0x01`;
  - the ring test: observed `0xDEADBEEF`, read pointer 1024;
  - the copy: fence 1, read pointer 2048;
  - the verify: 0 unexpected words;
  - the stop: `F32_CNTL` halted, `PowerDownSdma` `0x01`, `DESTROY_TMR`
    fence 3;
- the machine stays as before.

**On failure:**
- `sdma-unexpected-state` at the check means a register no longer reads its
  boot 19 value: record it, nothing was sent.
- `sdma-timeout` means: the stop still halts SDMA; shut down fully
  afterwards.
- Unexpected words outside the work area mean: power off at once.
- Make a Time Machine backup first. Save the output with `tee`.

Stage 14 succeeds when:

- the stage 13 conditions hold with `CezanneGPU stage` 14;
- `sudo cezanne-diag --gfxoff-disallow --sdma-inventory --psp-state`:
  - reports the stage 13 steps `ok`;
  - reports both SMU responses `0x01`;
  - prints all three columns;
  - shows `SDMA0_F32_CNTL` halted throughout;
- the machine stays as before.

A non-`0x01` response is a finding; do not retry. Save the output with
`tee`. Make a Time Machine backup first.

Stage 13 succeeds when:

- the stage 12 conditions hold with `CezanneGPU stage` 13;
- `sudo cezanne-diag --gfxoff-disallow --psp-sdma --psp-state` reports
  `ok` for:
  - the check;
  - the create;
  - `SETUP_TMR` (fence 1, status 0);
  - the load (fence 2, status 0, `C2PMSG_67` 32);
  - the observe: 0 unexpected words in both regions, SDMA0 still halted;
  - the teardown (`DESTROY_TMR` fence 3, then the ring destroy);
- `fw_addr` and the checksum before and after are recorded;
- the machine stays as before.

**On failure:**
- A non-zero `LOAD_IP_FW` status (for example a rejected signature) is a
  finding: record it and do not retry.
- `sdma-not-halted` means: shut down fully.
- Unexpected words outside the work area or the firmware image mean: power
  off at once.
- Make a Time Machine backup before this boot. Save the output with `tee`.

Stage 12 succeeds when:

- the stage 11 conditions hold with `CezanneGPU stage` 12;
- `sudo cezanne-diag --gfxoff-disallow --psp-tmr --psp-state` reports
  `ok` for:
  - the check (now with the TMR);
  - the create;
  - the submit: fence 1, response status 0, `C2PMSG_67` 16;
  - the observe: 0 unexpected words;
  - the teardown: `DESTROY_TMR` fence 2 with status 0, then the ring
    destroy;
- the machine stays as before.

**On failure:**
- A non-zero response status is a finding to record; do not retry.
- A fence timeout, or unexpected words outside the command and fence pages,
  means: shut down fully. Unexpected words beyond the work area mean a
  misdirected write: power off at once.
- Make a Time Machine backup before this boot. Save the output with `tee`.

Stage 11 succeeds when:

- the stage 10 conditions hold with `CezanneGPU stage` 11;
- `sudo cezanne-diag --gfxoff-disallow --psp-ring --psp-state` reports `ok`
  for the check, the create (three responses `0x80000000`), the observe
  (0 changed words in and after the page) and the destroy (response
  `0x80000000`);
- the final `--psp-state` and register dump are recorded;
- the machine stays as before: display, fans, temperatures.

A stop at the check is a finding with nothing sent. `psp-response-not-ok`
or `psp-timeout` during the create is a finding: do not retry. Shut down
fully and boot the known-good EFI. Changed words outside the ring page mean
a misdirected write: power off at once. Make a Time Machine backup before
this boot. Save the output with `tee` in an ignored directory.

Stage 10 succeeds when:

- the stage 9 conditions hold with `CezanneGPU stage` 10 (running
  `--smu-metrics` again is optional);
- `sudo cezanne-diag --gfxoff-disallow --psp-state` reports `ok` for the
  message and reads every stage 10 register, and the plain dump lists all 70
  registers with values;
- the values are recorded and compared with the expectations in the stage 10
  section;
- the machine stays as before.

A `gfx-not-on` on the GC-hub registers is a finding, not a failure (run
`--gfxoff-disallow` first). Save the output with `tee` in an ignored
directory. Nothing in stage 10 writes, so no backup step is added.

Stage 9 succeeds when:

- the stage 8 conditions hold with `CezanneGPU stage` 9;
- `sudo cezanne-diag --smu-metrics` reports `ok` for the check, for the
  transfer (three responses `0x01`) and for the read;
- the decoded values are plausible and recorded;
- the machine stays as before.

`table-region-in-use`, `metrics-address-mismatch` or
`metrics-target-invalid` stops before any message, and is a finding. If the
read reports `table-not-written`, treat it as a possibly misdirected write:
save nothing, power off at once, and boot the known-good EFI. Make a Time
Machine backup before this boot. Save the output with `tee` in an ignored
directory.

Stage 8 succeeds when:

- the stage 7 conditions hold with `CezanneGPU stage` 8;
- `sudo cezanne-diag --gfxoff-disallow` reports `ok` for the check and the
  message, with response `0x01` and `PWR_GFXOFF_STATUS` 2;
- the register dump matches boot 9, with the mailbox holding `0x8`, `0`
  and `0x1`;
- the machine stays as before: fans, temperatures, desktop.

A `smu-response-not-ok` (for example `0xFD`) or `gfxoff-timeout` is a finding
to record; do not retry or send `AllowGfxOff`.

Stage 7 succeeds when:

- the stage 6 conditions hold with `CezanneGPU stage` 7;
- `sudo cezanne-diag --smu-query` reports `ok` for the check and both
  queries, each with response `0x01`;
- the driver-interface and firmware versions are recorded;
- the register dump that follows matches boot 8, with the mailbox now holding
  the last exchange (`C2PMSG_66` `0x2`, `_82` the version, `_90` `0x1`);
- the machine stays stable afterwards: desktop responsive, fans normal.

Any other status is a finding to record; do not retry or send another message.
Save the output with `tee` in an ignored directory.

Stage 6 succeeds when the stage 5 conditions hold with `CezanneGPU stage` 6
and `sudo cezanne-diag --scratch-test` reports `ok` for all three steps:

- the check, with a stable original value;
- the write, reading back `0xcafedead`;
- the restore, reading back the original value.

The register dump that follows must match boot 7, with `SCRATCH_REG0` equal to
the original value. A `*-mismatch`, `scratch-unstable` or failed precondition
is a finding to record, not a reason to retry. Save the output with `tee` in
an ignored directory.

Stage 5 succeeds when the stage 4 conditions hold with `CezanneGPU stage` 5
and `sudo cezanne-diag --repeat 3` prints all 48 registers. Statuses other
than `ok` (for example `gfx-not-on`) are observations to record, not failures.
Save the output with `tee` in an ignored directory.

Stage 4 succeeds when the stage 3 conditions hold with `CezanneGPU stage` 4,
`CezanneGPU diagnostics` is `true`, and `sudo cezanne-diag --repeat 3` (built
with `tools/diag/build.sh out/diag`) prints all ten registers with no error
status. Also check that the tool without `sudo` is refused. Record its output
in an ignored directory.

Stage 3 succeeds when the stage 2 conditions hold with `CezanneGPU stage` 3,
`CezanneGPU stage 3 result` is `ok`, and the seven raw registers, `active CU
mask`/`count` and `active RB mask`/`count` are recorded and compared with the
expected values above. `gfx-index-not-se0-sh0` is an observation, not a
reason to write the index.

Any other stage 2 result is an observation that ends the experiment; a
`discovery-signature` with `discovery signature` `0xffffffff` or `0` points to
TMR protection. Do not retry with the `MM_INDEX` path without a new stage.

Any other stage 1 result (for example `not-in-d0` or `memory-decode-disabled`)
is a valid observation of the device's state and ends that boot's experiment.
A freeze or panic is a failure: record it as rule 6 requires.

Failure evidence: panic reports in `/Library/Logs/DiagnosticReports/*.panic` (read from
the known-good boot), and screen photos.

## Return to the known-good boot

Shut down, unplug the drive, and power on normally. Then confirm:

```sh
sysctl kern.bootargs                               # -NRedRBPlus, no cezanne-stage
sudo diskutil mount readOnly disk1s1
python3 tools/test_efi.py verify --manifest out/test-efi/usb-stage0/manifest.json --side known_good /Volumes/EFI/EFI
diskutil unmount /Volumes/EFI
```

`verify --side known_good` compares the internal EFI with the build-time hashes,
ignoring the config backups the test EFI does not copy. Both manifests record
the same known-good hashes.

## Test boot log

**Boot 1, 2026-10-05, stage 0** (manifest built 2026-10-05T14:07:34Z, with file
logging; kept as `out/test-efi/superseded-filelog-usb-stage0/`).

- Firmware boot menu showed two USB partitions. Partition 1 failed; partition 2
  started OpenCore.
- About 10–20 s of no visible progress, then a black screen. No picker appeared
  within about a minute; the user forced a power-off.
- The OpenCore log (copied to ignored `out/test-efi/boot-1-stage0/`; it holds
  SMBIOS values) has 304 lines over 51 s and ends at `OCB: Found 12 potentially
  bootable filesystems`, before the picker. Typical lines took 27 ms and 55
  lines took about 0.7 s each: file-log flushes to the USB 2.0 stick. No
  OpenCore error preceded the stop. macOS was never chosen, so the driver
  never loaded.
- The next power-on was slow and the firmware no longer listed the stick;
  macOS did not see it on USB either until it was unplugged and reinserted.
  Its EFI then still matched the manifest. Cutting power during a file write
  likely left the stick unresponsive; this is not confirmed.
- Change: file logging removed from the test EFI (see
  [What the test EFI changes](#what-the-test-efi-changes)). If a later boot
  stops before the picker again, a build with file logging is the diagnostic,
  preferably on a faster USB 3 stick and with several minutes' wait.

**Boot 2, 2026-10-05, stage 0** (manifest built after the file-logging fix;
`out/test-efi/usb-stage0/`).

- Partition 2 reached the OpenCore picker and macOS booted to the desktop.
- `kern.bootargs`: `-v keepsyms=1 debug=0x100 msgbuf=1048576 cezanne-stage=0`.
- Loaded: `org.cezanne-driver.gpu` 0.1.0 and Lilu; no NootedRed or Radeon kext.
- `ioreg`: `CezanneGPU` under `VGA@0` (bridge `GP17@8,1`) with `CezanneGPU
  stage` 0 and registry vendor `0x1002`, device `0x1638`, revision `0xc9`,
  subsystem `0x1002:0x1636`, class `0x030000`.
- Also attached to `VGA@0`: Apple's `AMDSupport` and the boot framebuffer
  `.Display_boot` (`IONDRVFramebuffer`). Either may hold the PCI device open;
  stage 1 now records that as `provider-open-failed` instead of the misleading
  `config-read-failed`, and reads nothing in that case.
- Display without NootedRed: 1920×1080 on the firmware framebuffer, 7 MB
  reported, no acceleration ("No Kext Loaded").
- The unified log has IOPCIFamily's `child CezanneGPU … published` and
  kernelmanagerd's load notification, but not the driver's `IOLog` lines.
  `sudo dmesg` held 131071 bytes starting at 195 s, so they had been
  overwritten. `CezanneGPU` is `registered`, so `start()` completed.
- IOPCIFamily logged the GPU at enumeration with command `0x0006` (memory
  decoding and bus mastering already on).
- Result: stage 0 succeeded.

**Boot 3, 2026-10-05, stage 1** (`out/test-efi/usb-stage1/`, kext with
`provider-open-failed`; cold boot after a full shutdown).

- Booted to the desktop; `kern.bootargs` ends `cezanne-stage=1`;
  `org.cezanne-driver.gpu` 0.1.0 loaded, no NootedRed.
- `ioreg` (`ioreg` prints 32-bit values with bit 31 set sign-extended; low
  32 bits shown):

  | Property | Value |
  | --- | --- |
  | `CezanneGPU stage 1 result` | `ok` |
  | `config command` | `0x0006` (memory decoding, bus master) |
  | `config status` | `0x0010` (capability list) |
  | `config pmcsr` | `0x0000` (D0) |
  | `config bar5` | `0xfca00000` |
  | `bar5 length` | `0x80000` (512 KiB) |
  | `MP0_SMN_C2PMSG_33` | `0x80000000` (IFWI ready) |
  | `RCC_CONFIG_MEMSIZE` | `0x800` (2048 MiB) |

- All match the expected values: the read-only BAR5 mapping works with
  AMDSupport and the boot framebuffer attached, and the firmware leaves the
  device in D0 with decoding on.
- `sudo dmesg | grep 'CezanneGPU:'` was empty about a minute after login, so
  the driver's `IOLog` lines may never reach the kernel buffer; not
  investigated. The registry is the record.
- Result: stage 1 succeeded.

**Boot 4, 2026-10-05, stage 2** (`out/test-efi/usb-stage2/`; cold boot; internal
EFI verified unchanged against the stage 2 manifest before it).

- Desktop reached; `kern.bootargs` ends `cezanne-stage=2`; driver loaded.
- `CezanneGPU stage 1 result` `ok` (values as boot 3); `CezanneGPU stage 2
  result` `ok`.

  | Property | Value |
  | --- | --- |
  | `MC_VM_FB_OFFSET` | `0x5c0` |
  | `carveout base` | `0x5c0000000` (2 GiB, ending at `0x640000000`, where BAR0 begins) |
  | `discovery address` | `0x63fff0000` |
  | `discovery signature` | `0x28211407` |
  | `discovery version` / `table version` | 1.1 / 2 |
  | `discovery IP count` | 41 |
  | `discovery GC version` | 9.3.0 |
  | `discovery GC base 0` / `1` | `0x2000` / `0xa000` |
  | `discovery MP0 base 0` | `0x16000` |

- The CPU can read the TMR region holding the table; no machine check. The
  base cross-check confirms `0xa5ac` is `MC_VM_FB_OFFSET`.
- `ioreg -a` capture and the extracted 1124-byte binary are in ignored
  `out/test-efi/boot-4-stage2/`. The decoded inventory is in the
  [target manifest](cezanne-target-manifest.md#measured-ip-inventory-stage-2-test-boot-2026-10-05).
- Result: stage 2 succeeded.

**Boot 5, 2026-10-05, stage 3** (`out/test-efi/usb-stage3/`; cold boot).

- Desktop reached; `kern.bootargs` ends `cezanne-stage=3`; driver loaded.
  Stage 1, 2 and 3 results all `ok`; stage 1 and 2 values as boots 3 and 4.

  | Register / derived | Value | Meaning |
  | --- | --- | --- |
  | `GRBM_STATUS` | `0x00003028` | `GUI_ACTIVE` clear: graphics idle |
  | `GRBM_GFX_INDEX` | `0x00000000` | SE 0 / SH 0 / instance 0, no broadcast bits |
  | `CC_GC_SHADER_ARRAY_CONFIG` | `0xff080000` | `INACTIVE_CUS` `0xff08`: CU 3 fused off; bits 8–15 beyond the 8 CUs |
  | `GC_USER_SHADER_ARRAY_CONFIG` | `0x00000000` | None disabled by software |
  | `CC_RB_BACKEND_DISABLE` / `GC_USER_RB_BACKEND_DISABLE` | `0` / `0` | No RB disabled |
  | `GB_ADDR_CONFIG` | `0x24000011` | `NUM_PIPES` 1, `PIPE_INTERLEAVE_SIZE` 2, `MAX_COMPRESSED_FRAGS` 0, `NUM_RB_PER_SE` 1, `ROW_SIZE` 2 |
  | `active CU mask` / `count` | `0xf7` / 7 | Matches the 5600GT's 7 CUs |
  | `active RB mask` / `count` | `0x3` / 2 | Matches the GC info table |

- `GB_ADDR_CONFIG` differs from Linux's Renoir golden value `0x24000042` in
  `NUM_PIPES` (1 vs 2), `PIPE_INTERLEAVE_SIZE` (2 vs 0) and
  `MAX_COMPRESSED_FRAGS` (0 vs 1). Linux v6.12 does not use the firmware's
  pipe, interleave or fragment fields, as traced offline afterwards (see the
  [target manifest](cezanne-target-manifest.md#gb_addr_config)).
- The `ioreg -a` capture is in ignored `out/test-efi/boot-5-stage3/`.
- Result: stage 3 succeeded.

**Boot 6, 2026-10-05, stage 4** (`out/test-efi/usb-stage4/`; cold boot).

- Desktop reached; `kern.bootargs` ends `cezanne-stage=4`. Stage 1, 2 and 3
  results `ok` with the boot 5 values; `CezanneGPU diagnostics` `true`.
- `cezanne-diag` without `sudo`: refused with `0xe00002c1`
  (`kIOReturnNotPrivileged`).
- `sudo cezanne-diag --repeat 3` (1 s apart): every register read `ok` in
  every pass, with values identical to the boot-time ones, including
  `GRBM_STATUS` `0x00003028` (idle) each time. No user client remained in
  the registry afterwards.
- Captures (`ioreg.plist`, `diag.txt`) are in ignored
  `out/test-efi/boot-6-stage4/`.
- Result: stage 4 succeeded.

**Boot 7, 2026-10-05, stage 5** (`out/test-efi/usb-stage5/`; cold boot).

- Desktop reached; `kern.bootargs` ends `cezanne-stage=5`; stages 1–3 `ok`,
  diagnostics `true`.
- `sudo cezanne-diag --repeat 3`: all 48 registers `ok` in every pass, no hang.
  The stage 1–3 registers matched boot 6. Only `MP0_SMN_C2PMSG_81` changed
  between passes (`0x0016b545`, `0x0016b5fb`, `0x0016b6b0`, about 1 s apart).
- Decoded with the v6.12 `*_sh_mask.h` fields (fields not listed are 0):

  | Area | Register: value | Reading |
  | --- | --- | --- |
  | GFX power | `SMUIO_GFX_MISC_CNTL` `0x00000005` | `PWR_GFXOFF_STATUS` 2: GFX on, not in GFXOFF |
  | SMU mailbox | `MP1_SMN_C2PMSG_66`/`82`/`90` `0`/`0`/`0x1` | No message pending; the last response was 1 (OK) |
  | PSP | `MP0_SMN_C2PMSG_81` non-zero, increasing | Secure OS sign of life: `psp_v12_0` skips loading the system driver and secure OS when it is non-zero |
  | PSP | `MP0_SMN_C2PMSG_35` `0xffffffff` | Bit 31 (bootloader ready) set; the all-ones value is unexplained |
  | GFX clock gating | `RLC_CGTT_MGCG_OVERRIDE` `0xffffffff`; `RLC_CGCG_CGLS_CTRL`(`_3D`) `0x0001003c` | Every MGCG override set; `CGCG_EN`, `CGLS_EN` 0: no GFX clock gating |
  | GFX memory light sleep | `RLC_MEM_SLP_CNTL`, `CP_MEM_SLP_CNTL` `0x00020200` | `*_MEM_LS_EN` 0: off (delays only) |
  | GFX power gating | `RLC_PG_CNTL` `0` | Off |
  | Command processor | `CP_ME_CNTL` `0x15000000`; `CP_MEC_CNTL` `0x50000000` | `ME_HALT`, `PFP_HALT`, `CE_HALT`, `MEC_ME1_HALT`, `MEC_ME2_HALT`: all halted |
  | RLC | `RLC_CNTL` `0`, `RLC_STAT` `0`; `CP_*_INSTR_PNTR` `0` | RLC not enabled; no CP microcode has run |
  | GRBM | `GRBM_STATUS2` `0x8`, `GRBM_STATUS_SE0` `0x6`, `CP_BUSY_STAT` `0`, `CP_CPF_STATUS` `0` | Idle |
  | SDMA0 | `SDMA0_F32_CNTL` `0x1`; `SDMA0_GFX_RB_CNTL` `0x00040000`; `SDMA0_STATUS_REG` `0x46dee557` | `HALT` 1, `RB_ENABLE` 0; every idle bit set |
  | SDMA0 gating | `SDMA0_CLK_CTRL` `0xff000100`; `SDMA0_POWER_CNTL` `0x40000050` | All `SOFT_OVERRIDE` bits set (no MGCG); `MEM_POWER_LS_EN` 0 |
  | HDP | `HDP_MEM_POWER_LS` `0x45504550` | `LS_ENABLE` 0 |
  | ATHUB | `ATHUB_MISC_CNTL` `0x200c0200` | `CG_ENABLE` 1, `CG_MEM_LS_ENABLE` 1: the only gating the firmware enabled |
  | MMHUB gating | `ATC_L2_MISC_CG` `0`; `DAGB0_CNTL_MISC2` `0x8888811f` | ATC L2 gating off; DAGB request/return clock gating disabled |
  | MMHUB FB | `MC_VM_FB_LOCATION_BASE`/`TOP` `0xf400`/`0xf47f` | GPU addresses `0xF400000000`–`0xF47FFFFFFF`: the 2 GiB carveout |
  | MMHUB VM | `VM_L2_CNTL` `0x00080602`; `VM_CONTEXT0_CNTL` `0x007ffe80`; `MC_VM_MX_L1_TLB_CNTL` `0x00002501` | `ENABLE_L2_CACHE` 0, `ENABLE_CONTEXT` 0; `ENABLE_L1_TLB` 1, `ATC_EN` 1 |
  | IH | `IH_RB_CNTL` `0x40610000` | `RB_ENABLE` 0: no interrupt ring |

- Summary: the firmware leaves every engine halted and idle, no microcode
  running in the CP or RLC, GPU VM context 0 and the MMHUB L2 off, almost all
  clock and power gating off, GFX powered on, the SMU idle with an OK last
  response, and the PSP secure OS already running.
- Captures (`ioreg.plist`, `diag.txt`) are in ignored
  `out/test-efi/boot-7-stage5/`.
- Result: stage 5 succeeded.

**Boot 8, 2026-10-05, stage 6** (`out/test-efi/usb-stage6/`; cold boot).

- Desktop reached; `kern.bootargs` ends `cezanne-stage=6`; stages 1–3 `ok`,
  diagnostics `true`.
- `sudo cezanne-diag --scratch-test`:

  | Step | Result |
  | --- | --- |
  | 1 check | `ok`: `SMUIO_GFX_MISC_CNTL` `0x5`, `CP_ME_CNTL` `0x15000000`, `CP_MEC_CNTL` `0x50000000`, `RLC_CNTL` `0`, `GRBM_STATUS` `0x3028`; `SCRATCH_REG0` original `0x00000000`, stable |
  | 2 write | `ok`: wrote `0xcafedead`, read back `0xcafedead` |
  | 3 restore | `ok`: wrote `0x00000000`, read back `0x00000000` |

- The 49-register dump that followed matched boot 7 in all 48 shared
  registers except `MP0_SMN_C2PMSG_81` (the PSP's running counter), and
  `SCRATCH_REG0` read `0x00000000`. No hang; no user client remained; no
  abandoned-restore property was set.
- First register write to this GPU from the driver: the write path, the
  writable page mapping and the restore work as designed, and the GPU state
  is unchanged afterwards.
- Captures (`diag.txt`, `ioreg.plist`) are in ignored
  `out/test-efi/boot-8-stage6/`.
- Result: stage 6 succeeded.

**Boot 9, 2026-10-05, stage 7** (`out/test-efi/usb-stage7/`; cold boot after
a software shutdown of the stage 6 session at 15:34).

- **First attempt: failed.** Reported by the user: the boot failed and the PC
  rebooted by itself. The second attempt (kernel up 15:35:43) reached the
  desktop.
  - The first attempt left no macOS evidence: no kernel log between the
    15:34:31 logout and the 15:35:43 boot, no panic report, and `DumpPanic`
    processed 0 files.
  - `Previous shutdown cause` was 5, the clean shutdown of the stage 6
    session.
  - So the reset most likely happened before macOS logging starts: in the
    firmware, OpenCore or early kernel. That is before the driver runs.
  - Stage 7's boot-time code path does the same as stage 6's; SMU messages
    are sent only on request.
  - **Cause unknown and not reproduced.** It is not attributed to the driver,
    but that is not excluded. Earlier USB boots also had firmware-level
    hiccups (boot 1).
  - If it recurs, note the last screen (firmware logo, OpenCore picker, or
    verbose text) and photograph it.
- **Second attempt:** `kern.bootargs` ends `cezanne-stage=7`; stages 1–3
  `ok`; diagnostics v3.
- `sudo cezanne-diag --smu-query`:

  | Step | Result |
  | --- | --- |
  | 1 check | `ok`: `C2PMSG_66` `0`, `C2PMSG_82` `0`, `C2PMSG_90` `0x1` (idle) |
  | 2 `GetDriverIfVersion` | `ok`, response `0x01`, answer `0x0000000e` = **14**, equal to `SMU12_DRIVER_IF_VERSION` |
  | 3 `GetSmuVersion` | `ok`, response `0x01`, answer `0x00404a00` = **SMU firmware 64.74.0** (program 0) |

- The register dump that followed matched boot 8 except, as expected, the
  mailbox holding the last exchange (`C2PMSG_66` `0x2`, `C2PMSG_82`
  `0x00404a00`, `C2PMSG_90` `0x1`) and the PSP counter.
- The SMU answered within the poll limit. The user confirmed the fans behaved
  normally afterwards. Their temperature monitor ("Hot") showed temperatures
  well below usual, consistent with nothing driving the GPU in a test boot
  (no NootedRed, graphics engine never started).
- Captures (`diag.txt`, `ioreg.plist`) are in ignored
  `out/test-efi/boot-9-stage7/`.
- Result: stage 7 succeeded on the second attempt; the first attempt's reset
  is an open finding.

**Boot 10, 2026-10-05, stage 8** (`out/test-efi/usb-stage8/`; cold boot;
booted on the first attempt).

- Kernel up 15:48:30; `kern.bootargs` ends `cezanne-stage=8`; stages 1–3
  `ok`; diagnostics v4.
- `sudo cezanne-diag --gfxoff-disallow`, written 15:49:46:

  | Step | Result |
  | --- | --- |
  | 1 check | `ok`: `C2PMSG_66` `0`, `C2PMSG_82` `0`, `C2PMSG_90` `0x1` (idle) |
  | 2 `DisallowGfxOff` (`0x8`) | `ok`, response `0x01`; `SMUIO_GFX_MISC_CNTL` `0x5`, `PWR_GFXOFF_STATUS` 2 on the first read |

- The register dump matched boot 9 except the mailbox (`C2PMSG_66` `0x8`,
  `C2PMSG_82` `0`, `C2PMSG_90` `0x1`) and the PSP counter. GFXOFF status was
  2 before and after, so as predicted the message confirmed the existing
  state.
- **Temperature.** The user saw the CPU temperature rise sharply during boot,
  to the 70s °C on reaching the desktop, then fall to about 45 °C (user's
  "Hot" monitor).
  - The rise began before the message: the kernel started at 15:48:30, and the
    message went out at 15:49:46, after the desktop appeared.
  - At 15:51 the load average was 15.9 / 13.2 / 5.9 (1/5/15 min) and
    `WindowServer` used about 85 % CPU. Without a graphics driver, the
    desktop is composited on the CPU, which explains heavy CPU use at boot and
    login.
  - The heat is therefore attributed to boot load and software compositing,
    not to `DisallowGfxOff`; the message did not change GFX state. Not
    measured: whether earlier test boots peaked the same way (they were not
    watched during boot).
- Captures (`diag.txt`, `ioreg.plist`) are in ignored
  `out/test-efi/boot-10-stage8/`.
- Result: stage 8 succeeded.

**Boot 11, 2026-10-05, stage 9** (`out/test-efi/usb-stage9/`; cold boot,
kernel up 16:06:52).

- `kern.bootargs` ends `cezanne-stage=9`; stages 1–3 `ok`; diagnostics v5.
- `sudo cezanne-diag --smu-metrics`, written 16:08:06:
  - Step 1 read `MC_VM_FB_LOCATION_BASE` `0xf400` and `MC_VM_FB_OFFSET`
    `0x5c0`; the GPU and physical targets `0xf440000000` and `0x600000000`
    were computed as designed.
  - It then returned **`table-region-in-use`**: the 64 KiB at `0x600000000`
    was not all zero.
  - Steps 2 and 3 did not run. No SMU message was sent and nothing was
    written. The mailbox read `0`/`0`/`0x1` (untouched), and the register dump
    matched boot 10 apart from the mailbox and the PSP counter.
- **Finding: "all zero" is the wrong test for "unused".**
  - Firmware does not clear the carveout, and DRAM does not come up zeroed
    after a power cycle.
  - In normal boots the GPU driver uses the whole carveout for its buffers.
  - So unused carveout memory may hold arbitrary stale data. Non-zero content
    does not show that the page is in use, and zero content would not prove
    that it is free.
  - Linux does not test content. It reserves the firmware-used region from the
    VBIOS (`vram_usagebyfirmware`), the discovery binary and the boot
    framebuffer, and treats the rest as free.
- **VBIOS not available here.** macOS 26 publishes neither the ACPI tables
  (no `VFCT`) nor a VBIOS image in the registry in this boot, so the firmware
  usage table could not be read from user space.
- **Temperature.** The user reports every boot runs hot and then cools; see
  boot 10.
- Captures (`diag.txt`, `ioreg.plist`) are in ignored
  `out/test-efi/boot-11-stage9/`.
- Result: **stopped safely before any write**. Stage 9 needs a revised,
  reviewed check before another attempt.

**Boot 13, 2026-10-05, stage 9 with the revised check**
(`out/test-efi/usb-stage9/`; cold boot, kernel up 16:27:19).

- `kern.bootargs` ends `cezanne-stage=9`; stages 1–3 `ok`.
- `sudo cezanne-diag --smu-metrics`, written 16:28:17:
  1. **Check:** `ok`. FB location base `0xf400`, FB offset `0x5c0`, GPU
     `0xf440000000`, physical `0x600000000`; the 64 KiB was unchanged over
     about 1 s.
  2. **Transfer:** `ok`. `SetDriverDramAddrHigh`, `SetDriverDramAddrLow` and
     `TransferTableSmu2Dram` each answered `0x01`.
  3. **Read:** `ok`. The 148 table bytes changed and bytes 148–4095 were
     identical to the snapshot.
- **First GPU-side write to memory.** The SMU wrote exactly where the
  address translation predicted, and nowhere else on the page.
- **Decoded metrics** (taken about 1 minute after boot, CPU still busy):

  | Reading | Value |
  | --- | --- |
  | GFX clock (current / average) | 400 / 400 MHz |
  | GFX activity | 0.00 % (graphics engine idle) |
  | GFX / SoC temperature | 43.75 / 43.00 °C |
  | Other clocks | SOCCLK 400, FCLK 1333, DCFCLK 400, DISPCLK 200, DPPCLK 200, DPREFCLK 600, VCLK/DCLK 400, LCLK 400, MP0/MP1/MP2 300/400/100, SHUBCLK 200, ACLK 200, ISPCLK 0, UMCCLK 6 (unit unclear) |
  | CPU cores (MHz / mW / °C) | six active at 4650 MHz, 4.2–8.8 W, 55.5–67.5 °C; cores 2 and 4 report 0 MHz / 0 mW (the 5600GT's 6 of 8 cores) at about 44.5 °C |
  | L3 | 4650 MHz, 48.0 °C (second L3 slot 0) |
  | VDD (CPU) | 1287 mV, 25.7 A, 33.1 W |
  | VDD SoC | 849 mV, 2.06 A, 1.75 W |
  | Socket / APU power | 37 W / 34 W |
  | Fan PWM | 50 980 milli (about 51 %) |
  | STAPM limit | 87 W (original 88 W) |
  | Throttler status | `0x0800`: bit 11 `EDC_CPU` (CPU current limit), consistent with all-core boost under load |
  | TDC VDD / SoC | 23 325 / 2 137 mA |
  | EDC VDD / SoC | 18 465 / 9 796 mA |

  - The CPU at 4.65 GHz on all six cores, at 1.29 V and 33 W, confirms the
    boot-time load seen in boot 10.
  - The GPU is idle at its minimum clock, around 44 °C.
- The register dump matched boot 10 except the mailbox (`C2PMSG_66`
  `0x1c`, `C2PMSG_82` `0x7`, `C2PMSG_90` `0x1`) and the PSP counter.
- Captures (`diag.txt`, `ioreg.plist`) are in ignored
  `out/test-efi/boot-13-stage9/`.
- Result: stage 9 succeeded.

**Boot 14, 2026-10-06, stage 10** (`out/test-efi/usb-stage10/`; cold boot,
kernel up 08:13:53 local).

- `CezanneGPU stage` 10; stages 1–3 `ok`.
- `sudo cezanne-diag --gfxoff-disallow --psp-state`, run about 2 minutes
  after boot:
  - **DisallowGfxOff:** check `ok`, response `0x01`, `PWR_GFXOFF_STATUS` 2,
    so the GC-hub registers were readable.
  - **PSP ring mailbox:** `C2PMSG_64` `0x80000000`: response flag set,
    status 0, so the secure OS is **ready** for ring commands. `C2PMSG_67`,
    `69`, `70` and `71` are all 0, so **no ring exists** after a cold boot.
    `C2PMSG_36` reads `0xffffffff` (bootloader argument, unused while the
    secure OS runs; same pattern as `C2PMSG_35`).
  - **MMHUB and GC hub are programmed identically:**
    - FB `0xF400000000`–`0xF47FFFFFFF` maps to physical `0x5C0000000`, and
      both hubs read FB offset `0x5c0`.
    - The system aperture is `0xF400000000`–`0xF48003FFFF`. Its high
      register, `0x3d2000`, is `(fb_end >> 18) + 1`: the Renoir/Green
      Sardine workaround in `mmhub_v1_0_init_system_aperture_regs`, already
      set by firmware.
    - AGP is unused (base, bottom and top all 0).
    - The system-aperture default page is 0 (LSB and MSB). Linux points it at
      a scratch page. Here, a stray access inside the aperture that misses FB
      would go to physical 0. **This is a finding to handle before any
      engine runs its own memory accesses.**
- The stage 9 register dump is unchanged from boot 13, except the mailbox
  (`0x8`/`0`/`0x1` from `DisallowGfxOff`) and the PSP counter.
- Captures (`diag.txt`, `ioreg.plist`) are in ignored
  `out/test-efi/boot-14-stage10/`.
- Result: stage 10 succeeded. The PSP starts clean, which is the case the
  stage 10 section names for proposing stage 11.

**Boot 15, 2026-10-06, stage 11** (`out/test-efi/usb-stage11/`; cold boot,
kernel up 08:31:23 local).

- `CezanneGPU stage` 11; stages 1–3 `ok`.
- `sudo cezanne-diag --gfxoff-disallow --psp-ring --psp-state`:
  - **DisallowGfxOff:** `ok`, response `0x01`.
  - **Check:** `ok`. `C2PMSG_81` `0x0016dae2`, `C2PMSG_64` `0x80000000`, no
    ring, and the 64 KiB at the ring page stable.
  - **Create step, first command** (`GBR_IH_SET` VMC: `C2PMSG_69` ← 3,
    `C2PMSG_70` ← `0x0015244b`, `C2PMSG_64` ← `0x00080000`): answered
    `0x80080100`. That is: answered, command ID 8 echoed, status `0x0100`
    `PSP_ERR_UNKNOWN_COMMAND`. Stopped as `psp-response-not-ok`.
  - The UMC reroute and the create were **not sent**. With no ring,
    observe and destroy were skipped, as designed.
- **Afterwards:** `C2PMSG_64` `0x80080100`, `C2PMSG_69` 3, `C2PMSG_70`
  `0x0015244b`, `C2PMSG_67`/`71` 0. The secure OS is still alive:
  `C2PMSG_81` advanced to `0x0016dbc7`. The rest of the dump equals boot 14,
  and the machine stayed as before.
- Captures (`diag.txt`, `ioreg.plist`) are in ignored
  `out/test-efi/boot-15-stage11/`.
- Result: **stopped safely after one rejected command.** This secure OS does
  not implement `GBR_IH_SET`. The response check is stricter than Linux's
  (it does not mask the echoed command ID). See the
  [proposed revision](#revision-stage-11-psp-responses-proposal).

**Boot 16, 2026-10-06, revised stage 11** (`out/test-efi/usb-stage11/`;
cold boot, kernel up 08:44:13 local).

- `CezanneGPU stage` 11, diagnostics v7; stages 1–3 `ok`.
- `sudo cezanne-diag --gfxoff-disallow --psp-ring --psp-state`:
  - **DisallowGfxOff:** `ok`, response `0x01`.
  - **Check:** `ok`. `C2PMSG_64` `0x80000000` (clean after the cold boot,
    so boot 15's rejected command did not persist), no ring, and the 64 KiB
    at the ring page stable.
  - **Create:** `ok`, response **`0x80020000`**: answered, create ID 2
    echoed, status 0. This confirms the echo boot 15 revealed. The first
    stage 11 build would have called this a failure.
  - **Observe:** `ok`. `C2PMSG_69`/`70`/`71` = `0x40100000`/`0xf4`/`0x1000`
    and `C2PMSG_67` 0. **0 changed words** in the ring page and in the 60 KiB
    after it: the PSP did not write the ring memory.
  - **Destroy:** `ok`, response **`0x80030000`** (destroy ID 3 echoed,
    status 0).
- **Finding: the argument registers are not ring state.** After the destroy,
  `C2PMSG_69`/`70`/`71` still hold the ring's address and size. They are
  plain mailbox arguments that the PSP leaves as written. So:
  - the `--psp-state` summary's "address or size set" does not mean a ring
    exists;
  - stage 11's no-ring check would refuse a second create in the same boot.
  - Neither matters for a cold-booted test.
- The rest of the dump equals boot 14, except the PSP counter (`C2PMSG_81`).
  The machine stayed as before.
- Captures (`diag.txt`, `ioreg.plist`) are in ignored
  `out/test-efi/boot-16-stage11/`.
- Result: **stage 11 succeeded.** This is the first PSP command accepted
  from this driver: a kernel-mode ring at GPU `0xF440100000`, created and
  destroyed, with no frame and no memory change.

**Boot 17, 2026-10-06, stage 12** (`out/test-efi/usb-stage12/`; cold boot,
kernel up 09:00:52 local).

- `CezanneGPU stage` 12, diagnostics v8; stages 1–3 `ok`.
- `sudo cezanne-diag --gfxoff-disallow --psp-tmr --psp-state`:
  - **DisallowGfxOff:** `ok`.
  - **Check:** `ok`. PSP ready, no ring, the 64 KiB at the ring stable, and
    the 4 MiB TMR region placed and stable.
  - **Create:** `ok`, `0x80020000`.
  - **Submit `SETUP_TMR`:** `ok`.
    - The command (TMR GPU `0xF440400000`, physical `0x600400000`, 4 MiB)
      went in as frame 0, and `C2PMSG_67` ← 16.
    - **Fence 1 arrived, with response status 0.** `fw_addr` and `tmr_size`
      are 0, as expected: those fields answer `LOAD_IP_FW` and `LOAD_TOC`.
  - **Observe:** `ok`, **0 unexpected words** in the 64 KiB. The PSP wrote
    only the fence and, at most, its response area.
  - **Teardown:** `ok`. `DESTROY_TMR` went in as frame 1 (`C2PMSG_67` ← 32)
    and fence 2 arrived with status 0. The ring destroy answered
    `0x80030000`.
- **Afterwards:** `C2PMSG_67` stays 32 after the destroy, like the argument
  registers in boot 16: the PSP leaves the write pointer as written. The rest
  of the dump equals boot 16 except the PSP counter. The machine stayed as
  before.
- Captures (`diag.txt`, `ioreg.plist`) are in ignored
  `out/test-efi/boot-17-stage12/`.
- Result: **stage 12 succeeded.**
  - **First CPU writes to carveout memory:** the command buffer, the fence
    buffer and two ring frames.
  - **First ring frames processed by the PSP:** `SETUP_TMR` and
    `DESTROY_TMR` both fenced with status 0.
  - The full submit protocol (frame, write pointer, fence, response) works
    as Linux implements it.

**Boot 18, 2026-10-06, stage 13** (`out/test-efi/usb-stage13/`, written
with `tools/update_stick.sh 13`; cold boot, kernel up 09:42:22 local).

- `CezanneGPU stage` 13, diagnostics v9; stages 1–3 `ok`.
- `sudo cezanne-diag --gfxoff-disallow --psp-sdma --psp-state`:
  - **DisallowGfxOff:** `ok`.
  - **Check:** `ok`. PSP, ring, TMR and firmware-buffer regions, and the
    embedded image's header.
    - Before: `SDMA0_UCODE_CHECKSUM` `0x00000000`, `SDMA0_F32_CNTL`
      `0x00000001` (halted).
  - **Create:** `0x80020000`.
  - **`SETUP_TMR`:** fence 1, status 0, `C2PMSG_67` 16.
  - **Load:** `ok`. The 17,152-byte image was copied to `0xF440200000` and
    read back. `LOAD_IP_FW` (type 9) as frame 1: **fence 2, status 0**,
    `C2PMSG_67` 32.
    - `fw_addr` came back 0, so this PSP does not report a TMR location for
      the load. That is recorded, not an error: Linux only stores the
      value.
  - **Observe:** `ok`.
    - **0 unexpected words** in the work area and in the firmware region.
    - **`SDMA0_UCODE_CHECKSUM` changed from 0 to `0x25a1ba79`:** the SDMA
      engine received firmware.
    - `SDMA0_F32_CNTL` is still `0x00000001` (halted).
  - **Teardown:** `DESTROY_TMR` as frame 2, **fence 3, status 0**. The ring
    destroy answered `0x80030000`.
- **Afterwards:**
  - `SDMA0_UCODE_CHECKSUM` stays `0x25a1ba79` after the TMR is destroyed.
  - **`SDMA0_CLK_CTRL` changed from `0xff000100` to `0xdf000100`:** bit 29,
    `SOFT_OVERRIDE2`, was cleared by the PSP or the firmware load, not by
    the driver. This is a clock-gating override; record it for the
    clock-gating stage.
  - Otherwise the dump equals boot 17, except the PSP counter and
    `C2PMSG_67` (48). The machine stayed as before.
- Captures (`diag.txt`, `ioreg.plist`) are in ignored
  `out/test-efi/boot-18-stage13/`.
- Result: **stage 13 succeeded.** It was the first firmware load: the PSP
  accepted the pinned SDMA0 image (signature and all) and installed it, and
  SDMA0 stayed halted. `tools/update_stick.sh` was used for the first time
  and verified the stick.

**Boot 19, 2026-10-06, stage 14** (`out/test-efi/usb-stage14/`, written
with `tools/update_stick.sh 14`; cold boot, kernel up 09:56:34 local).

- `CezanneGPU stage` 14, diagnostics v10; stages 1–3 `ok`.
- The stage 13 steps repeated boot 18 exactly:
  - check, create `0x80020000`, `SETUP_TMR` fence 1, `LOAD_IP_FW` fence 2;
  - status 0 throughout;
  - 0 unexpected words; checksum 0 → `0x25a1ba79`; still halted.
- **Inventory:** `ok`. **`PowerUpSdma` → `0x01`, `PowerDownSdma` →
  `0x01`.** All 31 registers read **the same in all three passes**: power
  gating through the SMU changed none of them, and the firmware checksum
  survived both messages.

  | Register | Value |
  | --- | --- |
  | `F32_CNTL` | `0x00000001` (halted) |
  | `CLK_CTRL` | `0xdf000100` |
  | `POWER_CNTL` | `0x40000050` |
  | `STATUS_REG` | `0x46dee557` |
  | `GFX_RB_CNTL` | `0x00040000` |
  | `UCODE_CHECKSUM` | `0x25a1ba79` |
  | `CNTL` | `0x00000002` |
  | `CHICKEN_BITS` | `0x00831f07` |
  | `GB_ADDR_CONFIG`, `_READ` | `0x00100012` |
  | `SEM_WAIT_FAIL_TIMER_CNTL` | `0x00000000` |
  | `UTCL1_WATERMK` | `0xfffbe1fe` |
  | `UTCL1_TIMEOUT` | `0x00010001` |
  | `UTCL1_PAGE` | `0x000003e0` |
  | `GFX_RB_BASE`/`_HI`, `RPTR`/`_HI`, `WPTR`/`_HI`, `RPTR_ADDR_HI`/`_LO` | `0` |
  | `GFX_RB_WPTR_POLL_CNTL`, `RLC0`/`RLC1_RB_WPTR_POLL_CNTL` | `0x00401000` |
  | `GFX_IB_CNTL` | `0x00000100` |
  | `GFX_DOORBELL`, `_OFFSET`, `WPTR_POLL_ADDR_HI`/`_LO`, `MINOR_PTR_UPDATE` | `0` |

- **What `golden_settings_sdma_4_3` would write from these values**
  (`soc15_program_register_sequence` arithmetic):

  | Register | Now | Golden result |
  | --- | --- | --- |
  | `CHICKEN_BITS` | `0x00831f07` | `0x02831f07` |
  | `CLK_CTRL` (plain write) | `0xdf000100` | `0x3f000100` |
  | `GB_ADDR_CONFIG`, `_READ` | `0x00100012` | `0x00000002` |
  | `GFX_RB_WPTR_POLL_CNTL`, `RLC0`/`RLC1_…` | `0x00401000` | `0x00403000` |
  | `POWER_CNTL` | `0x40000050` | `0x40000051` |
  | `UTCL1_WATERMK` | `0xfffbe1fe` | `0x03fbe1fe` |
  | `UTCL1_PAGE` | `0x000003e0` | unchanged |

  `SDMA0_GB_ADDR_CONFIG` differs from its golden value in the same way the
  GC `GB_ADDR_CONFIG` did at stage 3 (`NUM_PIPES` and related fields). That
  is for review in the stage 15 proposal.
- The teardown fenced 3 and the ring was destroyed, as in boot 18. The
  machine stayed as before.
- Captures (`diag.txt`, `ioreg.plist`) are in ignored
  `out/test-efi/boot-19-stage14/`.
- Result: **stage 14 succeeded.** Every value stage 15 needs is now
  measured.

## Unknowns and limits

- The firmware lists both partitions `eraseDisk … GPT` creates. Partition 1 is
  the empty EFI system partition and fails; partition 2 (`CZTEST`) starts
  OpenCore.
- Where the driver's `IOLog` output goes on macOS 26 is unknown: it reached
  neither the unified log nor, a minute after login, `dmesg`.
- The preparation checks prove the configs and files differ only as intended
  and that OpenCore's validator accepts them; they do not prove the boot works.
- The known-good copy was taken from a read-write mount the user created; no
  file was written to it, and `verify` matched afterwards.
