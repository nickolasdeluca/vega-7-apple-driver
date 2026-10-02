# Cezanne target and firmware selection manifest

Target and observed PCI identity: `1002:1638`, revision `c9`; host reports
Ryzen 5 5600GT, macOS 26.4.1 build 25E253. The [baseline](hardware-baseline.md)
establishes these values through read-only OS queries. This manifest is an
offline source study on 2026-10-02. **No hardware IP revisions, firmware versions,
firmware signatures or GPU register values were measured.**

## Identity selection

Linux v6.12's `amdgpu_drv.c` matches `1002:1638` to
`CHIP_RENOIR | AMD_IS_APU`. In `amdgpu_device_init_apu_flags`, CHIP_RENOIR
devices `1636` and `164c` get `AMD_APU_IS_RENOIR`; other IDs in that family,
including `1638`, get `AMD_APU_IS_GREEN_SARDINE`. This rule uses PCI device ID,
not the observed PCI revision `c9` or the installed macOS product name.
[PCI table](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/amd/amdgpu/amdgpu_drv.c),
[APU flag selection](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/amd/amdgpu/amdgpu_device.c)

Consequently, "Renoir" in the current Metal name cannot choose Renoir firmware.
The Green Sardine prefix is source-backed for the identity branch; exact image
selection still requires the corresponding IP versions and initialization path.

