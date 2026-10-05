# USB test boot

Status, 2026-10-05: **stages 0 to 7 succeeded** (see
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
attempt reset before reaching macOS, cause unknown. No later stage is
authorized; disallowing GFXOFF is
[proposed](#proposed-stage-8-disallow-gfxoff) for review.

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

### Proposed stage 8: disallow GFXOFF

**Status: proposal for the user's review. Not authorized, not implemented,
not built.**

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

**What it does not do:**

- It does not allow GFXOFF.
- It sends no clock, power-gating or table message.
- It writes no GC register.
- The scratch test and the version queries are unchanged.

Stage 4 risks: it adds a kernel entry point. It is limited to root and to the
reads stages 1–3 already made, but a defect in the user client could panic
the kernel. Reads happen while the system runs, still with no graphics
driver; the per-read checks stop on a device that left D0 or stopped decoding.
Do not sleep the machine.

## Build the test EFIs

On this Mac, with the internal EFI mounted read-only only for the copy (the
`diskutil mount` command needs `sudo`):

```sh
sudo diskutil mount readOnly disk1s1     # internal EFI; identify it with diskutil list first
mkdir -p out/test-efi
cp -Rp /Volumes/EFI/EFI out/test-efi/known-good-EFI
diskutil unmount /Volumes/EFI
driver/kext/build.sh out/test-efi/driver
for stage in 0 1 2 3 4 5 6 7; do
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
