# USB test boot

Status, 2026-10-06: **stages 0 to 16 succeeded** (see
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
inventory is recorded. Stage 15 (the first SDMA copy) ran in boot 20:
SDMA0 started and stopped cleanly, but the ring test timed out because the
driver wrote only the low half of the write pointer. With that fixed,
**boot 21 completed the first verified DMA copy and fence**: SDMA0 wrote
`0xDEADBEEF`, copied 4 KiB exactly and signalled fence 1, and nothing else
in the checked region changed. Stage 16 (a read-only display, VM and
interrupt inventory) succeeded in boot 22, apart from one semaphore register
whose read has a side effect, now removed from the list. Stage 17 (GART and
the interrupt ring) succeeded in boot 25: the MMHUB GART translated an SDMA
read through a driver-built page table, the SDMA0 trap arrived in IH ring 0,
and every register was restored. Boot 24 had stopped at the precondition
check on a live display status bit, since fixed. Stage 18 (interrupt
delivery) succeeded in boot 27: SDMA0's trap reached the kext's handler as
an MSI 31 µs after the write pointer, the acknowledgement caused no re-fire,
and everything was restored. Boot 26 had counted an extra MSI at the
`ENABLE_INTR` write; the verify now counts only MSIs after the submit.
Stage 19 (a display test pattern) is approved, and its implementation is in
progress; no later stage is authorized.

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
   - **Stage 16** (proposed 2026-10-06 and approved by the user the same
     day): stage 15 plus 92 read-only display, memory-hub VM and interrupt
     registers, read only through the diagnostic interface, described
     [below](#stage-16-display-vm-and-interrupt-inventory-proposal).
   - **Stage 17** (proposed 2026-10-06 and approved by the user the same
     day; implemented and built the same day, not yet booted): stage 16 plus GART (MMHUB context 0) and IH
     ring 0 with values pinned from boot 22, proven by an SDMA copy through
     GART with a fence and a trap, then restored. Described
     [below](#stage-17-gart-and-the-interrupt-ring-proposal).
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

### Fixing defects inside a stage

Set by the user on 2026-10-06:
- **Fix, document, retest.** When a test boot of an approved stage exposes
  a defect in its implementation, fix it, rebuild, document the finding and
  the fix here (the boot log entry plus a "Revision" note), and ask for
  another test boot. No proposal or separate approval is needed.
- **Stay within scope.** A fix must stay inside the approved stage: its
  registers, values, messages and memory. Anything beyond that is a new
  stage and still needs a proposal.

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

**Status: proposed 2026-10-06 and approved by the user the same day (golden
`GB_ADDR_CONFIG` applied, default page left for later). Implemented; boot 20
found the write-pointer defect, and with the fix it succeeded in boot 21.**

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

### Revision: stage 15 write-pointer commit

**Status: fixed 2026-10-06 after boot 20; it succeeded in boot 21.** It
was written as a proposal, then applied directly under the rule the user
set the same day (see [Rules](#rules)): defects found while testing an
approved stage are fixed, documented and retested.

**As implemented:**
- `submitSdma` writes `GFX_RB_WPTR`, then `GFX_RB_WPTR_HI` ← 0.
- The submit selector also returns `F32_CNTL` and `STATUS_REG`.
  Diagnostics version 12.
- **Tests:**
  - The fake SDMA engine commits the write pointer only on the `_HI`
    write.
  - The test checks the exact two-write order.
  - A weakened core without the `_HI` write fails.
  - The fake writer's log grew from 32 to 64 entries, after the sanitizer
    caught a test reading past it.
- The first stage 15 build is kept as `superseded-*-stage15-wptr-lo` in
  `out/test-efi/`.

**What boot 20 showed.**
- The load, the copy check and the start all passed: progress 2,
  `PowerUpSdma` `0x01`, and every golden and start value read back as
  written.
- The ring test then timed out: `observed` 0, `GFX_RB_RPTR` 0, and
  **`GFX_RB_WPTR` read 0 after the driver wrote 1024**.
- The stop halted SDMA0 (`F32_CNTL` 1) and powered it down (`0x01`); the
  teardown fenced 3.

**Cause (a defect in stage 15 as built).** Linux's non-doorbell path,
`sdma_v4_0_ring_set_wptr`, writes `GFX_RB_WPTR` (low dword) **and then
`GFX_RB_WPTR_HI`**. `gfx_resume` does the same.
- `submitSdma` wrote only the low dword.
- The low register reading back 0 fits a write pointer that the hardware
  commits only when the high half is written.
- The engine therefore never saw new work. That is consistent with the
  read pointer staying 0 and nothing changing in memory.

**Proposed change.**
- `submitSdma` writes `GFX_RB_WPTR` ← 1024 (then 2048), followed by
  `GFX_RB_WPTR_HI` ← 0, exactly as Linux does. `GFX_RB_WPTR_HI` ← 0 is
  already in the approved stage 15 table, so no new value is added.
- For the record, the submit step also returns `F32_CNTL` and `STATUS_REG`
  read after the poll.
- **Tests:**
  - The submit's exact two-write sequence.
  - The fake engine commits the write pointer only on the `_HI` write,
    modelling what boot 20 showed.
  - One weakened core (the `_HI` write removed) must fail.
- Nothing else changes.

### Stage 16: display, VM and interrupt inventory (proposal)

**Status: proposed 2026-10-06, approved by the user the same day,
implemented, and succeeded in boot 22.** The `VM_INVALIDATE_ENG17_SEM` read
had a side effect and that register was removed afterwards; see the
revision.

**Implementation notes:**
- **Core:** the 92 constants and the three group arrays were generated
  from the headers. `kStage16Registers` extends the stage 14 list (stage 15
  added no reads).
- **Tool:** `--inventory16` reads the groups, re-reads the display, prints
  changes, and decodes each pipe (OTG enable, totals, active size from the
  blank start and end, HUBP blank, surface address, viewport, pitch,
  format).
- **Tests:**
  - Prefix, uniqueness, inside BAR5, refused at stage 15, not writable,
    not GFX-gated, and group order.
  - `tests/test_inventory16.py` recomputes every offset from the pinned
    headers in `out/references` (skipped when they are absent).

**Purpose.** This prepares three milestones in one read-only boot: display,
GART, and interrupts. The user chose this approach on 2026-10-06:
- **Measure now:** stage 16 measures everything the next writes depend on,
  in one boot.
- **Write next:** stage 17 turns on GART and the IH ring with values pinned
  from these readings, as stages 14 and 15 did for SDMA.
- **Why measure first:** Linux's GART enable (`mmhub_v1_0_gart_enable`, about
  30 writes) and IH setup (`vega10_ih_irq_init`, about 15) are mostly
  read-modify-write, and most of those registers have never been read on
  this host.
- **No writes:** stage 16 adds no write, message or memory access.

**Bases.** The DMU bases measured in the stage 2 discovery capture (DMU
2.1.0: `0x12`, `0xC0`, `0x34C0`, `0x9000`, `0x2403C00`) and NBIF (2.5.0:
`0x0`, `0x14`, `0xD20`, `0x10400`, …) equal `renoir_ip_offset.h`. MMHUB
(`0x1A000`) and OSSSYS (`0x10A0`) are as before. Every offset below is
inside BAR5.

**A. Display (DCN 2.1), 55 registers**, from `dcn_2_1_0_offset.h`. These
are what the display code reads to reconstruct the state the firmware (GOP)
left. Pipe 0 is shown; pipes 1–3 repeat the same eleven registers at the
header's instance offsets.

| Register | BAR5 byte offset | Linux v6.12 use |
| --- | --- | --- |
| `OTG0_OTG_CONTROL` | `0x14004` | optc1_read_otg_state |
| `OTG0_OTG_H_TOTAL` | `0x13fa8` | optc1_read_otg_state |
| `OTG0_OTG_V_TOTAL` | `0x13fbc` | optc1_read_otg_state |
| `OTG0_OTG_H_BLANK_START_END` | `0x13fac` | optc1_read_otg_state |
| `OTG0_OTG_V_BLANK_START_END` | `0x13fd8` | optc1_read_otg_state |
| `HUBP0_DCHUBP_CNTL` | `0x0eacc` | hubp2_read_state |
| `HUBP0_DCSURF_SURFACE_CONFIG` | `0x0ea94` | hubp2_read_state |
| `HUBP0_DCSURF_PRI_VIEWPORT_DIMENSION` | `0x0eaa8` | hubp2_read_state |
| `HUBPREQ0_DCSURF_SURFACE_PITCH` | `0x0eb1c` | hubp2_read_state |
| `HUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS` | `0x0eb28` | hubp2_read_state |
| `HUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH` | `0x0eb2c` | hubp2_read_state |
| `DCN_VM_FB_LOCATION_BASE` | `0x0e54c` | hubbub21_init_dchub |
| `DCN_VM_FB_LOCATION_TOP` | `0x0e550` | hubbub21_init_dchub |
| `DCN_VM_FB_OFFSET` | `0x0e554` | hubbub21_init_dchub |
| `DCN_VM_AGP_BASE` | `0x0e560` | hubbub21_init_dchub |
| `DCN_VM_AGP_BOT` | `0x0e558` | hubbub21_init_dchub |
| `DCN_VM_AGP_TOP` | `0x0e55c` | hubbub21_init_dchub |
| `DIG0_DIG_BE_CNTL` | `0x155bc` | dcn10 link encoder state |
| `DIG1_DIG_BE_CNTL` | `0x159bc` | dcn10 link encoder state |
| `DIG2_DIG_BE_CNTL` | `0x15dbc` | dcn10 link encoder state |
| `DIG3_DIG_BE_CNTL` | `0x161bc` | dcn10 link encoder state |
| `DIG4_DIG_BE_CNTL` | `0x165bc` | dcn10 link encoder state |

The questions they answer:
- **Pipes and timing:** which of the four OTG/HUBP pipes is enabled, and
  the timing (total, blank start and end) of the active mode.
- **Scanout surface:** where the scanout surface is (primary surface
  address, which should be the GOP framebuffer, carveout offset 0) and its
  pitch, format and viewport.
- **Display addressing:** how DCN's own view of FB and AGP is set (the
  `DCN_VM_*` registers).
- **Connector:** which DIG back end (connector) is in use.

**B. Memory hub VM (GART path), 24 registers**, from `mmhub_1_0_offset.h`
(base `0x1A000`). These add to the MMHUB registers read since stages 5
and 10.

| Register | BAR5 byte offset | Linux v6.12 use |
| --- | --- | --- |
| `VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32` | `0x69cac` | init_gart_aperture_regs |
| `VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32` | `0x69cb0` | init_gart_aperture_regs |
| `VM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32` | `0x69d2c` | init_gart_aperture_regs |
| `VM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32` | `0x69d30` | init_gart_aperture_regs |
| `VM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32` | `0x69dac` | init_gart_aperture_regs |
| `VM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32` | `0x69db0` | init_gart_aperture_regs |
| `VM_L2_PROTECTION_FAULT_DEFAULT_ADDR_LO32` | `0x69a38` | init_system_aperture_regs |
| `VM_L2_PROTECTION_FAULT_DEFAULT_ADDR_HI32` | `0x69a3c` | init_system_aperture_regs |
| `VM_L2_PROTECTION_FAULT_CNTL` | `0x69a1c` | mmhub_v1_0_set_fault_enable_default |
| `VM_L2_PROTECTION_FAULT_CNTL2` | `0x69a20` | init_system_aperture_regs |
| `VM_L2_PROTECTION_FAULT_STATUS` | `0x69a2c` | gmc_v9_0_process_interrupt |
| `VM_L2_CNTL2` | `0x69a04` | init_cache_regs |
| `VM_L2_CNTL3` | `0x69a08` | init_cache_regs |
| `VM_L2_CNTL4` | `0x69a5c` | init_cache_regs |
| `VM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO32` | `0x69a44` | disable_identity_aperture |
| `VM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_HI32` | `0x69a48` | disable_identity_aperture |
| `VM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO32` | `0x69a4c` | disable_identity_aperture |
| `VM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_HI32` | `0x69a50` | disable_identity_aperture |
| `VM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO32` | `0x69a54` | disable_identity_aperture |
| `VM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI32` | `0x69a58` | disable_identity_aperture |
| `VM_INVALIDATE_ENG17_ACK` | `0x69c18` | gmc_v9_0_flush_gpu_tlb (engine 17 via vm_inv_eng0_ack + eng_distance) |
| `VM_INVALIDATE_ENG17_SEM` | `0x69b88` | gmc_v9_0_flush_gpu_tlb (engine 17 semaphore) |
| `VM_INVALIDATE_ENG0_ADDR_RANGE_LO32` | `0x69c1c` | program_invalidation |
| `VM_INVALIDATE_ENG0_ADDR_RANGE_HI32` | `0x69c20` | program_invalidation |

**C. Interrupts (IH ring and NBIO), 13 registers**, from
`osssys_4_0_offset.h` (OSSSYS base `0x10A0`) and `nbio_7_0_offset.h`. This
host uses NBIO 2.5.0, which is `nbio_v7_0_funcs` in `amdgpu_discovery.c`.

| Register | BAR5 byte offset | Linux v6.12 use |
| --- | --- | --- |
| `IH_RB_BASE` | `0x04484` | vega10_ih_enable_ring |
| `IH_RB_BASE_HI` | `0x04488` | vega10_ih_enable_ring |
| `IH_RB_WPTR` | `0x04490` | enable_ring |
| `IH_RB_RPTR` | `0x0448c` | enable_ring |
| `IH_RB_WPTR_ADDR_LO` | `0x04498` | enable_ring |
| `IH_RB_WPTR_ADDR_HI` | `0x04494` | enable_ring |
| `IH_DOORBELL_RPTR` | `0x0449c` | enable_ring |
| `IH_CHICKEN` | `0x048b0` | vega10_ih_irq_init (Renoir) |
| `IH_RB_CNTL_RING1` | `0x044a0` | toggle_interrupts |
| `IH_RB_CNTL_RING2` | `0x044c0` | toggle_interrupts |
| `INTERRUPT_CNTL` | `0x03844` | nbio_v7_0_ih_control |
| `INTERRUPT_CNTL2` | `0x03848` | nbio_v7_0_ih_control |
| `BIF_IH_DOORBELL_RANGE` | `0x03bc8` | nbio_v7_0_ih_doorbell_range |

Left out on purpose:
- **Write-triggered or indexed registers:** `VM_INVALIDATE_ENG*_REQ`,
  `SYSHUB_INDEX`/`_DATA`.
- **Registers Linux does not use for this IP:** `IH_CNTL`,
  `IH_STORM_CLIENT_LIST_CNTL`, `IH_INT_FLOOD_CNTL`,
  `VM_L2_PROTECTION_FAULT_ADDR_*`.

**How.** On request only, `sudo cezanne-diag --inventory16`. It reads A,
then B, then C, then A again. The second display pass shows whether
anything changed while the machine was running. The tool prints each
register's name before reading it.

**Changes:**
- **Core:** a stage 16 register list extending stage 15's by these 92
  registers. Display, MMHUB and IH registers are not GFX-gated. No write
  allowlist change.
- **Adapter:** none beyond the stage limit (the plain diagnostic read).
- **Tool:** `--inventory16`:
  - prints the three groups;
  - decodes the active pipe (OTG enable, H/V totals and blanking,
    surface address, pitch);
  - shows whether the surface address equals the GOP framebuffer
    (`0xF400000000` + 0);
  - marks registers that differ between the two display passes.
- **Tests:**
  - Every offset is recomputed from the headers in the test, unique and
    inside BAR5.
  - Refused at stage 15, and not writable.
  - The tool names every register in list order.

**Expected.**
- One OTG enabled, with 1920×1080 timing.
- One HUBP scanning out at the GOP framebuffer address with a 1920-pixel
  pitch (boot 12: 1920×1080×4).
- `DCN_VM_FB_*` matching the MMHUB FB location (`0xf400`–`0xf47f`, offset
  `0x5c0`).
- `VM_CONTEXT0_*` page-table registers 0 or stale.
- IH ring 0 is off (`IH_RB_CNTL` `0x40610000`, boot 7).

**What stage 17 will then do** (its own proposal, with pinned values):
- **GART:** program MMHUB context 0 with a one-level page table in a
  checked carveout page. Point the protection-fault default page at a
  scratch page, which also fixes the boot 14 zero default page. Enable the
  context, then flush the TLB through invalidation engine 17. Prove it by
  having SDMA copy through a GART address that maps a carveout page.
- **Interrupts:** program IH ring 0 in the carveout with write-pointer
  write-back, keep `ENABLE_INTR` (CPU delivery) off, set `TRAP_ENABLE` in
  `SDMA0_CNTL`, add the `TRAP` after the fence, and confirm an IH entry
  from SDMA0 (client 8, `src_id` 224) appears in the ring.
- **Undo:** both are turned off again before the existing stop and
  teardown.

**Risks.** As in stages 5 and 10. Every register is a status or
configuration register that Linux reads, and none is clear-on-read. DCN is
active and driving the screen; reading its registers does not change the
display. If a read hangs, the printed name identifies it: power off and
boot the known-good EFI.

### Revision: stage 16 semaphore read

**Status: fixed 2026-10-06 after boot 22 (fix-within-a-stage rule).**

- **Change:** `VM_INVALIDATE_ENG17_SEM` is removed from the inventory (now
  91 registers, 23 for MMHUB). The header and test record why: a read
  acquires the semaphore.
- **Tests:**
  - The count changed.
  - `test_inventory16.py` asserts the register is absent.
  - The core tests check the new group size.
- **Rebuilt:** the first build is kept as `superseded-*-stage16-sem`.
- **Lesson for reviews:** "status or configuration register that Linux
  reads" is not enough. A register Linux reads only as part of a protocol
  (a semaphore, an acknowledge, a data port) can act on a read. Check how
  Linux reads it, not only that it does.

### Stage 17: GART and the interrupt ring (proposal)

**Status: proposed 2026-10-06 and approved by the user the same day;
implemented and built (`out/test-efi/usb-stage17`) the same day. Not yet
booted.**

**Purpose.** Turn on the two remaining memory and interrupt foundations,
with every value pinned from boot 22:
- **GART:** MMHUB VM context 0 with a one-level page table. It also
  replaces the zero default page found in boot 14.
- **The IH ring:** where engines post interrupts.

SDMA0 proves both in one frame: a copy whose source is a GART address,
followed by a fence and a `TRAP` that must appear in the IH ring. CPU
interrupt delivery stays off, since macOS has no handler for this device.
Everything written is restored to its boot 22 value before the stage 15
stop and teardown.

**It builds on stage 15.** `sudo cezanne-diag --gart-ih` first runs the
stage 15 flow unchanged up to its verify: load, check, start, ring test,
copy and fence. Then it runs the steps below, and ends with the stage 15
stop.

**A new work area** at carveout `0x40800000` (GPU `0xF440800000`, physical
`0x600800000`), just above the TMR, 64 KiB checked and snapshotted:

| Page | Offset | Contents |
| --- | --- | --- |
| GART page table | `+0x0000` | 512 PTEs (2 MiB of GART); PTE 0 maps the stage 15 source page, the rest are 0 (invalid) |
| Dummy/default page | `+0x1000` | zero; the new system-aperture default page and VM fault default page |
| IH ring 0 | `+0x2000` | 4 KiB, zeroed |
| IH write-pointer write-back | `+0x3000` | zeroed |

The stage 15 work area grows by one page: a second destination at `+0x4000`
(zeroed), plus SDMA frame 2.

**GART** (Linux v6.12 `mmhub_v1_0_gart_enable`; GC 9.3 sets
`translate_further`, `gmc_v9_0_sw_init`):

| Register | Boot 22 | Write | Linux |
| --- | --- | --- | --- |
| `VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32`/`HI32` | 0/0 | `0x00800001`/`0x6` | `amdgpu_gmc_pd_addr`: table physical address (`vram_mc2pa`) \| VALID |
| `VM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32`/`HI32` | 0/0 | `0`/`0` | GART at MC 0, as `amdgpu_gmc_gart_location` best fit places it on this host |
| `VM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32`/`HI32` | 0/0 | `0x1ff`/`0` | 2 MiB here; Linux uses 1 GiB for GC 9.3 |
| `MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB`/`MSB` | 0/0 | `0x600801`/`0` | `init_system_aperture_regs`: scratch page physical `>> 12` |
| `VM_L2_PROTECTION_FAULT_DEFAULT_ADDR_LO32`/`HI32` | 0/0 | `0x600801`/`0` | dummy page `>> 12` |
| `VM_L2_PROTECTION_FAULT_CNTL2` | `0x000a0000` | `0x000e0000` | + `ACTIVE_PAGE_MIGRATION_PTE_READ_RETRY` |
| `MC_VM_MX_L1_TLB_CNTL` | `0x00002501` | `0x00003d59` | `init_tlb_regs`: `SYSTEM_ACCESS_MODE` 3, advanced driver model, `MTYPE` UC |
| `VM_L2_CNTL` | `0x00080602` | `0x00080603` | `init_cache_regs`: + `ENABLE_L2_CACHE` |
| `VM_L2_CNTL2` | `0` | `0x00000003` | invalidate all L1 TLBs and the L2 cache |
| `VM_L2_CNTL3` | `0x80100007` | `0x8014800c` | `BANK_SELECT` 12, `BIGK` 9 (`translate_further`) |
| `VM_L2_CNTL4` | `0x000000c1` | `0x00000001` | PDE and PTE requests not physical |
| `VM_CONTEXT0_CNTL` | `0x007ffe80` | `0x007ffe01` | `enable_system_domain`: enable, depth 0, no retry |
| `VM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO32`/`HI32` | 0/0 | `0xffffffff`/`0xf` | `disable_identity_aperture` |
| `VM_INVALIDATE_ENG17_ADDR_RANGE_LO32`/`HI32` | 0/0 | `0xffffffff`/`0x1f` | `program_invalidation`, engine 17 only |

Then the TLB flush (`gmc_v9_0_flush_gpu_tlb`, MMHUB, engine 17, with the
semaphore):
1. Read `VM_INVALIDATE_ENG17_SEM` until it reads 1 (acquired).
2. Write `VM_INVALIDATE_ENG17_REQ` ← `0x007c0001`: VMID 0, flush type 0,
   L2 PTEs, PDE0–2 and L1 PTEs.
3. Poll `VM_INVALIDATE_ENG17_ACK` bit 0.
4. Release the semaphore: `SEM` ← 0.

**PTE 0** is `0x0600000600302073`: the source page's physical address
`0x600302000`, plus `VALID`, `SYSTEM`, `EXECUTABLE`, `READABLE` and
`WRITEABLE`, and `MTYPE_UC` (3, `vega10_enum.h`) at bit 57. There is no
`SNOOPED`, because the CPU maps the page uncached
(`amdgpu_ttm_tt_pte_flags`).
- **Left out:** contexts 1–15 (`setup_vmid_config`), the other 17
  invalidation engines, GFXHUB (SDMA uses MMHUB), AGP and the system
  aperture bounds. The firmware's system aperture is unchanged, so FB
  addresses, including the display's, still bypass the page table.

**IH ring 0** (`vega10_ih_enable_ring`, `vega10_ih_rb_cntl`):

| Register | Boot 22 | Write | Linux |
| --- | --- | --- | --- |
| `IH_RB_BASE`/`_BASE_HI` | 0/0 | `0xF4408020`/`0x00` | ring GPU address `>> 8`, `>> 40` |
| `IH_RB_CNTL` | `0x40610000` | `0xc0110114`, then `0xc0110115` | see below; then `RB_ENABLE` |
| `IH_RB_WPTR_ADDR_LO`/`_HI` | 0/0 | `0x40803000`/`0xf4` | write-pointer write-back |
| `IH_RB_WPTR`, `IH_RB_RPTR` | `0x00080000`, 0 | 0, 0 | ring reset |
| `IH_DOORBELL_RPTR` | 0 | 0 | no doorbell |

`IH_RB_CNTL` has the following fields:
- `MC_SPACE` 4: the MC address space. Linux uses 4 for rings it places in
  VRAM; ring 0 on Renoir uses a bus address (`use_bus_addr`, `MC_SPACE` 1
  with `IH_CHICKEN.MC_SPACE_GPA_ENABLE`). We use a carveout MC address, so
  `IH_CHICKEN` stays 0.
- `RB_SIZE` 10 (4 KiB), write-back on, `WPTR_OVERFLOW_ENABLE` and
  `_CLEAR`, `MC_SNOOP` 1, `MC_RO` 0, VMID 0.
- `RPTR_REARM` 0: there is no MSI.
- **`ENABLE_INTR` stays 0:** no CPU interrupt. `vega10_ih_toggle_interrupts`
  would set it.
- **Left out:** the NBIO `ih_control` and doorbell range, which only
  matter for CPU delivery and doorbells, and rings 1 and 2.

**SDMA interrupt.** `SDMA0_CNTL` ← `0x00000003` (`TRAP_ENABLE`, as
`sdma_v4_0_set_trap_irq_state` sets). Frame 2 of the SDMA ring (dwords
512–767), then `GFX_RB_WPTR` ← 3072 and `_HI` ← 0:
- `COPY_LINEAR` 4 KiB from **GART address `0x0`** (PTE 0, the source
  page) to the second destination, `0xF440304000`;
- `FENCE` 2 at write-back `+0x204`;
- `TRAP` (`0x00000006`, context 0).

**Verify** (no writes):
- the second destination equals the source;
- fence 2 arrived;
- `GFX_RB_RPTR` is 3072;
- `VM_L2_PROTECTION_FAULT_STATUS` is 0 (no VM fault);
- the IH write-back pointer advanced, and the ring holds an entry from
  client 8 (`SOC15_IH_CLIENTID_SDMA0`) with source 224
  (`SDMA0_4_0__SRCID__SDMA_TRAP`); other clients' entries are recorded;
- the display inventory (stage 16) is unchanged, apart from the live
  `DCHUBP_CNTL` status bits (`HUBP_NO_OUTSTANDING_REQ`, `HUBP_IN_BLANK`,
  `HUBP_XRQ_NO_OUTSTANDING_REQ`; widened after boot 24);
- the two 64 KiB regions hold only expected words.

**Restore** (before the stage 15 stop):
- `IH_RB_CNTL` ← `0xc0110114` (ring off), then every IH register back to
  its boot 22 value.
- `SDMA0_CNTL` ← `0x00000002`.
- Every GART register back to its boot 22 value, `VM_CONTEXT0_CNTL` first,
  then a second engine 17 flush.
- If the tool exits early, the driver does the same.

**Preconditions.** Every register written must read its boot 22 value, and
display pipe 0 must match boot 22. The semaphore is acquired only inside
the flush.

**New writes:**
- **Registers:** the GART and IH registers above, with exactly those values
  and their boot 22 restore values. Also `VM_INVALIDATE_ENG17_REQ` ←
  `0x007c0001`, `VM_INVALIDATE_ENG17_SEM` ← 0, `SDMA0_CNTL` ← `0x3`, and
  `GFX_RB_WPTR` ← 3072.
- **Memory:** the new work area's four pages (one non-zero word in the page
  table, everything else zero), the second destination page (zero), and
  SDMA frame 2's words.

**Risks:**
- **These are the first writes that change settings shared by every MMHUB
  client, including the display's scanout.** They are `MC_VM_MX_L1_TLB_CNTL`
  (`SYSTEM_ACCESS_MODE` 0 → 3) and `VM_L2_CNTL` (L2 cache on). Linux makes
  exactly these writes while the GOP framebuffer is on screen. The display
  uses FB addresses inside the unchanged system aperture, which bypass the
  page table.
- **If the screen glitches or goes black:** the restore runs at the end
  anyway. If the machine hangs, power off; a cold boot resets everything.
- **A VM fault now goes to the dummy page,** not physical 0.
- **No CPU interrupt can fire** (`ENABLE_INTR` 0, and MSI is not set up).
- Make a Time Machine backup before this boot.

**Expected:**
- Every stage 15 result again.
- Fence 2, destination 2 equal to the source, no VM fault.
- One or more IH entries, including SDMA0's trap.
- The display unchanged, and every register back at its boot 22 value
  after the restore.

**Implementation** (2026-10-06). Diagnostic interface version 13, selectors
26–29, and selector 23 with frame 2:

| Step | Selector | What it does |
| --- | --- | --- |
| check | 26 `GartCheck` | Only after a passing stage 15 verify on the same connection. Reads the 41-entry precondition list in order: the 21 GART registers, the 8 IH registers, `SDMA0_CNTL` (2, as the stage 15 start leaves it), then display pipe 0's 11 registers. Places the new area (stage 9 checks) and snapshots its 64 KiB after the 1 s stability check, then snapshots the 55 display registers. No write. |
| enable | 27 `GartEnable` | Writes the GART area (one non-zero PTE), frame 2 and the zeroed second destination, all read back. Then the 21 GART writes, the flush, the 8 IH writes, the ring on, and `SDMA0_CNTL` ← 3. Reports progress 0–3 (none, GART, IH, `SDMA0_CNTL`), which drives the restore. |
| frame 2 | 23 `SdmaSubmit`, frame 2 | `GFX_RB_WPTR` 2048 → 3072, then `_HI` ← 0; polls fence 2 for 100 ms. |
| verify | 28 `GartVerify` | Reads only. Polls the IH write-back for SDMA0's trap for up to 100 ms, then returns a `GartReport`: pointers, fence, fault status, IH counts, the first 32 entries, and unexpected-word and display-change counts. |
| restore | 29 `GartRestore` | Restores by progress, then reads every written register back (see below). The stage 15 stop (selector 25) runs it first whenever the enable was sent and not yet restored. So does a closed or abandoned connection, which goes through the stop. |

- **Register writes** go through a third BAR5 page set, `kGartPages`:
  - `0x4000`: IH and SDMA0;
  - `0x69000`: VM L2, context 0 and engine 17;
  - `0x6a000`: L1 TLB and default address.
- **Semaphore:** `VM_INVALIDATE_ENG17_SEM` is in no read allowlist. The
  adapter lets only that page set's operations read it, and in the core only
  `flushGart` reads it.
- **Flush:**
  - If the semaphore never reads 1 within 100 polls, the flush writes
    nothing (`gart-semaphore-timeout`).
  - After an acquire it always writes the release, even when `ACK` never
    sets (`gart-ack-timeout`).
- **Memory:** the GART area is the fourth writable memory mapping (16 KiB).
  From stage 17 the copy work area maps 20 KiB (the second destination);
  stage 15 and 16 boots keep 16 KiB.

**Choices not fixed by the proposal:**
- **`VM_INVALIDATE_ENG17_ADDR_RANGE_LO32`/`HI32` were never read** (stage 16
  read engine 0's). The table's "0/0" for them is an expectation, and the
  check reads them first (stage 17 adds them to the read list). If either is
  not 0, the check stops with nothing written: a finding.
- **The restore check does not require `IH_RB_WPTR`;** it reports it. Bit 19
  of its boot 22 value `0x80000` is `RB_MAY_OVERFLOW`, a status the IH sets
  itself (`osssys_4_0_sh_mask.h`), so a written value need not read back.
  Every other written register, and `SDMA0_CNTL`, must read its boot 22
  value (`gart-not-restored` with the register otherwise).
- **IH entries** are taken from the write-back pointer (`OFFSET` bits 17:2, in
  bytes, 32 bytes per entry):
  - The ring holds anything below that pointer and must be zero above it.
  - Overflow, or an offset past 4 KiB, marks the ring wrapped, and the whole
    page is then accepted.
  - An entry from client 8, source 224 is SDMA0's trap. Any other client is
    counted and printed, not treated as a failure.

### Stage 18: interrupt delivery (proposal)

**Status: proposed 2026-10-06 and approved by the user the same day;
implemented and built (`out/test-efi/usb-stage18`) the same day.
Succeeded in boot 27, after the boot 26 fix.** The display test pattern
follows as stage 19.

**Purpose.** Stage 17 put SDMA0's trap into IH ring 0 but kept CPU delivery
off. Stage 18 turns it on: the IH raises an MSI, and the kext counts it.
This is the kext's first interrupt handler. Everything is undone before the
stage 17 restore and the stage 15 stop.

**It builds on stage 17.** `sudo cezanne-diag --ih-intr` runs the stage 17
flow unchanged through its verify (stage 15 copy, GART and IH enable,
frame 2, verify). Then it runs the steps below, then the stage 17 restore
and the stage 15 stop.

**The vector.** The host baseline's registry ([hardware-baseline.md](hardware-baseline.md), 2026-10-02, NootedRed loaded)
lists two interrupt specifiers on the GPU's `IOPCIDevice`:
- index 0: `io-apic-1` (the legacy line);
- index 1: `IOPCIMessagedInterruptController`, one MSI vector
  (`IOPCIMSIMode` true).

On the test EFI no driver has registered either. The check finds the index
whose `getInterruptType` reports `kIOInterruptTypePCIMessaged`. With none,
it stops before any write (`intr-no-msi`). The legacy line is never used.
Linux also asks for one MSI vector (`amdgpu_irq_init`,
`pci_alloc_irq_vectors(…, 1, 1, PCI_IRQ_MSI | PCI_IRQ_MSIX)`).

**The handler.** It is an `IOFilterInterruptEventSource` on the kext's work
loop, for that index:
- **The filter** runs in primary interrupt context. It only increments a
  counter and records `mach_absolute_time()` of the first and last
  interrupt. It takes no lock, logs nothing and touches no register. It
  returns `false`, so no work-loop action is scheduled.
- **Acknowledging** (consuming entries and writing `IH_RB_RPTR`) is done
  by a diagnostic selector under `lock_`, not by the handler.
- **Storm bound:** with `RPTR_REARM` set, as Linux sets it with MSI
  (`vega10_ih_enable_ring`), the IH sends no further MSI until
  `IH_RB_RPTR` is written. MSI is edge-triggered, so nothing re-fires on
  its own. The verify measures this rather than assuming it.

**Sequence** (Linux `vega10_ih_irq_init` order, from the stage 17 state:
ring 0 on with 1 entry, `ENABLE_INTR` 0):

| Step | Register or action | Value | Linux |
| --- | --- | --- | --- |
| 1 | `IH_RB_CNTL` | `0xc0110114` | ring off (`toggle_interrupts(false)`); the stage 17 off value |
| 2 | `IH_RB_RPTR`, `IH_RB_WPTR` | 0, 0 | reset by the toggle |
| 3 | CPU | zero the ring page and the write-back page | |
| 4 | `INTERRUPT_CNTL2` | `0x06008010` | `nbio_v7_0_ih_control`: dummy page `>> 8`, the stage 17 dummy page `0x600801000` |
| — | `INTERRUPT_CNTL` | stays 0 | Linux sets `IH_DUMMY_RD_OVERRIDE` 0 and `IH_REQ_NONSNOOP_EN` 0; boot 22 already reads 0 |
| 5 | `IH_RB_CNTL` | `0xc0310114` | `vega10_ih_enable_ring`: + `RPTR_REARM` (`msi_enabled`) |
| 6 | `IH_RB_WPTR`, `IH_RB_RPTR` | 0, 0 | ring reset |
| 7 | kext | create, add and enable the event source (macOS programs and enables the MSI capability) | `pci_alloc_irq_vectors`, `request_irq` |
| 8 | `IH_RB_CNTL` | `0xc0330195` | `toggle_interrupts(true)`: + `RB_ENABLE`, `RB_GPU_TS_ENABLE`, `ENABLE_INTR` |

The handler is enabled (step 7) before the IH may interrupt (step 8).
- **Left out:** `IH_CHICKEN` (`MC_SPACE_GPA_ENABLE` only matters for a
  bus-address ring), the doorbell (`BIF_IH_DOORBELL_RANGE` stays 0, as
  Linux writes it with no doorbell), rings 1 and 2, and `pci_set_master`:
  the command register already reads `0x0006`, bus master on.
- **`IH_RB_CNTL` fields** (`osssys_4_0_sh_mask.h`): `RB_GPU_TS_ENABLE`
  bit 7, `ENABLE_INTR` bit 17, `RPTR_REARM` bit 21.

**Frame 3.** The last quarter of the SDMA ring (dwords 768–1023):
- `FENCE` 3 at write-back `+0x208`;
- `TRAP` (context 0);
- NOP padding.

Then `GFX_RB_WPTR` ← 4096, `_HI` ← 0. Like Linux (`sdma_v4_0_ring_set_wptr`),
the write pointer is not masked to the 4 KiB ring, so 4096 is the end of
the ring. `GFX_RB_RPTR` afterwards (4096 or 0) is recorded, not required.

**Verify** (reads, then one acknowledgement):
1. Poll for up to 100 ms: fence 3, and the MSI count reaching 1.
2. Read the IH write-back: `0x20`, one entry from client 8, source 224.
   Any other entry is recorded.
3. Record the MSI count, and the time from the `WPTR` write to the first
   interrupt.
4. **Acknowledge:** `IH_RB_RPTR` ← the write pointer (`0x20`), as
   `amdgpu_ih_process` does.
5. Wait 100 ms. The MSI count must still be 1 and the IH write pointer
   unchanged: no re-fire and no flood.
6. `VM_L2_PROTECTION_FAULT_STATUS` 0, both 64 KiB regions clean, and
   display pipe 0 unchanged (as in stage 17).

**PCI MSI capability**, read only, from configuration space: message control,
address and data. They are read at the check, after step 7 and after the
restore, and printed. macOS writes them in step 7; this stage reads them to
record what it wrote.

**Restore** (before the stage 17 restore):
1. `IH_RB_CNTL` ← `0xc0310114` (`ENABLE_INTR` and `RB_ENABLE` off), then
   `IH_RB_RPTR`, `IH_RB_WPTR` ← 0. Wait 1 ms (`vega10_ih_irq_disable`).
2. Disable the event source, remove it from the work loop and release it.
3. `INTERRUPT_CNTL2` ← 0 (boot 22).
4. Then the stage 17 restore, which starts with `IH_RB_CNTL` ← `0xc0110114`
   and returns every IH and GART register to boot 22.

The stage 15 stop runs this first if the tool exits early. The kext's `stop`
also removes the event source if it is still registered.

**Preconditions:**
- the stage 17 verify passed on this connection;
- `INTERRUPT_CNTL`, `INTERRUPT_CNTL2` and `BIF_IH_DOORBELL_RANGE` read
  their boot 22 values (0);
- an MSI index exists, and no event source is registered.

**New writes:**
- **Registers:**
  - `IH_RB_CNTL` ← `0xc0310114` and `0xc0330195`;
  - `IH_RB_RPTR` ← `0x20` (the acknowledgement);
  - `INTERRUPT_CNTL2` ← `0x06008010` and 0, on a new BAR5 page, `0x3000`;
  - `GFX_RB_WPTR` ← 4096.

  The other values (`0xc0110114`, `IH_RB_RPTR`/`WPTR` ← 0) are stage 17's.
- **Memory:** frame 3's words in the SDMA ring, and zeroing the IH ring and
  write-back pages (already zero at stage 17's enable).
- **Through macOS:** the device's MSI capability (address, data, enable),
  written by `IOPCIFamily` when the source is enabled.

**Risks:**
- **This is the first code that runs in interrupt context.** A fault there
  panics the kernel. The filter is a few lines with no locks, and the core
  tests cover the counting. If the machine panics, power off and record the
  panic log from the next normal boot.
- **Other IH clients may now interrupt too.** Stage 17 saw none during its
  window, and with `RPTR_REARM` each one can raise at most one MSI before
  the acknowledgement.
- **MSI not delivered** (count 0 with the entry in the ring) is a finding.
  The restore still runs.
- Make a Time Machine backup before this boot.

**Expected:**
- Every stage 17 result again.
- Fence 3, one SDMA0 trap entry, exactly one MSI, and no further MSI after
  the acknowledgement.
- Every register back at its boot 22 value and the event source removed.

**Stage 18 succeeds when:**
- `sudo cezanne-diag --gfxoff-disallow --ih-intr --psp-state` reports `ok`
  for every stage 17 step up to the verify, then for:
  - the check, naming the MSI index;
  - the enable;
  - frame 3: fence 3;
  - the verify: MSI count 1 after the trap, still 1 after the
    acknowledgement and 100 ms, one SDMA0 trap entry, no VM fault, regions
    and display unchanged;
  - the restore, then the stage 17 restore and the stage 15 stop;
- the final register dump shows the boot 22 values;
- the machine stays up and the display is unchanged.

**Implementation** (2026-10-06). Diagnostic interface version 14, selectors
30–34, and selector 23 with frame 3:

| Step | Selector | What it does |
| --- | --- | --- |
| check | 30 `IntrCheck` | Only after a passing stage 17 verify on the same connection. Finds the first interrupt index whose type has `kIOInterruptTypePCIMessaged` (indexes 0–7), reads the MSI capability, then reads `INTERRUPT_CNTL`, `INTERRUPT_CNTL2` and `BIF_IH_DOORBELL_RANGE`. No write. |
| enable | 31 `IntrEnable` | Writes frame 3 into the SDMA ring (read back). Steps 1–6 of the sequence, with the ring and write-back pages zeroed and read back (progress 1). Then the handler: a new `IOWorkLoop`, an `IOFilterInterruptEventSource` on the MSI index, added and enabled. Then step 8 (progress 2). Reports the MSI capability afterwards. |
| frame 3 | 23 `SdmaSubmit`, frame 3 | `GFX_RB_WPTR` 3072 → 4096, then `_HI` ← 0; polls fence 3 for 100 ms. The kext notes the time just before. |
| verify | 32 `IntrVerify` | Reads only. Polls for up to 100 ms until fence 3, the trap entry and an MSI have all arrived, then returns an `IntrReport`: MSI count, microseconds from the submit to the first MSI, fence, pointers, `IH_RB_CNTL`, fault status, IH counts and the first 32 entries, unexpected words, display changes. |
| acknowledge | 33 `IntrAck` | Once, after a passing verify. `IH_RB_RPTR` ← the write-back offset, 100 pauses, then the MSI count and the write-back again. |
| restore | 34 `IntrRestore` | Steps 1–3 of the restore with a check of the three registers. The stage 17 restore (selector 29) runs it first if needed, and so does the stage 15 stop, so a closed or abandoned connection is covered. `stop()` also removes a handler that is still registered. |

- **Register writes** go through a fourth BAR5 page set, `kIntrPages`:
  `0x3000` (NBIO, `INTERRUPT_CNTL2`) and `0x4000` (IH).
- **The handler:** the filter increments an atomic counter and records
  `mach_absolute_time()` for the first interrupt, then returns `false`. The
  work-loop action is empty and never scheduled. A structural test checks
  that the filter has no lock, log, register or memory access. It is the
  only interrupt registration, and the enable is the only caller, between
  the arm and `ENABLE_INTR`.
- **The kext's libraries** gain `com.apple.kpi.mach`
  (`mach_absolute_time`, `absolutetime_to_nanoseconds`).

**Choices not fixed by the proposal:**
- **The acknowledgement's value** is the write-back's offset, not a fixed
  `0x20`. The allowlist accepts any 32-byte entry boundary inside the 4 KiB
  ring, so that another client's entry before the trap does not block the
  acknowledgement. With only the trap, the value is `0x20`.
- **The verify requires exactly one MSI after the frame 3 submit** (revised
  after boot 26; it first required exactly one in all). MSIs before the
  submit are counted and timed, not required. `kIntrNotDelivered` is kept
  apart from other failures. It does not require exactly one IH entry:
  another client's entry is recorded, and can take the one MSI instead.
- **`GFX_RB_RPTR` after frame 3** is reported, not required (4096 or 0).
- **The restore's ring-off value** is `0xc0310114` (`RPTR_REARM` kept,
  `RB_GPU_TS_ENABLE` cleared). Linux's toggle keeps `RB_GPU_TS_ENABLE` set;
  the stage 17 restore that follows writes `0xc0110114` and then boot 22
  anyway.

### Stage 19: display test pattern (proposal)

**Status: proposed 2026-10-06 and approved by the user the same day;
implemented and built (`out/test-efi/usb-stage19`) on 2026-10-07.
Succeeded in boot 28 the same day; the grey lines were straight, so the
hardware reads 1920 pixels per line.**

**Purpose.** The first write that changes what is on screen. Pipe 0 keeps
the firmware's mode and everything else it set up. Only its surface address
moves, from the GOP framebuffer to a test pattern the driver draws in the
carveout. The pattern stays up for 5 seconds, then the address goes back.
Two things are proved:
- the driver can flip pipe 0's surface, as Linux does
  (`hubp21_program_surface_flip_and_addr`);
- the GOP framebuffer's pitch, an open observation since boot 22 (see
  below).

**It stands alone.** `sudo cezanne-diag --display-pattern` needs no SDMA,
PSP, GART or interrupt step, and no GFX register. The macOS boot framebuffer
at `0xF400000000` is never written: whatever macOS draws there reappears at
the restore.

**The pattern surface** is at carveout `0x41000000` (GPU `0xF441000000`,
physical `0x601000000`), 8 MiB, inside DCN's FB aperture
(`0xf400`–`0xf47f`, so no page table is involved). It uses the stage 9
placement checks. A checksum is read twice, 1 s apart, and must match (the
TMR's method, as 8 MiB is too large to snapshot). The pattern is 1920 × 1080
ARGB8888, linear, 1920 pixels (7680 bytes) per line, the same format as the
GOP surface:
- **8 horizontal bands** of 135 lines: white `0xFFFFFFFF`, yellow
  `0xFFFFFF00`, cyan `0xFF00FFFF`, green `0xFF00FF00`, magenta
  `0xFFFF00FF`, red `0xFFFF0000`, blue `0xFF0000FF`, black `0xFF000000`;
- **9 vertical grey lines** (`0xFF808080`), 2 pixels wide, at x = 0, 240,
  …, 1680 and at x = 1918;
- the rest of the 8 MiB is zero.

**What the pitch decides.** `DCSURF_SURFACE_PITCH` reads `0x780`. Linux
programs pitch − 1, which would make the GOP surface 1921 pixels wide; the
GOP's framebuffer is exactly 1920 × 1080 × 4 bytes. The pattern is written
at 1920 per line. If the hardware uses 1920, the grey lines are straight
and vertical. If it uses 1921, every line starts one pixel later, and the
lines lean visibly (1080 pixels over the height of the screen). Either way
the bands are horizontal. **You report which you see.**

**New reads**, HUBPREQ0/HUBP0 and OTG0, from `dcn_2_1_0_offset.h`, none
measured yet. The check requires the values marked; the others are
recorded:

| Register | Offset | Expected | Linux |
| --- | --- | --- | --- |
| `DCSURF_FLIP_CONTROL` | `0x0eb6c` | `SURFACE_UPDATE_LOCK` 0, `SURFACE_FLIP_TYPE` 0 (at vsync), `SURFACE_FLIP_PENDING` 0 | `hubp2_is_flip_pending` |
| `DCSURF_SURFACE_EARLIEST_INUSE`/`_HIGH` | `0x0eb94`/`0x0eb98` | `0x00000000`/`0xf4` (the GOP surface) | `hubp2_is_flip_pending` |
| `DCSURF_TILING_CONFIG` | `0x0ea9c` | `SW_MODE` 0 (linear) | `hubp2_read_state` |
| `DCSURF_SURFACE_CONTROL` | `0x0eb68` | 0 (no DCC, no TMZ) | `program_surface_flip_and_addr` |
| `DCSURF_PRIMARY_META_SURFACE_ADDRESS`/`_HIGH` | `0x0eb48`/`0x0eb4c` | 0/0 (no metadata) | same |
| `VMID_SETTINGS_0` | `0x0eb24` | 0 | same |
| `OTG_STATUS_FRAME_COUNT` | `0x14030` | recorded; must advance | `optc1_get_vblank_counter` |

Display pipe 0's stage 16 registers must also match boot 22, with the live
`DCHUBP_CNTL` status bits ignored (stage 17's check).

**The flip** writes two registers, in `program_surface_flip_and_addr`'s
order. Every other register that function writes already holds the value
it would write, which the check confirms:

| Step | Register | Value |
| --- | --- | --- |
| 1 | `DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH` (`0x0eb2c`) | `0x000000f4` (unchanged) |
| 2 | `DCSURF_PRIMARY_SURFACE_ADDRESS` (`0x0eb28`) | `0x41000000` |

The low write latches the flip at the next vertical sync. The step then polls
for up to 100 ms (about 6 frames) for `SURFACE_FLIP_PENDING` 0 and
`EARLIEST_INUSE` = `0xF441000000`.

**Hold and verify** (reads only): the tool waits 5 s. Then:
- `EARLIEST_INUSE` is still the pattern;
- the frame count advanced (about 300 at 60 Hz), so scanout continued;
- pipe 0's registers are unchanged apart from the address (the underflow
  status included);
- the pattern region reads back as written.

**Restore:** the same two writes with `0xf4` and `0x00000000`, then the
same poll for `EARLIEST_INUSE` = `0xF400000000`. A closed or abandoned
connection runs it, and so does the kext's `stop()` if needed.

**New writes:**
- **Registers:** `DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH` ← `0xf4`, and
  `DCSURF_PRIMARY_SURFACE_ADDRESS` ← `0x41000000` or `0x00000000`, on BAR5
  page `0xe000`. Nothing else in DCN.
- **Memory:** the 8 MiB pattern region, written once and read back.

**Risks:**
- **The screen changes:** that is the point. If it shows anything other
  than the pattern, or goes black, let the tool finish; the restore runs
  after 5 s. If the screen stays wrong after the tool ends, shut down fully:
  a cold boot resets the display.
- **Underflow or corruption** if DCN reads the new surface differently from
  the old one. The check confirms both are linear ARGB8888 at the same
  pitch, without DCC.
- **The region's previous contents are overwritten.** It sits in the
  GPU-only carveout, which nothing else on this host uses: the stability
  check guards against an active user, and the old checksum is recorded.
- Make a Time Machine backup before this boot.

**Expected:**
- The pattern on screen for 5 seconds, then the macOS desktop back exactly
  as before.
- The flip landing within 100 ms each way, and the frame count advancing.
- The answer to the pitch question.

**Stage 19 succeeds when:**
- `sudo cezanne-diag --display-pattern --psp-state` reports `ok` for the
  check, the pattern write, the flip, the verify after 5 s, and the
  restore;
- you saw eight horizontal colour bands and grey lines (straight or
  leaning: either is an answer, not a failure);
- the desktop came back unchanged, and the final register dump shows pipe 0
  at boot 22 again.

**Implementation** (2026-10-07). Diagnostic interface version 15, selectors
35–38:

| Step | Selector | What it does |
| --- | --- | --- |
| check | 35 `DisplayCheck` | Reads pipe 0's 11 stage 16 registers against boot 22 (stage 17's masks), then the 8 `kDisplayExpect` entries (flip control, `EARLIEST_INUSE`, tiling, surface control, metadata, VMID). Places the pattern region with the stage 9 checks, sums it twice 1000 pauses (1 s) apart through a read-only uncached map, and snapshots pipe 0. Returns the first mismatching entry, the frame count and the checksum. No write. |
| flip | 36 `DisplayFlip` | Writes the 8 MiB pattern word by word, reads it all back, then the two address writes and the poll (`flipDisplay`). Returns `EARLIEST_INUSE`, the pauses taken and the frame count after the flip. |
| verify | 37 `DisplayVerify` | Reads only: `EARLIEST_INUSE`, flip control, frame count, pipe 0 against the check's snapshot (address expected at the pattern), and the whole pattern. Returns a `DisplayReport`. |
| restore | 38 `DisplayRestore` | The two writes back to `0xF400000000` and the poll, then pipe 0 against boot 22. Its result is also published as the property `CezanneGPU display restore`. |

- **Register writes** go through a fifth BAR5 page, `kDisplayPageOffset`
  (`0xe000`), and the allowlist accepts only the high dword `0xf4` and the
  low dwords `0x41000000` and `0x00000000`.
- **Pattern writes** go through a separate 8 MiB writable, uncached map of
  physical `0x601000000` (`withPattern`), opened only for the flip step.
  Every word must equal `patternWord(offset)`: the core checks it, and the
  kext's write adapter checks it again.
- **Order and ownership:** the steps run once per connection, in order.
  Another connection can start only after a restore. The kext treats the
  surface as possibly flipped as soon as the flip step starts, so a failed
  pattern write or flip is still followed by a restore. An abandoned
  connection runs the restore (property `CezanneGPU display abandoned
  restore`), and so does `stop()`.
- **The tool:** `--display-pattern` runs check, flip, `sleep(5)`, verify and
  restore, and always calls the restore once the flip was sent. The register
  dump includes the 9 new registers from stage 19.

**Choices not fixed by the proposal:**
- **The verify compares pipe 0 with the check's snapshot**, not with the
  boot 22 values, so it measures only what changed during the hold. The
  restore's check uses the boot 22 values.
- **The restore runs its register check even if the flip back times out**,
  and reports the poll's status first.
- **The kext holds its lock through the check's 1 s checksum.** No other
  diagnostic runs at the same time.

### Stage 20: SDMA draws the pattern, and the flip interrupt (proposal)

**Status: proposed 2026-10-07 and approved by the user the same day;
implemented and built (`out/test-efi/usb-stage20`) the same day. Boot 29
stopped at the check, before any stage 20 write: the flip-interrupt
register held the firmware's status latches. The check and the restore now
look at the enables only (fix within the stage). Boot 30 filled the region
but only the white band came out right: the fill packet filled bytes. It
now sets `FILLSIZE` 2 (dword fills), as Mesa's RADV does. Succeeded in
boot 31.**

The user chose to combine
two steps in this stage: the GPU drawing what is shown, and the display's
flip interrupt. Starting the main graphics engine is stage 21, proposed
after this stage boots, and probably split over several stages as SDMA was
(13–15).

**Purpose.** Two firsts, at the same event:
- **SDMA draws the surface the display shows.** Stage 19's pattern was
  written by the CPU. Here SDMA0 fills the same region with
  `CONST_FILL` packets.
- **The display tells the driver the flip happened.** HUBP0's flip
  interrupt goes through the IH ring to the stage 18 handler. Stage 19 only
  polled for the flip.

**It builds on stage 18.** `sudo cezanne-diag --gfxoff-disallow --sdma-flip
--psp-state` runs the stage 18 flow unchanged through its acknowledgement:
the stage 15 copy, the GART and IH enable, frame 2, the MSI handler,
frame 3, and the stage 18 verify and acknowledgement. Then it runs the steps
below, then the stage 18 restore, the stage 17 restore and the stage 15 stop.
Stage 19's `--display-pattern` stays as it is.

**Kept apart so that a failure points at one part.** SDMA's fill is
finished and read back by the CPU before any display register is written.
The flip is confirmed both by stage 19's poll and by the interrupt, each on
its own. If the interrupt does not arrive, the poll still shows whether
the flip happened, the pattern still shows, and the restore still runs.

**The pattern** is stage 19's bands in reverse order, so it can be told
apart from stage 19 on screen: black, blue, red, magenta, green, cyan,
yellow, white from top to bottom. It has no grey lines, which would take
1080 × 9 small fills. The region is the same: carveout `0x41000000` (GPU
`0xF441000000`), 8 MiB.

**Frame 4** (SDMA0, `sdma_v4_0_emit_fill_buffer`; `vega10_sdma_pkt_open.h`).
Frame 3 ended the ring at `GFX_RB_WPTR` 4096 and the engine read all of it.
Frame 4 is written at the start of the ring again (dwords 0–255). Like
Linux with 64-bit pointers, the write pointer continues from 4096 to 5120;
it is not wrapped to 0.

| Dwords | Packet | Values |
| --- | --- | --- |
| 9 × 5 | `CONST_FILL`: header `0x8000000b` (`FILLSIZE` 2; `0x0000000b` before boot 30), destination low, high, data, byte count − 1 | bands 0–7: destination `0xF441000000` + k × `0xfd200` (135 lines × 7680 bytes), count `0x000fd1ff`, data the band's colour; the 9th: the tail `0xF4417e9000`, count `0x00016fff`, data 0 |
| 4 | `FENCE` 4 | write-back `+0x20c` |
| rest | `NOP` | to dword 255 |

**Fill size (revised after boot 30).** Linux's header leaves `FILLSIZE`
(bits 31:30) at 0, which fills bytes with the data's low byte. Linux fills
only with 0, where that makes no difference. Mesa's RADV
(`radv_sdma_fill_memory`, Mesa 25.2) sets `FILLSIZE` 2, "the count is in
dwords", with the count still in bytes − 1. Frame 4 now does the same.
Each count is below
the 4 MiB limit (`fill_max_bytes` `0x400000`; the count field is 22 bits).
Frame 4 has no `TRAP`, so the only interrupt this stage expects is the
display's.

**HUBP0's flip interrupt** (Linux `irq_service_dcn21.c`, `pflip_int_entry`;
`irq_service.c`; `amdgpu_dm.c` `dcn10_register_irq_handlers`):
- IH client `SOC15_IH_CLIENTID_DCE` (4), source
  `DCN_1_0__SRCID__HUBP0_FLIP_INTERRUPT` (`0x4f`). The IH needs no change
  for a new client.
- **Enable:** `DCSURF_SURFACE_FLIP_INTERRUPT.SURFACE_FLIP_INT_MASK` (bit 0)
  set to 1. In DCN, `MASK` 1 means enabled.
- **Acknowledge:** `SURFACE_FLIP_CLEAR` (bit 8) set to 1.
  `dal_irq_service_set` acknowledges before it enables.
- **Status:** `SURFACE_FLIP_OCCURRED` (bit 16) and `SURFACE_FLIP_INT_STATUS`
  (bit 17), read only.
- **Routing:** `DCHUB_INTERRUPT_DEST2.HUBP0_IHC_FLIP_INTERRUPT_DEST`
  (bit 0) selects the destination. Linux never writes it and receives
  this interrupt, so its reset value routes to the host. The check requires
  0 and never writes it. If it reads 1, the stage stops before any write,
  and that is a finding.

**New reads**, from `dcn_2_1_0_offset.h` (DMU segment 2, base `0x34c0`):

| Register | Offset | Expected |
| --- | --- | --- |
| `HUBPREQ0_DCSURF_SURFACE_FLIP_INTERRUPT` | `0x0eb80` | both enables 0 (bits 0 and 2); the status latches recorded (revised after boot 29, which read `0x00050000`) |
| `DCHUB_INTERRUPT_DEST2` | `0x0d83c` | bit 0 = 0; the rest recorded |
| `DISP_INTERRUPT_STATUS_CONTINUE17` | `0x0d7ec` | recorded; bit 2 is HUBP0's flip interrupt |

**Steps** (after the stage 18 acknowledgement, with the IH ring on,
`RPTR_REARM` armed, and `IH_RB_RPTR` at `0x20`):

| Step | What | Writes |
| --- | --- | --- |
| 1 check | Stage 19's check: pipe 0 at boot 22, the region placed and stable. The three new reads. The stage 18 acknowledgement passed on this connection. | none |
| 2 clear | The CPU zeroes the 8 MiB region and reads it back. After frame 4, every non-zero word must therefore be SDMA's. | memory |
| 3 frame 4 | Write frame 4 into the ring (read back), then `GFX_RB_WPTR` 4096 → 5120 and `_HI` ← 0. Poll fence 4 for up to 100 ms. Then the CPU reads the whole region against the reversed bands. The MSI count must not change. | ring, `GFX_RB_WPTR` |
| 4 arm | `SURFACE_FLIP_INTERRUPT` ← `0x00000100` (clear), then ← `0x00000001` (enable). Read back. | 2 |
| 5 flip | Stage 19's two address writes, to `0xF441000000`. Then wait up to 100 ms for both of these: the poll (`FLIP_PENDING` 0 and `EARLIEST_INUSE` at the pattern), and the MSI count going up by one. Record the time from the low write to the MSI, the IH entries, and `SURFACE_FLIP_INTERRUPT`. | 2 |
| 6 acknowledge | `SURFACE_FLIP_INTERRUPT` ← `0x00000101` (clear, still enabled), then `IH_RB_RPTR` ← the IH write-back (`0x40`), as `amdgpu_ih_process` does. | 2 |
| 7 hold, verify | 5 s, then stage 19's verify against the reversed bands. No further MSI during the hold: the flip interrupt fires once per flip, not once per frame. | none |
| 8 restore | The two address writes back to `0xF400000000`, the poll, and the MSI for this flip (recorded, not required). Then `SURFACE_FLIP_INTERRUPT` ← `0x00000100`, then ← 0 (disabled, as `dal_irq_service_set(false)`). `IH_RB_RPTR` ← the write-back. Pipe 0 against boot 22, and `SURFACE_FLIP_INTERRUPT`'s enables 0. | 6 |

Then the stage 18 restore (interrupts off, handler removed), the stage 17
restore and the stage 15 stop, unchanged. A closed or abandoned connection,
and the kext's `stop()`, run step 8 before the stage 18 restore.

**Preconditions:**
- the stage 18 acknowledgement passed on this connection;
- stage 19's check passes;
- `SURFACE_FLIP_INTERRUPT`'s enables read 0, and `DCHUB_INTERRUPT_DEST2` bit 0 reads
  0.

**New writes:**
- **Registers:**
  - `HUBPREQ0_DCSURF_SURFACE_FLIP_INTERRUPT` ← `0x100`, `0x1`, `0x101` and
    0, on the stage 19 display page (`0xe000`);
  - `GFX_RB_WPTR` ← 5120.

  Everything else is an earlier stage's value: the surface address writes
  (stage 19), `IH_RB_RPTR` acknowledgements (stage 18), and `_HI` ← 0.
- **Memory:**
  - the 8 MiB region, zeroed by the CPU;
  - frame 4's 256 words in the SDMA ring;
  - through SDMA, the 9 fills inside the region.

**Risks:**
- **SDMA writes 8 MiB in one frame.** Every earlier frame wrote at most
  4 KiB. The destinations are fixed in the frame's words and lie inside the
  checked region, which is not on screen while SDMA writes. A wrong
  destination would corrupt carveout memory. The core tests check every
  fill's address range against the region.
- **The flip interrupt may go elsewhere.** The routing check covers the
  known register. If no MSI arrives, the poll still completes the flip,
  and the restore runs. That is a finding, not a hang.
- **The display interrupt is new to the handler.** With `RPTR_REARM`, it
  raises at most one MSI before each acknowledgement, as the SDMA trap
  did.
- The screen changes for 5 seconds, as in stage 19. If it stays wrong
  after the tool ends, shut down fully.
- Make a Time Machine backup before this boot.

**Expected:**
- Every stage 18 result again.
- Fence 4. The region matches the reversed bands with no CPU write after
  the zeroing.
- On screen for 5 seconds: eight bands, black at the top and white at the
  bottom, with no grey lines. Then the desktop, unchanged.
- One MSI per flip, with an IH entry from client 4, source `0x4f`. No MSI
  during the hold.

**Stage 20 succeeds when:**
- `sudo cezanne-diag --gfxoff-disallow --sdma-flip --psp-state` reports
  `ok` for every stage 18 step up to the acknowledgement, then for:
  - the check;
  - the clear;
  - frame 4: fence 4, and the region equal to the reversed bands;
  - the arm;
  - the flip: the poll and exactly one MSI, from a client 4, source `0x4f`
    entry;
  - the acknowledgement;
  - the verify after 5 s, with no MSI during the hold;
  - the restore, then the stage 18, 17 and 15 restores;
- you saw the reversed bands, and the desktop came back unchanged;
- the final register dump shows the boot 22 values.

**Implementation** (2026-10-07). Diagnostic interface version 16, selectors
39–44, all on the stage 18 connection:

| Step | Selector | What it does |
| --- | --- | --- |
| check | 39 `FlipCheck` | Only after a passing stage 18 acknowledgement, with no stage 19 pattern up. Stage 19's check (pipe 0, `kDisplayExpect`, the region placed, the 1 s checksum, a pipe 0 snapshot), then `SURFACE_FLIP_INTERRUPT`'s enables (bits 0 and 2) 0 and `DCHUB_INTERRUPT_DEST2` bit 0 = 0. A failing flip register is reported as index 19 or 20, after stage 19's 19 entries. No write. |
| fill | 40 `FlipFill` | The CPU zeroes the 8 MiB region through the stage 19 mapping and reads it back. Then frame 4 is written to ring dwords 0–255 and read back, and submitted (`submitSdma` frame 4: `GFX_RB_WPTR` must read 4096; ← 5120, `_HI` ← 0; fence 4 polled for 100 ms). Then the whole region is read against the reversed bands. Any MSI during the step fails it. Reports the step reached (1 clear, 2 frame written, 3 submitted, 4 checked), and the value at the first unexpected word (added after boot 30). |
| show | 41 `FlipShow` | The arm (`0x100`, `0x1`, read back: enable set), then `flipWithIntr`: stage 19's two writes and poll, then up to 100 more pauses for the MSI, then the new IH entries from `IH_RB_RPTR` to the write-back. Returns a `FlipReport`, with the latency from just before the flip's operation to the handler's time for that MSI. |
| acknowledge | 42 `FlipAck` | `SURFACE_FLIP_INTERRUPT` ← `0x101`, then stage 18's `ackIntr` (`IH_RB_RPTR` ← the write-back, 100 ms, no re-fire). |
| verify | 43 `FlipVerify` | Reads only: stage 19's verify against the reversed bands, and the MSI count unchanged since the acknowledgement. |
| restore | 44 `FlipRestore` | `restoreFlip`: the flip back with its MSI recorded, `0x101` then 0, pipe 0 at boot 22 and `SURFACE_FLIP_INTERRUPT`'s enables 0 (index 11). Then `ackIntr`. Its result is also the property `CezanneGPU flip restore`. |

- **No new writable page set.** The flip-interrupt and surface writes use
  the stage 19 display page (`0xe000`). `GFX_RB_WPTR` uses the stage 15
  SDMA page set, and the IH acknowledgements the stage 18 page set.
- **The restore is owed from the arm on.** The kext marks it owed before
  the arm's writes. The stage 18 restore runs it first (so the stage 17
  restore, the stage 15 stop and an abandoned connection do too), and so
  does `stop()`, before it removes the handler.