As a separate comparison, NootedRed source pinned at
`8e93e82c497d28814d7066cb5cdd72ac9471d63f` classifies `1638` as Green Sardine
and includes it in an X5000 accelerator personality. This helps explain why
Apple's installed discrete-Vega plist alone cannot describe the working stack.
It does not prove that the installed NootedRed 0.9.0 binary has exactly this
source or establish a usable independent-driver interface.
[NRed.cpp](https://github.com/ChefKissInc/NootedRed/blob/8e93e82c497d28814d7066cb5cdd72ac9471d63f/NootedRed/NRed.cpp),
[Injected personality source](https://github.com/ChefKissInc/NootedRed/blob/8e93e82c497d28814d7066cb5cdd72ac9471d63f/NootedRed/Personalities/com.apple.kext.AMDRadeonX5000.xml)

## Conditional IP dispatch inventory

Linux v6.12 `amdgpu_discovery_set_ip_blocks` has no hardcoded CHIP_RENOIR
case; its default uses discovery data to establish register bases and IP
versions. The table records conditions to verify, rather than asserting those
versions for this host. Every target IP version, base address and instance count
below is currently **unavailable**. Handler filenames are implementation
families, not necessarily hardware version numbers.
[Discovery and dispatch](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/amd/amdgpu/amdgpu_discovery.c)

| Block | Source condition / handler under study | Target validation needed |
| --- | --- | --- |
| Common SoC / NBIO | GC 9.3.0 selects `vega10_common_ip_block`; `soc15.c` selects NBIO helpers separately | Discovery validity, block bases, NBIO version, BAR purpose, doorbell ranges |
| GC graphics/compute | GC 9.3.0 selects `gfx_v9_0_ip_block` | GC version/instances, enabled CU mask, queue limits; shader target mapping separately |
| GMC / GFXHUB / MMHUB | GC 9.3.0 selects `gmc_v9_0_ip_block` | Memory-hub versions/bases, page-table/address width, UMA carveout, DMA translation |
| SDMA | SDMA0 4.1.2 selects `sdma_v4_0_ip_block` | SDMA version/instances, ring/doorbell/fence rules |
| PSP / MP0 | MP0 12.0.1 selects `psp_v12_0_ip_block` | MP0 version, firmware boot state and accepted signed images |
| SMU / MP1 | MP1 12.0.0 or 12.0.1 selects `smu_v12_0_ip_block` | MP1 version, running firmware/table ABI and shared power domains |
| IH / OSSSYS | OSSSYS 4.1.0, 4.1.1 or 4.3.0 are among `vega10_ih_ip_block` cases | Actual OSSSYS version, interrupt format, MSI routing and overflow behavior |
| Display / DCE_HWIP | DCE_HWIP 2.1.0 is a supported display-dispatch case; study DCN2.1 resource code | Display version, pipe count, board connector routing, timing and bandwidth |
| VCN / JPEG | UVD_HWIP 2.2.0 firmware prefix distinguishes Renoir/Green Sardine | Version and media requirements; deferred beyond initial display/compute goals |

No raw discovery table is available from the permitted host evidence. Obtaining
one through BAR mapping, indexed registers or an owner change is outside this
milestone. A future experimental boot must validate identity, discovery bounds,
checksum, instance layout and register bases before accepting this inventory.

## Conditional firmware inventory

`amdgpu_ucode_ip_version_decode` maps GC 9.3.0 and SDMA 4.1.2 to their Green
Sardine prefixes when `AMD_APU_IS_RENOIR` is unset; MP0 12.0.1 directly selects
Green Sardine. UVD_HWIP 2.2.0 likewise distinguishes the two APU paths.
[Prefix decoder](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/amd/amdgpu/amdgpu_ucode.c)

| Candidate filename under `amdgpu/` | Consumer / selection condition | Required versus conditional in the studied upstream path |
| --- | --- | --- |
| `green_sardine_ce.bin`, `green_sardine_pfp.bin`, `green_sardine_me.bin` | `gfx_v9_0_init_cp_gfx_microcode`, Green Sardine GC prefix | Required when initializing graphics rings; not evidence that SDMA alone needs them |
| `green_sardine_rlc.bin` | `gfx_v9_0_init_rlc_microcode` | Required by the studied GC initialization path |
| `green_sardine_mec.bin` | `gfx_v9_0_init_cp_compute_microcode` | Required by the studied compute initialization path |
| `green_sardine_mec2.bin` | Same function, guarded by `gfx_v9_0_load_mec2_fw_bin_support` | Declared, but not requested for GC 9.3.0: the guard is false and MEC2 version/feature metadata is copied from MEC. Other paths may request a separate image and tolerate its absence |
| `green_sardine_sdma.bin` | `sdma_v4_0` initialization, SDMA 4.1.2 prefix rule | SDMA firmware candidate for copy/fence bring-up |
| `green_sardine_asd.bin`, `green_sardine_ta.bin` | `psp_v12_0_init_microcode`, MP0 12.0.1 prefix | Both initializer errors propagate in this path; exact trusted-application services and payload availability require inspection |
| `green_sardine_vcn.bin` | UVD_HWIP 2.2.0 prefix rule | Media candidate, deferred; not an initial rendering requirement |

The roles and conditionals come from pinned
[GC firmware initialization](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/amd/amdgpu/gfx_v9_0.c),
[SDMA](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/amd/amdgpu/sdma_v4_0.c),
[PSP12 initialization](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/amd/amdgpu/psp_v12_0.c)
and [PSP firmware parsing](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/amd/amdgpu/amdgpu_psp.c).
MODULE_FIRMWARE declarations alone do not prove a file is mandatory for every
device. This is not a complete boot image set: PSP system/secure-OS boot state,
SMU residency and optional payloads remain unresolved.

This study downloaded no firmware. The later
[provenance study](firmware-provenance.md) pins linux-firmware `20260916` and
records the license, size, SHA-256 and documented header/feature versions and
payload bounds for each candidate. It adds `green_sardine_dmcub.bin`, which display
selects only through a hardware-revision rule. Signature acceptance and the host's
actual IP versions remain **unavailable**; do not fill them from file headers or
source filenames. Hash validation and host-side header checks
do not substitute for security-processor acceptance of signed images.

Before acquisition/use, record an exact AMD firmware source revision and license
for each image; then review redistribution terms. Before loading, validate
format/bounds and establish the permitted signed-image path. Successful loading,
rejection handling and recovery require experimental hardware evidence.

## Reproduce and advance

Source inputs are the linked Linux v6.12 files, not the moving default branch.
Fetch to a new ignored directory, preserve download failures, record URL/revision
and SHA-256, then inspect these symbols:

```text
amdgpu_drv.c: amdgpu PCI ID table, 0x1638
amdgpu_device.c: amdgpu_device_init_apu_flags
amdgpu_discovery.c: amdgpu_discovery_set_ip_blocks and *_set_*_ip_blocks
amdgpu_ucode.c: amdgpu_ucode_ip_version_decode
gfx_v9_0.c: gfx_v9_0_init_microcode and CP/RLC helpers
sdma_v4_0.c: firmware declaration and initialization helpers
psp_v12_0.c: psp_v12_0_init_microcode
amdgpu_psp.c: psp_init_asd_microcode, psp_init_ta_microcode
```

Local source bytes and digests are in ignored `out/metal-loader-study/source-index.json`;
the earlier study files remain under `out/references/`. They are reference
material, not incorporated driver code. Review per-file licensing before reuse.

| Next experiment | Scope | Success criterion |
| --- | --- | --- |
| Firmware provenance/header specification | Done: [provenance study](firmware-provenance.md) | Exact release/license for each image, bounded parser and required/optional decisions recorded |
| Compiler target mapping | Offline primary-source study | Establish Cezanne-to-compiler mapping independently of a product string; verify emitted ISA/resource metadata before selecting `gfx90c` |
| Target IP validation | Deferred to experimental boot | Checked discovery/IP identity and instance counts agree with selected handlers; unsupported versions stop initialization |
| Firmware and copy/fence | Deferred to own PCI ownership and recovery | Signed image acceptance, bounded rejection behavior, guarded byte-for-byte DMA copy and reliable fence/interrupt completion |

The loader feasibility investigation remains the earlier priority; see
[Metal loader study](metal-loader-study.md). This manifest does not authorize
hardware access or claim bring-up success.
