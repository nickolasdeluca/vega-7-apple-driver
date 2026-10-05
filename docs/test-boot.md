# USB test boot

Status, 2026-10-05: **stages 0 to 3 succeeded** (see
[Test boot log](#test-boot-log)). Stage 1 read both boot-state registers with
the expected values. The first stage 0 attempt stalled in OpenCore file
logging, which the test EFI no longer does. Stage 2 read and validated the IP
discovery table from the carveout without writes. Stage 3 read the GC
configuration: 7 of 8 CUs and both RBs active. No later stage is authorized.

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

## Build the test EFIs

On this Mac, with the internal EFI mounted read-only only for the copy (the
`diskutil mount` command needs `sudo`):

```sh
sudo diskutil mount readOnly disk1s1     # internal EFI; identify it with diskutil list first
mkdir -p out/test-efi
cp -Rp /Volumes/EFI/EFI out/test-efi/known-good-EFI
diskutil unmount /Volumes/EFI
driver/kext/build.sh out/test-efi/driver
for stage in 0 1 2 3; do
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
  `MAX_COMPRESSED_FRAGS` (0 vs 1). Linux reads the register in
  `gfx_v9_0_gpu_early_init` and writes the golden settings later in hardware
  init; which value its derived configuration ends up using on this host is
  not established.
- The `ioreg -a` capture is in ignored `out/test-efi/boot-5-stage3/`.
- Result: stage 3 succeeded.

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