- **Frame 4 is submitted only by the fill.** Selector 23 still accepts
  frames 0–3 only.
- **The tool:** `--sdma-flip` runs the `--ih-intr` chain. After the stage 18
  acknowledgement it runs check, fill, show, acknowledgement, `sleep(5)`,
  verify and restore. The restore always runs once the show was sent. Then
  come the stage 18 restore and the rest. The register dump includes the 3
  new registers from stage 20. Separately, `--inventory16` now prints
  pipe 0's pitch as the register's value (1920), as boot 28 showed the
  hardware uses it, not Linux's value + 1.

**Choices not fixed by the proposal:**
- **The disable writes `0x101`, then 0,** not `0x100` then 0. Linux's
  acknowledgement is a read-modify-write that keeps the enable bit, so on an
  enabled interrupt it writes `0x101`. Both values were already in the
  proposal's list, and the result is the same: cleared, then disabled.
- **The fill's step fails on any MSI.** Frame 4 has no `TRAP`, so an MSI
  during the fill would come from something else and is reported.
- **The verify compares pipe 0 with the stage 20 check's snapshot,** as
  stage 19 does.
- **After boot 29, the check and the restore read only the two enables**
  (`kFlipIntEnables`, bits 0 and 2). The status latches are the firmware's
  flips. Linux never requires them clear: `dal_irq_service_set` clears
  `SURFACE_FLIP_OCCURRED` before enabling. `SURFACE_FLIP_AWAY_OCCURRED`
  (bit 18) has its own clear bit, which this stage does not write, so it
  stays set after the restore.

### Stage 21: the graphics engine runs our commands and draws (proposal)

**Status: proposed 2026-10-07 (revised the same day at the user's request
for a bolder stage) and approved by the user the same day; implemented and
built (`out/test-efi/usb-stage21`) the same day. Boot 32: firmware loaded,
the RLC and the CP started, and the CP fetched and ran commands. It then
stalled in the clear-state preamble, and everything was restored. Boot 33:
the stall is the CP's ring fetch waiting on memory. Boot 34, with the golden
settings: the same stall; the PFP waits on a register read that never
returns.**

**Purpose.** This is the first time the main graphics engine (GC 9.3)
executes commands from the driver. The user asked for visible progress, so
one stage goes from firmware to a picture on screen, in steps that each
run only if the previous one passed (as stage 20 did):
1. **load the GFX firmware** through the PSP;
2. **start the RLC**, the GC's control microcontroller;
3. **start the command processor (CP)** with a GFX ring;
4. **ring test:** a register write executed by the CP (Linux's
   `gfx_v9_0_ring_test_ring`);
5. **fence:** the CP writes a value to memory;
6. **the CP draws the screen:** its DMA fills the pattern region with three
   bands (red, green, blue), and pipe 0 shows them for 5 s;
7. **restore:** CP halted, RLC stopped, every GC register written back to
   the value the check read, then the PSP teardown.

**Not in this stage:**
- **shaders** (real rendering): the golden settings, `constants_init`
  (shader apertures, GDS, per-VMID setup), the KIQ and the compute queues
  come with the first shader stage;
- **the EOP interrupt:** fences are polled, and no IH work is needed;
- **doorbells:** the write pointer is a register write, as for SDMA in
  stage 15 (`gfx_v9_0_ring_set_wptr_gfx` without a doorbell).

**It stands alone.** `sudo cezanne-diag --gfxoff-disallow --gfx-start
--psp-state` runs stage 12's ring and TMR, then this stage, then stage 12's
teardown. It needs no SDMA, GART or IH step. GFXOFF must be disallowed
first: GC must stay powered, and its registers are GFX-gated reads.

**Part A, the firmware load (Linux v6.12):**

- **Images and order.** `gfx_v9_0_init_microcode` registers these for GC
  9.3.0 with PSP loading:
  - `CP_PFP`, `CP_ME` and `CP_CE` (`amdgpu_gfx_cp_init_microcode`);
  - `CP_MEC1` and `CP_MEC1_JT`. MEC2 is not loaded:
    `gfx_v9_0_load_mec2_fw_bin_support` is false for GC 9.3.0;
  - `RLC_G`, and for RLC header v2.1 the three save/restore lists
    `RLC_RESTORE_LIST_CNTL`, `_GPM_MEM` and `_SRM_MEM`
    (`amdgpu_gfx_rlc_init_microcode_v2_1`).

  `psp_load_non_psp_fw` loads them in `AMDGPU_UCODE_ID` order, so the RLC
  comes last: CE, PFP, ME, MEC1, MEC1_JT, the three lists, RLC_G.
- **No autoload.** PSP v12 sets `autoload_supported = false`
  (`psp_early_init`). So no `psp_rlc_autoload_start`, and loading `RLC_G`
  does not start the RLC: `gfx_v9_0_rlc_start` does that later.
- **The bytes** (`amdgpu_ucode_init_single_fw`):
  - CE, PFP, ME and `RLC_G`: `ucode_size_bytes` from
    `ucode_array_offset_bytes`;
  - `MEC1`: `ucode_size_bytes − jt_size × 4`;
  - `MEC1_JT`: `jt_size × 4` bytes at `ucode_array_offset_bytes + jt_offset
    × 4`;
  - the lists: their sizes and offsets from the v2.1 header.

  Each image goes into a page-aligned slot of the firmware buffer.
- **The command** is stage 13's `LOAD_IP_FW` (6): the slot's GPU address,
  the payload size, and the `psp_gfx_if.h` type: CE 3, PFP 2, ME 1, MEC 4,
  MEC_ME1 (the jump table) 5, the three lists 22, 20 and 21, RLC_G 8.

**The firmware** is the pinned linux-firmware `20260916` set
([firmware provenance](firmware-provenance.md)). `tools/amdgpu_firmware.py`
accepts every file, and the payloads stay opaque:

| # | Image | File, SHA-256 | Payload (file offset, bytes) | Type | Slot offset (pages) |
| --- | --- | --- | --- | --- | --- |
| 1 | CE | `green_sardine_ce.bin`, `3bba10cf…ca70fd` | 256, 36,352 | 3 | `0x00000` (9) |
| 2 | PFP | `green_sardine_pfp.bin`, `94e1474b…7bdcdc` | 256, 85,504 | 2 | `0x09000` (21) |
| 3 | ME | `green_sardine_me.bin`, `671af908…68fb9c` | 256, 69,120 | 1 | `0x1e000` (17) |
| 4 | MEC1 | `green_sardine_mec.bin`, `0e4c6712…4eba6` | 256, 267,072 | 4 | `0x2f000` (66) |
| 5 | MEC1 jump table | the same file | 267,328, 896 | 5 | `0x71000` (1) |
| 6 | RLC list CNTL | `green_sardine_rlc.bin`, `66f4397c…db319d` | 26,832, 592 | 22 | `0x72000` (1) |
| 7 | RLC list GPM | the same file | 27,424, 2,560 | 20 | `0x73000` (1) |
| 8 | RLC list SRM | the same file | 29,984, 9,944 | 21 | `0x74000` (3) |
| 9 | RLC_G | the same file | 256, 16,896 | 8 | `0x77000` (5) |

The pinned headers give these values: CE, PFP and ME `ucode_size_bytes`;
MEC `jt_offset` 66,768 and `jt_size` 224; RLC v2.1 list sizes and offsets.
The core accepts an image only if its header matches them. MEC2 and the
other images are not used.

**Placement.** A new firmware buffer at carveout `0x40900000` (GPU
`0xF440900000`, physical `0x600900000`), 496 KiB (`0x7c000`). It sits in
the free space between the GART work area and the stage 19 pattern
region. The 1 MiB around it gets the stage 9 placement checks and a 1 s
stability snapshot. The ring, command and fence pages (stage 12) and the
TMR (`0x40400000`, 4 MiB) are unchanged.

**How the firmware reaches the driver:** embedded at build time, as for
SDMA. `build.sh` checks each file's SHA-256 against the pin above and
generates the arrays; the files stay in ignored `out/`.

**Part B, the RLC start (Linux `gfx_v9_0_rlc_resume`, GC 9.3.0, bare
metal).** The RLC firmware is loaded by the PSP, so
`gfx_v9_0_rlc_load_microcode` is skipped. Renoir's `pg_flags` (`soc15.c`)
have no GFX power gating, so `init_pg` writes no jump table and runs no
`init_gfx_power_gating`. GC 9.3.0 is neither 9.2.1 nor Raven2, so the
save/restore lists are not written by MMIO (the PSP loaded them).

| # | Linux | Register (byte offset) | Value |
| --- | --- | --- | --- |
| B1 | `rlc_stop` | `RLC_CNTL` (`0x3b000`) | `RLC_ENABLE_F32` ← 0: the value read with bit 0 clear (boot: 0) |
| B2 | `enable_gui_idle_interrupt(false)` | `CP_INT_CNTL_RING0` (`0x0c1a8`) | the value read with bits 18–21 clear |
| B3 | `wait_for_rlc_serdes` | `GRBM_GFX_INDEX` (`0x30800`) | `0x40000000` (SE 0, SH 0, instances broadcast); poll `RLC_SERDES_CU_MASTER_BUSY` = 0; then `0xe0000000` (broadcast); poll `RLC_SERDES_NONCU_MASTER_BUSY` & `0x000dffff` = 0 |
| B4 | disable CG | `RLC_CGCG_CGLS_CTRL` (`0x3b124`) | 0 |
| B5 | `init_csb` | the clear-state buffer, then `RLC_CSIB_ADDR_HI` (`0x3b28c`), `_LO` (`0x3b288`), `RLC_CSIB_LENGTH` (`0x3b290`) | `0xf4`, `0x40a03000`, 904 (dwords) |
| B6 | `enable_save_restore_machine` | `RLC_SRM_CNTL` (`0x3b200`) | the value read with `SRM_ENABLE` (bit 0) set |
| B7 | `update_spm_vmid_internal(0xf)` | `RLC_SPM_MC_CNTL` (`0x3b1c4`) | the value read with `RLC_SPM_VMID` (bits 3:0) = `0xf` |
| B8 | `rlc_start` | `RLC_CNTL` | `RLC_ENABLE_F32` ← 1; 50 µs. On an APU Linux does not re-enable the GUI-idle interrupt here |

The clear-state buffer is `gfx_v9_0_get_csb_buffer`'s 904 dwords, built
from `clearstate_gfx9.h` (`gfx9_cs_data`: 8 context extents, 879 register
defaults). That table is under AMD's MIT licence, so it is carried in the
core.

**Part C, the CP start (Linux `gfx_v9_0_cp_gfx_resume` and
`gfx_v9_0_cp_gfx_start`, without a doorbell).** The ring is 8 KiB (2048
dwords), as Linux sizes its GFX ring (1024 dwords × 2 submissions).

| # | Register (byte offset) | Value |
| --- | --- | --- |
| C1 | `CP_RB_WPTR_DELAY` (`0x08704`) | 0 |
| C2 | `CP_RB_VMID` (`0x0c144`) | 0 |
| C3 | `CP_RB0_CNTL` (`0x0c104`) | `0x0000080a` (`RB_BUFSZ` 10, `RB_BLKSZ` 8) |
| C4 | `CP_RB0_WPTR` (`0x0c150`), `_HI` (`0x0c154`) | 0, 0 |
| C5 | `CP_RB0_RPTR_ADDR` (`0x0c10c`), `_HI` (`0x0c110`) | `0x40a02000`, `0xf4` |
| C6 | `CP_RB_WPTR_POLL_ADDR_LO` (`0x0c118`), `_HI` (`0x0c11c`) | `0x40a02008`, `0xf4` |
| C7 | 1 ms, then `CP_RB0_CNTL` again | `0x0000080a` |
| C8 | `CP_RB0_BASE` (`0x0c100`), `_HI` (`0x0c2c4`) | `0xf440a000`, 0 |
| C9 | `CP_MAX_CONTEXT` (`0x0c2b8`), `CP_DEVICE_ID` (`0x0c12c`) | 7 (`max_hw_contexts` − 1), 1 |
| C10 | `CP_ME_CNTL` (`0x086d8`) | the value read with `ME_HALT`, `PFP_HALT`, `CE_HALT` (bits 28, 26, 24) clear: the CP runs |

`CP_RB_DOORBELL_CONTROL` must read `DOORBELL_EN` 0, and it is not written.
The doorbell range registers are not written either. The MEC stays halted
(`CP_MEC_CNTL` is not written).

**The ring's frames** are each padded with one `NOP` packet to a 256-dword
boundary (Linux's `align_mask` 0xff). The write pointer is in dwords,
unmasked, as in stage 15:

| Frame | Dwords | Contents | `CP_RB0_WPTR` | Done when |
| --- | --- | --- | --- | --- |
| 0 | 0–1023 | `cp_gfx_start`'s 911 dwords: the clear-state preamble (the same 904 as the buffer), `SET_BASE` CE partition `0x8000`/`0x8000`, `SET_UCONFIG_REG VGT_INDEX_TYPE` 0 | 1024 | the read pointer's write-back reaches 1024 (100 ms) |
| 1 | 1024–1279 | ring test: `SET_UCONFIG_REG SCRATCH_REG0` ← `0xdeadbeef` (after `SCRATCH_REG0` ← `0xcafedead` by MMIO) | 1280 | `SCRATCH_REG0` reads `0xdeadbeef` (100 ms) |
| 2 | 1280–1535 | fence 1: `RELEASE_MEM` (`gfx_v9_0_ring_emit_fence`: `CACHE_FLUSH_AND_INV_TS_EVENT`, index 5, TC/TCL1/TC_MD/TC_WB actions, `DATA_SEL` 1, `INT_SEL` 0), value 1 at write-back `+0x100` | 1536 | memory reads 1 (100 ms) |
| 3 | 1536–2047 | the drawing: 6 `DMA_DATA` fills, then fence 2 (value 2 at `+0x108`) | 2048 | memory reads 2 (1 s) |

The `PACKET3` encodings come from `soc15d.h`. `RELEASE_MEM` dw2 is
`0x00238514` and dw3 is `0x20000000`.

**Part D, the drawing.** The CPU first zeroes the 8 MiB pattern region
(stage 20's clear). Then frame 3 has the CP fill it: each `DMA_DATA`
packet has control `0xc0300000` (`CP_SYNC`, `SRC_SEL` 2 for immediate data,
`DST_SEL` 3 through L2, as Mesa's CP DMA fills on GFX9), the colour, the
destination, and a byte count of 1,382,400 (180 lines, below the 21-bit
limit):

| Lines | Colour | Packets |
| --- | --- | --- |
| 0–359 | red `0xFFFF0000` | 2 |
| 360–719 | green `0xFF00FF00` | 2 |
| 720–1079 | blue `0xFF0000FF` | 2 |

Fence 2's `RELEASE_MEM` writes the L2 back to memory before the fence
value lands. The CPU then reads the whole region against the image. Stage
19's flip shows it for 5 s, stage 19's verify checks it, and the flip goes
back.

**Memory:**

| Buffer | Carveout offset | Size | GPU address |
| --- | --- | --- | --- |
| Ring, command, fence (stage 12) | `0x40100000` | 12 KiB | `0xF440100000` |
| TMR (stage 12) | `0x40400000` | 4 MiB | `0xF440400000` |
| GFX firmware buffer (new) | `0x40900000` | 496 KiB | `0xF440900000` |
| GFX work area (new): ring `+0x0000` (8 KiB), write-back `+0x2000` (read pointer `+0x0`, write-pointer poll `+0x8`, fences `+0x100`/`+0x108`), clear-state buffer `+0x3000` | `0x40a00000` | 16 KiB | `0xF440A00000` |
| Pattern (stages 19–20) | `0x41000000` | 8 MiB | `0xF441000000` |

The new buffers get the stage 9 placement checks and a 1 s stability
snapshot: 1 MiB around the firmware buffer and 64 KiB around the work area.

**How GC reaches memory.** The CP fetches and writes through the GC's own
memory hub (GFXHUB), VMID 0. Stage 15 showed MMHUB VMID 0 reaching FB
aperture addresses with the boot configuration (no page table). The check
requires the GFXHUB to read the same as MMHUB did at boot 22:
- `MC_VM_MX_L1_TLB_CNTL` (GC, `0x0a61c`) = `0x00002501`;
- `VM_CONTEXT0_CNTL` (GC, `0x0a200`) = `0x007ffe80` (context 0 off);
- `VM_L2_CNTL` (GC, `0x0a100`) = `0x00080602`;
- its FB location and offset as stage 10 read them.

If they differ, the stage stops before any write. Programming the GFXHUB
would be a stage of its own.

**The check (no writes)** requires:
- stage 12's check, GFX on, and the nine images' pinned headers;
- the three buffers placed and stable;
- display pipe 0 at boot 22 (stage 19's check);
- `RLC_CNTL` 0; `CP_ME_CNTL` with the three halt bits set; `CP_MEC_CNTL`
  with both MEC halts set; `GRBM_GFX_INDEX` `0xe0000000`;
  `CP_RB_DOORBELL_CONTROL.DOORBELL_EN` 0;
- the GFXHUB values above.

It **snapshots every register this stage writes** (the 23 of parts B and
C, and `SCRATCH_REG0`). Where Linux does a read-modify-write (B1, B2, B6,
B7, C10), the value written is computed from that snapshot. The allowlist
accepts exactly the computed value, so only the field Linux changes may
differ. The restore writes the snapshot back. Earlier stages pinned every
value from a boot reading; here no GC register in parts B and C has been
read before, so the first boot records them. The tool prints the snapshot.

**New reads (all GFX-gated):**
- the 23 registers of parts B and C;
- `RLC_SERDES_CU_MASTER_BUSY` (`0x3b184`) and
  `RLC_SERDES_NONCU_MASTER_BUSY` (`0x3b188`);
- `CP_RB0_RPTR` (`0x08700`);
- the GFXHUB `MC_VM_MX_L1_TLB_CNTL`, `VM_L2_CNTL` and `VM_CONTEXT0_CNTL`;
- the earlier draft's 9 status registers: `GRBM_STATUS2`, `CP_STAT`,
  `CP_CPF_STATUS`, `CP_CPC_STATUS`, `RLC_STAT`, and the PFP, ME, CE and
  MEC1 instruction pointers;
- for the shader stage, recorded only: Renoir's 12 golden registers
  (`golden_settings_gc_9_1_rn`).

All offsets come from `gc_9_0_offset.h`, and the inventory test recomputes
them.

**Steps** (ordered selectors; each printed first):

1. **Check** (above).
2. **PSP ring, `SETUP_TMR`, firmware copy, nine `LOAD_IP_FW`s** (part A),
   then the RLC and CP state read again: still off and halted.
3. **RLC start** (part B). Then `RLC_CNTL` and `RLC_STAT` are read.
4. **CP start** (part C) and frame 0. `CP_RB0_RPTR` and the write-back
   must reach 1024.
5. **Ring test** (frame 1).
6. **Fence 1** (frame 2).
7. **Drawing** (part D): the clear, frame 3 and fence 2, the CPU check, the
   flip, 5 s, the verify, the flip back.
8. **Restore:**
   - `CP_ME_CNTL` ← the snapshot (halted);
   - `rlc_stop` (B1–B3);
   - every part B and C register ← the snapshot, in reverse order;
   - `SCRATCH_REG0` ← the snapshot;
   - then all of them read back against the snapshot (index and value on a
     mismatch);
   - then `DESTROY_TMR` and `DESTROY_RINGS`.

   It runs whenever step 3 was started. A closed or abandoned connection
   runs it, and so does the kext's `stop()`. The flip back runs first if
   the pattern is up.

**New writes:**
- **Registers:** the 23 of parts B and C, with the values above or the
  snapshot (restore), and `SCRATCH_REG0` ← `0xcafedead` (stage 6 already
  allows it). Each goes through a new GC page set. `C2PMSG_67` takes frames
  0–10 (part A).
- **Memory:**
  - the GFX firmware buffer (part A);
  - the GFX work area: the ring's four frames, the zeroed write-back page,
    the clear-state buffer;
  - the pattern region: the CPU's clear;
  - through the CP: the 6 fills, two fences and the read-pointer
    write-back.

**Risks:**
- **This is the first code running on GC.** A hung RLC or CP shows as a
  step timeout. The restore still halts the CP and stops the RLC; a GC
  hang does not stop the display (DCN scans out on its own). If the
  machine hangs, power off: a cold boot resets GC.
- **The PSP may reject an image** (non-zero status). The stage stops, and
  the teardown runs.
- **The CP writes 8 MiB through the GFXHUB.** The destinations are fixed in
  the frame and lie inside the checked region, which is not on screen while
  the CP writes. A wrong translation would show as a fence timeout or
  unexpected words in the region.
- **Values computed from the first reading:** an unexpected snapshot (for
  example the CP or RLC already running) fails the check before any write.
- **Firmware stays in the halted engines until power-off,** as SDMA0's
  does.
- Make a Time Machine backup before this boot.

**Expected:**
- all nine loads with status 0;
- the RLC running (`RLC_STAT` recorded);
- the read pointer at 1024;
- `SCRATCH_REG0` `0xdeadbeef`;
- fences 1 and 2;
- the region equal to the three bands;
- **on screen for 5 s: red, green and blue, top to bottom;**
- then the desktop, and every written register back at its snapshot.

**Stage 21 succeeds when:**
- `sudo cezanne-diag --gfxoff-disallow --gfx-start --psp-state` reports
  `ok` for the check, the loads, the RLC start, the CP start, the ring
  test, fence 1, the drawing (fence 2, the region, the flip, the verify
  and the flip back), the restore and the teardown;
- you saw the three bands, and the desktop came back unchanged;
- the final register dump shows every stage 21 register at its snapshot
  and pipe 0 at boot 22.

**Implementation** (2026-10-07). Diagnostic interface version 17,
selectors 45–52. `--gfx-start` runs stage 13's PSP check, ring create and
`SETUP_TMR`, then these steps, then stage 12's teardown. Every step except
the verify returns a `GfxState`: the 54 registers of `kGfxStateRegisters`
after it. The first 24 are the snapshot list; the rest are status
registers, instruction pointers, serdes, doorbell, GC hub and golden
registers. The tool prints them.

| Step | Selector | What it does |
| --- | --- | --- |
| check | 45 `GfxCheck` | Only right after a passing `SETUP_TMR` by this connection, with no SDMA copy or stage 19 pattern up. The five embedded files' lengths and 38 pinned header words (`kGfxImageInvalid` names the file); GFX on and `kGfxExpect` (index 0–9); the 24-register snapshot; stage 19's pipe 0 check and display snapshot; the placement of the pattern region and the two new buffers; 1 s stability of the firmware buffer's 1 MiB, the work area's 64 KiB and the 8 MiB pattern region. |
| load | 46 `GfxLoad` | Writes the firmware buffer and the GFX work area (ring with all four frames, zeroed write-back page, clear-state buffer), each read back. Then the nine `LOAD_IP_FW`s as frames 1–9, stopping at the first failure; reports the images loaded and the last fence, status and `fw_addr`. The kext then follows the PSP's frame count, so the teardown's `DESTROY_TMR` takes the next frame whatever happened. |
| RLC | 47 `GfxRlc` | Part B. The restore is owed before its first write. |
| CP | 48 `GfxCp` | Part C, then frame 0. It passes when either the read-pointer write-back or `CP_RB0_RPTR` reaches 1024. |
| test | 49 `GfxTest` | The ring test, then fence 1. |
| draw | 50 `GfxDraw` | The CPU clear (stage 20's), frame 3 and fence 2 (1 s), the CPU check against the image, then stage 19's flip. The GOP surface is owed before the flip. |
| verify | 51 `GfxVerify` | Stage 19's verify against the image. Returns a `DisplayReport`. |
| restore | 52 `GfxRestore` | The flip back if owed, then `restoreGfx` if owed; the property `CezanneGPU GFX restore`. |

- **Two allowlists for GC registers.** The fixed values stay in
  `writeAllowed`. The snapshot-relative ones go in `gfxWriteAllowed`, which
  takes the check's snapshot:
  - the values of `kGfxWrites`, each `(snapshot & ~mask) | value`;
  - the snapshot itself, for the restore.

  The core's `writeGfxRegister` checks both before writing. The kext's
  write adapter checks them again, with the snapshot it holds, and only for
  the new GC page set.
- **The GC page set**, `kGfxPageSet`: `0x8000` (`CP_ME_CNTL`,
  `CP_RB_WPTR_DELAY`), `0xc000` (the CP ring registers), `0x30000`
  (`GRBM_GFX_INDEX`, `SCRATCH_REG0`), `0x3b000` (the RLC). `WritePage` now
  holds four pages.
- **New writable carveout mappings**, the only ones besides earlier stages':
  the 124-page firmware buffer and the 4-page work area. Both go through one
  helper, `withGfxMemory`, and each has its own allowlisted store.
- **The clear-state table** is `driver/core/clearstate_gfx9.h`, converted
  from Linux's `clearstate_gfx9.h` with its MIT notice kept and the values
  unchanged (879 values in 8 extents).
- **The firmware:** `build.sh` embeds the five files, each checked against
  its SHA-256 pin (`CEZANNE_GFX_FW_DIR` overrides the directory).
- **The restore is owed** from the RLC start on and runs:
  - from its selector;
  - before the PSP teardown (`pspTeardownLocked`), so also on an abandoned
    connection and from the SDMA stop;
  - from `stop()`.
- **The tool:** `--gfx-start` is `pspSdma` mode 6. It needs
  `--gfxoff-disallow` first, or the check stops with `gfx-not-on`. The
  register dump includes the 39 new registers.

**Choices not fixed by the proposal:**
- **`GRBM_GFX_INDEX` is recorded, not required to be `0xe0000000`.** Every
  boot dump reads `0x00000000`. Its writes are Linux's two (`0x40000000`,
  `0xe0000000`), and the restore puts the reading back.
- **The restore writes the snapshot in list order, not reverse.** That keeps
  `CP_RB0_WPTR_HI` after `CP_RB0_WPTR`: the high write commits the pointer,
  as stage 15 found for SDMA.
- **The serdes waits do not fail the step.** Linux logs a timeout and
  continues; the readings are reported.
- **`GCEA_PROBE_MAP`** (Renoir's twelfth golden register) is not in
  `gc_9_0_offset.h`. It was left out until boot 33; Linux defines it in
  `gfx_v9_0.c` (`0x070c`, segment 0), which gives its offset.
- **The golden settings run first in the RLC step (after boot 33),** not in
  the shader stage as the proposal said. The proposal is kept as approved,
  and the change is recorded in the boot 33 entry.

## Build the test EFIs

On this Mac, with the internal EFI mounted read-only only for the copy (the
`diskutil mount` command needs `sudo`):

```sh
sudo diskutil mount readOnly disk1s1     # internal EFI; identify it with diskutil list first
mkdir -p out/test-efi
cp -Rp /Volumes/EFI/EFI out/test-efi/known-good-EFI
diskutil unmount /Volumes/EFI
driver/kext/build.sh out/test-efi/driver
for stage in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21; do
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

`tools/capture_boot.sh NAME [cezanne-diag flags...]` collects one boot's
evidence in one command.
- **What it saves,** into a new `out/test-efi/NAME`:
  - `bootargs.txt`, with a warning if `cezanne-stage` is missing;
  - the matching `kmutil` lines;
  - `sudo out/diag/cezanne-diag` with the given flags, shown as it runs and
    saved to `diag.txt`, with its exit status in `diag-exit.txt`;
  - `ioreg.plist`.
- **What it refuses:** a name that is not `boot-N-stageM`, and an existing
  folder, so earlier captures are never overwritten.
- **What it leaves out:** the `log show` and `dmesg` lines above, which the
  user runs only when needed.

For example:
`tools/capture_boot.sh boot-24-stage17 --gfxoff-disallow --gart-ih --psp-state`.

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

Stage 17 succeeds when:

- the stage 16 conditions hold with `CezanneGPU stage` 17;
- `sudo cezanne-diag --gfxoff-disallow --gart-ih --psp-state` reports `ok`
  for every stage 15 step up to the verify, then for:
  - the check;
  - the enable: progress 3, `VM_INVALIDATE_ENG17_ACK` bit 0 set;
  - frame 2: fence 2, read pointer 3072;
  - the verify: fault status 0, at least one SDMA0 trap entry (client 8,
    source 224), 0 unexpected words in both regions, 0 display changes;
  - the restore: every register back at its boot 22 value, with
    `IH_RB_WPTR` reported;
  - the stop, as in stage 15;
- the final register dump shows the boot 22 values (including the two
  engine 17 range registers);
- the display and the machine stay as before.

**On failure:**
- `gart-unexpected-state` at the check names the register. Nothing was
  written; record it.
- A failure after the enable still runs the restore and the stop.
- `gart-fault`, `ih-no-trap` and `gart-verify-failed` are findings to
  record.
- A black or glitching screen during the run: let the tool finish (the
  restore runs), then shut down fully. A hang: power off.
- Unexpected words outside the two work areas: power off at once.
- Make a Time Machine backup first. Save the output with `tee`.

Stage 16 succeeds when:

- the stage 15 conditions hold with `CezanneGPU stage` 16 (rerunning
  `--sdma-copy` is optional);
- `sudo cezanne-diag --inventory16` reads all 92 registers with 0 failed
  reads, and prints the pipe decode;
- the display is unaffected.

A failed or hanging read is a finding: note the name on screen.

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

**Boot 20, 2026-10-06, stage 15** (`out/test-efi/usb-stage15/`, written
with `tools/update_stick.sh 15`; cold boot, kernel up 10:16:21 local).

- `CezanneGPU stage` 15, diagnostics v11; stages 1–3 `ok`.
- **The stage 13 load** repeated boot 19: fences 1 and 2, status 0, 0
  unexpected words, checksum `0x25a1ba79`.
- **Copy check:** `ok`. All 30 pinned SDMA registers read their boot 19
  values, and the work area was stable.
- **Start:** `ok`, progress 2, `PowerUpSdma` `0x01`. Afterwards every golden
  and start register reads its table value:
  - `CLK_CTRL` `0x3f000100`, `POWER_CNTL` `0x40000051`, `CHICKEN_BITS`
    `0x02831f07`, `GB_ADDR_CONFIG`/`_READ` `0x00000002`, `UTCL1_WATERMK`
    `0x03fbe1fe`, the poll controls `0x00403000`;
  - `GFX_RB_BASE` `0xf4403000`, the read-pointer address
    `0xf4`/`0x40301000`, the poll address `0xf4`/`0x40301008`.
- **Ring test:** `sdma-timeout`. Observed 0, `GFX_RB_RPTR` 0, and
  **`GFX_RB_WPTR` 0 after writing 1024.** The copy and fence were not
  submitted.
- **Stop:** `ok`. `F32_CNTL` `0x00000001` (halted), `GFX_RB_CNTL`
  `0x00041014`, `PowerDownSdma` `0x01`. Then `DESTROY_TMR` fence 3, and the
  ring destroy `0x80030000`. The machine stayed as before.
- Captures (`diag.txt`, `ioreg.plist`) are in ignored
  `out/test-efi/boot-20-stage15/`.
- Result: **stopped safely: the engine started but never received work.**
  `submitSdma` wrote only the low half of the write pointer, while Linux
  also writes `GFX_RB_WPTR_HI`. Fixed; see the
  [revision](#revision-stage-15-write-pointer-commit).
- **Every SDMA register write was verified on hardware**, and so was the
  full start and stop path.

**Boot 21, 2026-10-06, stage 15 with the write-pointer fix**
(`out/test-efi/usb-stage15/`, written with `tools/update_stick.sh 15`; cold
boot, kernel up 10:26:47 local).

- `CezanneGPU stage` 15, diagnostics v12; stages 1–3 `ok`.
- **Load:** as boots 19 and 20 (fences 1 and 2, status 0, checksum
  `0x25a1ba79`). **Copy check:** `ok`. **Start:** `ok`, progress 2,
  `PowerUpSdma` `0x01`.
- **Ring test:** `ok`. Observed **`0xdeadbeef`**, `GFX_RB_RPTR` 1024,
  `GFX_RB_WPTR` 1024. `F32_CNTL` 0 (running), `STATUS_REG` `0x46deed57`.
- **Copy and fence:** `ok`. **Fence 1**, `GFX_RB_RPTR` 2048, `GFX_RB_WPTR`
  2048.
- **Verify:** `ok`.
  - `GFX_RB_RPTR` 2048.
  - **The destination equals the source word for word.**
  - **0 unexpected words** in the 64 KiB around the work area.
- **Stop:** `ok`. `F32_CNTL` `0x00000001` (halted), `PowerDownSdma` `0x01`,
  `DESTROY_TMR` fence 3, ring destroy `0x80030000`.
- **Afterwards:** `GFX_RB_RPTR` and `GFX_RB_WPTR` stay at `0x800` (2048).
  Otherwise the dump equals boot 20 (golden and start values in place,
  ring disabled). The machine stayed as before.
- **`STATUS_REG` changed:** it read `0x46dee557` while halted (boots 7 to
  19) and `0x46deed57` while running. Bit 11 differs; recorded for later
  decoding.
- Captures (`diag.txt`, `ioreg.plist`) are in ignored
  `out/test-efi/boot-21-stage15/`.
- Result: **stage 15 succeeded. This is the "verified DMA copy and fence"
  milestone:**
  - SDMA0 ran firmware the PSP loaded;
  - it executed a ring the driver built;
  - it wrote memory (`0xDEADBEEF`), copied 4 KiB, and signalled a fence;
  - nothing else in the checked region changed.
  - Boot 20's missing `GFX_RB_WPTR_HI` write was the only defect.

**Boot 22, 2026-10-06, stage 16** (`out/test-efi/usb-stage16/`, written
with `tools/update_stick.sh 16`; cold boot, kernel up 10:39:35 local).

- `CezanneGPU stage` 16, diagnostics v12; stages 1–3 `ok`.
  `sudo cezanne-diag --inventory16`: **all 92 reads succeeded**, and the
  display re-read showed no changes.
- **Display (DCN 2.1).** One pipe drives the screen.
  - **Pipe 0:**
    - **OTG:** `OTG_CONTROL` `0x80011301`, master enable on. Totals
      `0x897`/`0x464` give 2200 × 1125, and active is 1920 × 1080: the
      CEA-861 1080p60 timing (148.5 MHz pixel clock).
    - **HUBP0:** unblanked (`DCHUBP_CNTL` `0x000f0002`). The surface is
      **`0xF400000000`, the GOP framebuffer at carveout offset 0**.
      Viewport 1920 × 1080, pixel format 8 (`ARGB8888`).
    - **Pitch:** `DCSURF_SURFACE_PITCH` reads `0x780` (1920). Linux programs
      pitch − 1 (`hubp1_program_size`), so the GOP's value is one higher
      than Linux would write for a 1920-pixel surface. The tool decodes it
      the Linux way and prints 1921. This is recorded as an open
      observation, since the framebuffer is exactly 1920 × 1080 × 4 bytes
      (boot 12). Boot 28 settled it: the hardware reads 1920 pixels per
      line.
  - **Pipes 1–3:** OTG off (`0x80000300`), HUBP blanked, no surface.
  - **Connector:** `DIG0_DIG_BE_CNTL` `0x00020100`: front-end source 1,
    `DIG_MODE` 2. DIG1–4 read `0x00010000`.
  - **DCN's view of memory** matches MMHUB: `DCN_VM_FB_LOCATION`
    `0xf400`–`0xf47f`, offset `0x5c0`, AGP 0.
- **Memory hub VM (GART path).**
  - The context 0 page-table base, start and end are all 0.
  - The protection-fault default address is 0.
  - `VM_L2_PROTECTION_FAULT_CNTL` `0x3ffffffc`, `_CNTL2` `0x000a0000`,
    `_STATUS` 0.
  - `VM_L2_CNTL2` 0, `VM_L2_CNTL3` `0x80100007`, `VM_L2_CNTL4` `0x000000c1`.
  - The identity aperture and physical-offset registers are all 0.
  - `VM_INVALIDATE_ENG17_ACK` 0, and the engine 0 address range is 0.
- **Interrupts.**
  - `IH_RB_BASE`/`_HI` 0, `IH_RB_RPTR` 0, write-back address 0, doorbell
    0, `IH_CHICKEN` 0.
  - `IH_RB_WPTR` `0x00080000`: only `RB_MAY_OVERFLOW` set, offset 0.
  - Rings 1 and 2 `IH_RB_CNTL` 0.
  - NBIO `INTERRUPT_CNTL`, `INTERRUPT_CNTL2` and `BIF_IH_DOORBELL_RANGE` all
    0.
- **Finding: `VM_INVALIDATE_ENG17_SEM` is not side-effect free.** It read 1.
  - In `gmc_v9_0_flush_gpu_tlb`, "a read return value of 1 means semaphore
    acquire", and a write of 0 releases it. Linux uses that semaphore for
    MMHUB on this GC 9.3 APU (`gmc_v9_0_use_invalidate_semaphore`).
  - So the inventory's read acquired engine 17's semaphore and left it
    held until the next cold boot.
  - Nothing used it in this boot. The register was a defect in the stage
    16 list; see the [revision](#revision-stage-16-semaphore-read).
  - Stage 17 must acquire and release it exactly as Linux does, and must
    not assume it is free if anything has read it.
- Captures (`diag.txt`, `ioreg.plist`) are in ignored
  `out/test-efi/boot-22-stage16/`.
- Result: **stage 16 succeeded**, apart from the semaphore read, which is
  fixed. Every value stage 17 needs is measured.

**Boot 23, 2026-10-06, stage 16 with the semaphore read removed**
(`out/test-efi/usb-stage16/`; cold boot, kernel up 10:46:41 local).

- `sudo cezanne-diag --inventory16`: **all 91 reads succeeded**, and every
  value equals boot 22.
- The only difference is `HUBP0_DCHUBP_CNTL`, which read `0x000f000a` in
  the first pass and `0x000f0002` in the second. Bit 3 is `HUBP_IN_BLANK`,
  the live vertical-blank status: it depends on when the read lands in the
  frame, and is not a change of state.
- Captures are in ignored `out/test-efi/boot-23-stage16/`.
- Result: **the stage 16 fix is confirmed.**

**Boot 24, 2026-10-06, stage 17** (`out/test-efi/usb-stage17/`, first
build; cold boot; `tools/capture_boot.sh boot-24-stage17 --gfxoff-disallow
--gart-ih --psp-state`).

- GFXOFF disallow, the SDMA0 load (fences 1 and 2) and the stage 15 copy
  (ring test, `COPY_LINEAR`, fence 1, verify) all passed as in boot 21.
- `gart 1/5 check` stopped with `gart-unexpected-state`:
  `HUBP0_DCHUBP_CNTL` read `0x000e0000`, against boot 22's `0x000f0002`.
  Nothing of stage 17 was written: no GART, IH, `SDMA0_CNTL` or engine 17
  write, and no frame 2.
- The stage 15 stop then ran normally (`SDMA0_F32_CNTL` 1, `PowerDownSdma`,
  `DESTROY_TMR` fence 3, ring response `0x80030000`). The final dump read
  `HUBP0_DCHUBP_CNTL` `0x000f0002` again; every GART and IH register still
  held its boot 22 value. `cezanne-diag` exited 1.
- **Finding:** the two bits that differ are `HUBP_NO_OUTSTANDING_REQ`
  (bit 1) and bit 16 of `HUBP_XRQ_NO_OUTSTANDING_REQ` (19:16). Like
  `HUBP_IN_BLANK` (boot 23), they are live status: pipe 0 is scanning out,
  and they report whether a fetch is in flight when the read lands.
  The check masked only bit 3.
- **Fix (within the stage):** `kHubpLiveStatus` = `0x000f000a` (bits 1, 3
  and 19:16) is masked out in the precondition check and in the verify's
  display comparison. `HUBP_BLANK_EN`, `HUBP_DISABLE`, the timeout and the
  underflow status are still compared. The core test now accepts the boot
  24 value and rejects an underflow status. The first build is kept as
  `superseded-*-stage17-hubp`.
- Captures are in ignored `out/test-efi/boot-24-stage17/`.
- Result: **stopped before any stage 17 write; the check is fixed.** Boot 25
  repeats the run with the rebuilt `usb-stage17`.

**Boot 25, 2026-10-06, stage 17 with the live-status mask**
(`out/test-efi/usb-stage17/`, rebuilt; cold boot; `tools/capture_boot.sh
boot-25-stage17 --gfxoff-disallow --gart-ih --psp-state`; exit 0).

- GFXOFF, the SDMA0 load and the stage 15 copy passed as in boot 21.
- **Check:** ok, with every GART, IH and `SDMA0_CNTL` register at its
  boot 22 value.
- **Enable:** ok, progress 3. The engine 17 flush completed with
  `VM_INVALIDATE_ENG17_ACK` `0x00010001`.
- **Frame 2:** `COPY_LINEAR` read 4 KiB from GART address 0 through PTE 0;
  fence 2 arrived; `GFX_RB_RPTR` 3072.
- **Verify:** ok.
  - `VM_L2_PROTECTION_FAULT_STATUS` 0.
  - Both 64 KiB regions held only expected words, so the second destination
    equals the source.
  - The IH write-back and `IH_RB_WPTR` are both `0x20`: exactly one entry,
    `8000e008 00000003 0 …` = client 8 (SDMA0), source 224 (`SDMA_TRAP`),
    ring 0, VMID 0. No other client posted.
  - 0 display changes.
- **Restore:** ok. `IH_RB_WPTR` went back to `0x00080000` (boot 22).
- **Stop:** as in stage 15 (`DESTROY_TMR` fence 3, ring response
  `0x80030000`).
- **Final dump against boot 24's:** the only differences are:
  - `SDMA0_GFX_RB_RPTR`/`WPTR` `0xc00` (frame 2);
  - the live `HUBP0_DCHUBP_CNTL` status bits;
  - the `MP0_SMN_C2PMSG_81` counter;
  - `VM_INVALIDATE_ENG17_ACK` 1. This is the restore flush's own
    acknowledgement, a status register that Linux also leaves set; it is
    not restored and not part of the GART state.
  
  The engine 17 range registers read 0/0, as assumed.
- Captures are in ignored `out/test-efi/boot-25-stage17/`.
- Result: **stage 17 succeeded.**

**Boot 26, 2026-10-06, stage 18** (`out/test-efi/usb-stage18/`, first
build; cold boot; `tools/capture_boot.sh boot-26-stage18 --gfxoff-disallow
--ih-intr --psp-state`; exit 1).

- The stage 15 copy and the whole stage 17 flow passed as in boot 25.
- **Check:** ok. The MSI vector is interrupt index 1. Its capability read
  control `0x0084` (64-bit, disabled), address `0xfee00000`, data `0x4079`.
- **Enable:** ok, progress 2. macOS enabled the capability (control
  `0x0085`), with the same address and data.
- **Frame 3:** fence 3; `GFX_RB_RPTR` read 4096.
- **Verify: `intr-verify-failed`, because the MSI count was 2.**
  - Everything else passed: one ring entry (client 8, source 224), fence 3,
    no VM fault, both regions clean, display unchanged.
  - The entry's dw1 now holds a GPU timestamp (`3e2870d9`, from
    `RB_GPU_TS_ENABLE`), and dw2 is 2.
  - `IH_RB_CNTL` read back `0x40330195`: `WPTR_OVERFLOW_CLEAR` (bit 31)
    reads 0, so it acts as a write strobe.
  - The latency read 0 µs, which the kext reports when the first MSI came
    **before** the frame 3 write pointer was written. So one MSI arrived
    after the handler was enabled and before the trap existed, most likely
    at the `ENABLE_INTR` write while the ring was empty. The second is the
    trap's. With `RPTR_REARM` and no `IH_RB_RPTR` write between them, the
    first did not block the second.
- The acknowledgement was skipped (the verify had failed). The interrupt
  restore, the stage 17 restore and the stop all passed, and the MSI
  capability went back to disabled (`0x0084`). The final dump shows every
  IH, NBIO, GART and `SDMA0_CNTL` register at its boot 22 value.
- The machine stayed up and the display was unchanged: **the first
  interrupt handler worked.**
- **Fix (within the stage):** the kext records the MSI count when frame 3 is
  submitted and the time of each of the first 8 MSIs. The verify requires
  exactly one MSI after the submit, and reports those before it. The tool
  prints every MSI's time from the `ENABLE_INTR` write, and the submit's. No
  register, value or step changed. The first build is kept as
  `superseded-*-stage18-count`.
- Captures are in ignored `out/test-efi/boot-26-stage18/`.
- Result: **MSI delivery works; the verify's count is fixed.** Boot 27
  repeats the run and should show when the extra MSI arrives.

**Boot 27, 2026-10-06, stage 18 with the count after the submit**
(`out/test-efi/usb-stage18/`, rebuilt; cold boot; `tools/capture_boot.sh
boot-27-stage18 --gfxoff-disallow --ih-intr --psp-state`; exit 0).

- The stage 15 copy and the stage 17 flow passed as before.
- **Check and enable:** ok, as in boot 26 (MSI index 1; the capability goes
  from `0x0084` to `0x0085` with address `0xfee00000`, data `0x4079`).
- **Frame 3:** fence 3, `GFX_RB_RPTR` 4096.
- **Verify: ok.** The times are measured from the `ENABLE_INTR` write:
  - **MSI 1 at 23 µs**, before the submit at 52 µs, with the ring empty.
    This confirms boot 26: setting `ENABLE_INTR` raises one MSI by itself.
    Linux's handler would find `rptr == wptr` and do nothing.
  - **MSI 2 at 83 µs**: the trap's, 31 µs after the write pointer.
  - One ring entry, client 8, source 224, ring 0, VMID 0. dw1 holds the GPU
    timestamp `085b9e46`.
  - Fence 3, no VM fault, both regions clean, display unchanged.
- **Acknowledge: ok.** `IH_RB_RPTR` ← `0x20`. After 100 ms the MSI count was
  still 2 and the write-back still `0x20`: no re-fire.
- **Restores and stop:** ok. The MSI capability went back to `0x0084`.
  Against boot 26, the final dump differs only in the `C2PMSG_81` counter.
- **Observations for later stages:**
  - The `ENABLE_INTR` MSI does not use up `RPTR_REARM`'s single interrupt:
    the trap still raised one with no `IH_RB_RPTR` write in between.
  - Delivery takes about 30 µs from the write-pointer commit,
    measured with the handler's `mach_absolute_time()`.
- Captures are in ignored `out/test-efi/boot-27-stage18/`.
- Result: **stage 18 succeeded.**

**Boot 28, 2026-10-07, stage 19** (`out/test-efi/usb-stage19/`; cold boot;
`tools/capture_boot.sh boot-28-stage19 --display-pattern --psp-state`;
exit 0).

- **Check: ok.** Pipe 0 matched boot 22, the 8 precondition registers
  matched, and the pattern region was placed and stable (checksum
  `0x556749721d2b9d98`). Frame count 11238.
- **Flip: ok.** The pattern was written and read back, then
  `EARLIEST_INUSE` reached `0xF441000000` after 13 ms, within one frame.
  Frame count 11357: the 1 s checksum and the 8 MiB write and read-back
  took about 2 s (119 frames).
- **What the user saw:** eight horizontal colour bands, and grey lines that
  **looked straight**.
- **Verify after 5 s: ok.** Still the pattern; 300 frames since the flip
  (60 Hz); `DCSURF_FLIP_CONTROL` `0x04100000`. Pipe 0 unchanged apart from
  the address, and no unexpected pattern words.
- **Restore: ok.** Back to `0xF400000000` after 4 ms. Pipe 0 matched boot 22,
  and the user saw the desktop return unchanged.
- **Final dump against boot 22:** apart from the 9 new registers, it differs
  only in the PSP counter `C2PMSG_81`, `DCHUBP_CNTL`'s live status bits
  (`0x000e0000`, inside `kHubpLiveStatus`) and
  `VM_INVALIDATE_ENG17_ACK` (0), which stage 19 does not touch.
- **First readings of the new registers:**
  - `DCSURF_FLIP_CONTROL` `0x04100000`, the same during the hold and after;
  - `DCSURF_TILING_CONFIG` `0x00000080` (`SW_MODE` 0, linear);
  - `SURFACE_CONTROL`, metadata and `VMID_SETTINGS_0` all 0.
- **The pitch question (open since boot 22):** with `DCSURF_SURFACE_PITCH`
  `0x780`, a 1921-pixel pitch would have shifted each line one pixel
  further, so the lines would lean by the full screen height (1080
  pixels). They were straight, so the hardware reads 1920 pixels per line:
  the register holds the pitch itself. Why Linux's `hubp2_program_size`
  writes `surface_pitch - 1` is not resolved here. `cezanne-diag`'s
  inventory decode still adds 1 and prints 1921.
- Captures are in ignored `out/test-efi/boot-28-stage19/`.
- Result: **stage 19 succeeded.**

**Boot 29, 2026-10-07, stage 20** (`out/test-efi/usb-stage20/`, first
build; cold boot; `tools/capture_boot.sh boot-29-stage20 --gfxoff-disallow
--sdma-flip --psp-state`; exit 1).

- Every stage 15, 17 and 18 step passed as in boot 27:
  - the stage 18 verify: MSI at 23 µs (`ENABLE_INTR`) and 83 µs (the trap),
    32 µs after the submit;
  - the acknowledgement: no re-fire.
- **Flip check: `flip-unexpected-state`.**
  `HUBPREQ0_DCSURF_SURFACE_FLIP_INTERRUPT` read `0x00050000` where the check
  required 0. These are `SURFACE_FLIP_OCCURRED` (bit 16) and
  `SURFACE_FLIP_AWAY_OCCURRED` (bit 18), the status latches of the
  firmware's flips. Both enables (bits 0 and 2) were 0.
  `DCHUB_INTERRUPT_DEST2` read 0 (routed to the host) and
  `DISP_INTERRUPT_STATUS_CONTINUE17` read 0.
- **No stage 20 write was made.** The screen did not change (the user saw
  no pattern).
- **Restores and stop:** ok. The stage 18 restore, the stage 17 restore and
  the stage 15 stop all ran.
- **Fix (within the stage):** the check requires only the two enables to be
  0, and the restore requires the same. Linux clears `OCCURRED` with the
  arm's first write. Rebuilt for boot 30; the first build is kept as
  `superseded-*-stage20-latch`.
- Captures are in ignored `out/test-efi/boot-29-stage20/`.

**Boot 30, 2026-10-07, stage 20 with the enables-only check**
(`out/test-efi/usb-stage20/`, rebuilt; cold boot; `tools/capture_boot.sh
boot-30-stage20 --gfxoff-disallow --sdma-flip --psp-state`; exit 1).

- Stages 15, 17 and 18 passed as before.
- **Flip check: ok.** Region checksum `0x01c004dc69efe5a2`, frame count
  7852. `DCHUB_INTERRUPT_DEST2` and `DISP_INTERRUPT_STATUS_CONTINUE17` read 0.
- **Fill: `flip-fill-mismatch` at step 4.** This is the first CPU read of
  SDMA's work:
  - the clear and frame 4 passed;
  - fence 4 arrived, with `GFX_RB_RPTR` 5120 and no MSI;
  - **1,814,400 words were wrong, first at `+0x0`.**

  That count is exactly 7 bands of 259,200 words: one band was right.
- **Cause.** Frame 4's header left `FILLSIZE` 0, as Linux does, and that
  fills bytes with the data's low byte. Of the eight colours only white
  (`0xFFFFFFFF`) has the same low byte in every position, so only the white
  band was right. The black band at `+0x0` was filled with `0x00` bytes.
- **No display write was made.** The show step never ran, and the user saw
  no pattern. The stage 18 restore, the stage 17 restore and the stage 15
  stop all reported ok.
- **Fix (within the stage):**
  - the header is `0x8000000b` (`FILLSIZE` 2, dword fills), as RADV writes
    it;
  - the fill step now also reports the value read at the first wrong word.

  The second build is kept as `superseded-*-stage20-fill`.
- Captures are in ignored `out/test-efi/boot-30-stage20/`.

**Boot 31, 2026-10-07, stage 20 with dword fills**
(`out/test-efi/usb-stage20/`, rebuilt; cold boot; `tools/capture_boot.sh
boot-31-stage20 --gfxoff-disallow --sdma-flip --psp-state`; exit 0).

- **Stages 15, 17 and 18:** passed as before. The trap's MSI came 45 µs
  after the submit, and the `ENABLE_INTR` MSI at 29 µs.
- **Check:** ok. Frame count 5148, region checksum `0x03bbaa4f11fa4326`.
- **Fill: ok.** Fence 4, `GFX_RB_RPTR` 5120, and **all 8 MiB matched the
  reversed bands**: SDMA drew the whole surface. No MSI during the fill.
- **Show: ok.**
  - The arm read back `0x00040001`: enabled, with the flip-away latch.
  - `EARLIEST_INUSE` reached `0xF441000000` after 7 ms.
  - **One MSI, 7.2 ms after the address writes**, which is the wait for the
    next vertical sync.
  - One IH entry: client 4, source `0x4f` (79), dw0 `0x80004f04`, with a
    GPU timestamp in dw1 and dw4.
  - `SURFACE_FLIP_INTERRUPT` read `0x00070001` (occurred, status, flip-away
    latch, enabled) and `DISP_INTERRUPT_STATUS_CONTINUE17` read
    `0x00000004`: the HUBP0 flip bit, as `irqsrcs_dcn_1_0.h` names it.
- **What the user saw:** eight bands, black at the top and white at the
  bottom, no grey lines.
- **Acknowledgement: ok.** `0x101` left `0x00040001`: status cleared,
  still enabled. `IH_RB_RPTR` ← `0x40`, and after 100 ms the MSI count was
  still 3: no re-fire.
- **Verify after 5 s: ok.** 306 frames since the flip, **no MSI during the
  hold** (the flip interrupt fires once per flip), pipe 0 unchanged, and the
  pattern intact.
- **Restore: ok.** Back to `0xF400000000` after 1 ms. The flip back raised
  its own MSI (3 → 4) and entry. `IH_RB_RPTR` ← `0x60`. The user saw the
  desktop return unchanged.
- **Then the stage 18 restore, the stage 17 restore and the stage 15
  stop:** all ok.
- **Final dump against boot 27,** which ran the same chain without
  stage 20: it differs only in the PSP counter `C2PMSG_81` and SDMA's ring
  pointers (5120, frame 4). Pipe 0 matches. `SURFACE_FLIP_INTERRUPT` ends at
  `0x00040000`: disabled, with the flip-away latch, as expected.
- Captures are in ignored `out/test-efi/boot-31-stage20/`.
- Result: **stage 20 succeeded.**

**Boot 32, 2026-10-07, stage 21** (`out/test-efi/usb-stage21/`, first
build; cold boot; `tools/capture_boot.sh boot-32-stage21 --gfxoff-disallow
--gfx-start --psp-state`; exit 1). **These are the first GC register
readings.**

- **Check: ok.** Snapshot (boot values):
  - `RLC_CNTL` 0, `CP_ME_CNTL` `0x15000000`, `CP_MEC_CNTL` `0x50000000`,
    `CP_INT_CNTL_RING0` 0;
  - `RLC_CGCG_CGLS_CTRL` `0x0001003c`, `RLC_SRM_CNTL` 2, `RLC_SPM_MC_CNTL` 0,
    `RLC_CSIB_*` 0;
  - `CP_RB0_CNTL` `0x00400000`, `CP_RB0_BASE` `0xfedcbaef` (an uninitialised
    pattern), `CP_MAX_CONTEXT` 7, `CP_DEVICE_ID` 0, the other ring registers
    0;
  - `GRBM_GFX_INDEX` 0 and `SCRATCH_REG0` 0;
  - the GC hub as MMHUB at boot 22;
  - golden registers: `GB_ADDR_CONFIG` `0x24000011` (Linux's golden value
    is `0x24000042`, under a mask), `CB_HW_CONTROL` `0x00014107`,
    `PA_SC_ENHANCE` 1, `PA_SC_ENHANCE_1` `0x04040000`, `TCP_CHAN_STEER`
    `0xfedcba98`/`0x76543210`, the others 0.
- **Load: ok.** All nine `LOAD_IP_FW`s returned status 0, fence 10.
  `fw_addr` read 0, as for SDMA0 in stage 13.
- **Finding: loading `RLC_G` started the RLC.** After the loads, before any
  GC write, `RLC_CNTL` read 1 and `RLC_SRM_CNTL` read 3. That is
  `RLC_ENABLE_F32` and `SRM_ENABLE` set, although PSP v12 does not use
  autoload in Linux. `RLC_STAT` stayed 0.
- **RLC start: ok.** 11 writes, serdes idle. `RLC_CSIB_*` at
  `0xf4`/`0x40a03000`/904, `RLC_SPM_MC_CNTL` `0xf`, `RLC_CNTL` 1.
- **CP start: `gfx-cp-timeout`.** All 15 writes took: the ring registers
  read back as written, and `CP_ME_CNTL` read 0. **The CP ran:**
  - `CP_RB0_RPTR` reached 152 (0x98) of frame 0's 1024 dwords;
  - the PFP, ME and CE instruction pointers moved (`0x38`, `0xa`, `0x4c`).

  It then stopped. Decoded:
  - `CP_STAT` `0x84008200`: ROQ ring, PFP, CE and CP busy;
  - `CP_CPF_STATUS` `0x94000023`, `CP_BUSY_STAT` `0x00400000`
    (`CE_PARSING_PACKETS`);
  - `GRBM_STATUS` `0xa0003028` (`CP_BUSY`, `GUI_ACTIVE`);
  - the ME's command FIFO was empty.

  Dword 152 lies inside the preamble's first `SET_CONTEXT_REG`
  (dwords 5–218, 212 context registers). The read-pointer write-back stayed
  0: the CP writes it per `RB_BLKSZ` block (512 dwords), which it never
  reached.
- **Restore: ok.** The CP was halted, the RLC stopped, and all 24 registers
  were back at the snapshot. The flip back was not needed. The PSP teardown
  passed (`DESTROY_TMR` fence 11). The CP's busy bits stay set until a cold
  boot.
- **What the readings cannot say** is which packet the PFP and CE are
  waiting on, or why.
- **Fix (within the stage):** every stage 21 state report now also reads
  11 more registers from Linux's GC hang dump (`gc_reg_list_9`):
  `CP_STALLED_STAT1`/`2`, `CP_CPF_STALLED_STAT1`, `CP_CPF_BUSY_STAT`,
  `CP_GFX_ERROR`, the CE, PFP and ME header dumps, `RLC_GPM_GENERAL_6`,
  `RLC_SAFE_MODE` and `RLC_INT_STAT`. There are no new writes. The first
  build is kept as `superseded-*-stage21-stall`.
- Captures are in ignored `out/test-efi/boot-32-stage21/`.

**Boot 33, 2026-10-07, stage 21 with the stall diagnostics**
(`out/test-efi/usb-stage21/`, rebuilt; cold boot; `tools/capture_boot.sh
boot-33-stage21 --gfxoff-disallow --gfx-start --psp-state`; exit 1).

- **The same result as boot 32:** the loads and the RLC start passed, the CP
  ran to `CP_RB0_RPTR` 152 and stopped, and everything was restored. That
  makes it reproducible.
- **RLC firmware:** `RLC_GPM_GENERAL_6` reads `0x59` once loaded (0
  before). The RLC firmware is running.
- **The stall reason**, decoded from `gc_9_0_sh_mask.h`:
  - **`CP_CPF_STALLED_STAT1` `0x1` (`RING_FETCHING_DATA`)** and
    `CP_CPF_BUSY_STAT` `0x2` (`CSF_RING_BUSY`). The fetcher is waiting for
    ring data from memory: a read that never returns.
  - `CP_STALLED_STAT1` `0x00000c00`: `ME_HAS_ACTIVE_CE_BUFFER_FLAG` and
    `ME_HAS_ACTIVE_DE_BUFFER_FLAG` only.
  - `CP_GFX_ERROR` 0. The header dumps read `0xdefNdefN` (no header
    recorded).
- **Reading:** the commands are not the problem. The CP's memory path
  answered the first fetches, up to 152 dwords, then stopped answering.
- **The fix (within the stage): Linux's golden settings first.**
  `gfx_v9_0_hw_init` programs `golden_settings_gc_9_1_rn` before anything
  else, and this stage had deferred them to the shader stage. Three of the
  twelve configure the GC's memory path, and boot 32 read them far from
  Linux's values:
  - `GB_ADDR_CONFIG` `0x24000011`: 2 pipes, against Linux's `0x24000042`
    (4 pipes, under mask `0xf3e777ff`);
  - `TCP_CHAN_STEER_LO`/`HI` `0x76543210`/`0xfedcba98`: 16 channels,
    against `0x3120`/0;
  - `GCEA_PROBE_MAP`, which Linux sets to `0xcccc` (its offset, `0x09c30`,
    is defined in `gfx_v9_0.c`).

  Requests steered to channels that do not exist would explain a fetch that
  answers at first and then hangs. The RLC step now starts with the twelve
  writes, each `(read & ~mask) | (value & mask)` as
  `soc15_program_register_sequence` does. They go through the
  snapshot-relative allowlist, and the restore puts all twelve back.
- **What else changed:**
  - the snapshot grows to 36 registers;
  - the GC page set gains `0x9000` and `0xa000`;
  - each report also reads `CPF_`, `CPC_` and `CPG_UTCL1_STATUS` and the
    GC hub's `VM_L2_PROTECTION_FAULT_STATUS`, which would show a translation
    fault.

  The boot 33 build is kept as `superseded-*-stage21-fetch`.
- Captures are in ignored `out/test-efi/boot-33-stage21/`.

**Boot 34, 2026-10-07, stage 21 with the golden settings**
(`out/test-efi/usb-stage21/`, rebuilt; cold boot; `tools/capture_boot.sh
boot-34-stage21 --gfxoff-disallow --gfx-start --psp-state`; exit 1).

- **The golden settings took:** `GB_ADDR_CONFIG` `0x24000042`,
  `TCP_CHAN_STEER_LO`/`HI` `0x3120`/0, `GCEA_PROBE_MAP` `0xcccc`. The RLC
  step made 23 writes.
- **The CP stalled exactly as before:** `CP_RB0_RPTR` 152, PFP at `0x38`,
  ME at `0xa`, CE at `0x4c`. So the golden settings were not the cause.
- **No memory fault:** `CPF_`, `CPC_` and `CPG_UTCL1_STATUS` and the GC
  hub's `VM_L2_PROTECTION_FAULT_STATUS` all read 0.
- **New: `CP_STALLED_STAT2` `0x00000020`, `PFP_RCIU_READ_PENDING`.** The
  PFP issued a register read through the RCIU, and the read never returns.
  The fetcher's `RING_FETCHING_DATA` (boot 33) only follows from that: its
  queue is full while the PFP is blocked.
- **Restore and teardown: ok.** All 36 registers, the golden ones included,
  are back at the snapshot.
- **Reading:** a register read waits forever when the target block never
  answers and `GRBM_CNTL.READ_TIMEOUT` is 0. Linux's `constants_init`, the
  `hw_init` step after the golden settings, opens with
  `GRBM_CNTL.READ_TIMEOUT` ← `0xff`. With a timeout the read would complete
  (as an error), and `GRBM_READ_ERROR` (`0x08058`) would record the
  address the PFP read.
- Captures are in ignored `out/test-efi/boot-34-stage21/`.
- **Next: to be decided with the user** (continue with `constants_init`, or
  dial the stage down).

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
