# Green Sardine firmware provenance and header validation

The Green Sardine microcode candidates are now pinned to an exact upstream
release. They were downloaded from kernel.org and checked against the official
GitLab mirror's Git blob IDs, and their documented headers were validated offline
by a new bounded parser. All eleven images pass, and every region Linux consumes
lies inside its file. **Header validity is not signature acceptance. The images'
version fields are file metadata, not measurements of this host's GPU.** No
firmware was loaded, sent to hardware, tracked in Git or interpreted beyond its
documented header fields.

This advances the conditional inventory in the
[target manifest](cezanne-target-manifest.md), which identified the candidate
names from Linux v6.12 but recorded every artifact field as unavailable.

## Experiment record

Offline study on 2026-10-02. The host was not queried for this task; its identity
remains PCI `1002:1638:c9` from the [baseline](hardware-baseline.md).

| Item | Value |
| --- | --- |
| Firmware release | [linux-firmware](https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git) tag `20260916`, commit `ab23307cfe7f9366c819025ca3e4778299bc2db2`, the newest tag on 2026-10-02 |
| Download route | kernel.org cgit `plain/<path>?id=<commit>` |
| Verification route | [GitLab mirror](https://gitlab.com/kernel-firmware/linux-firmware) partial fetch of the same tag; it resolves to the same commit, and `git ls-tree` blob IDs equal the Git blob hashes of all 13 downloaded files |
| Layout and consumer source | Linux [v6.12](https://github.com/torvalds/linux/tree/v6.12/drivers/gpu/drm/amd) `amdgpu_ucode.h`, `amdgpu_ucode.c`, `amdgpu_rlc.c`, `amdgpu_psp.c`, `psp_v12_0.c`, `gfx_v9_0.c`, `sdma_v4_0.c`, `soc15.c`, display `amdgpu_dm.c` and `dal_asic_id.h` |
| Tools | `tools/amdgpu_firmware.py` (Python 3, standard library), `git`, `curl` |

Ignored `out/firmware-provenance/` keeps the firmware, license, `WHENCE`, sources,
the partial clone, an artifact index and every command's arguments, UTC time,
stdout/stderr and exit status. Preserved failures:
- `LICENSE.amdgpu` returned HTTP 404 at the repository root; this release keeps
  it at `LICENSES/LICENSE.amdgpu`.
- A kernel.org partial fetch timed out after 120 seconds because that server
  ignores `--filter=blob:none`.
- One mutant in the first parser mutation check was equivalent to the original
  code, so it survived; it was replaced with a real mutation.
- The first documented-reproduction run verified only two blobs because `git
  ls-tree` treats `*` literally; the command now lists every path.

## Release, license and project constraints

`WHENCE` lists eleven `amdgpu/green_sardine_*.bin` files under the amdgpu driver
entry, with "Licence: Redistributable. See LICENSE.amdgpu for details." It names
`dmcub`, which the manifest's inventory omitted.

`LICENSES/LICENSE.amdgpu` (SHA-256 `572872598565dc35…`) grants free
redistribution **in binary form only**, if the copyright notice, permission
notice and disclaimers are reproduced. It **prohibits reverse engineering,
decompilation and disassembly**, disclaims warranties, caps liability and adds a
US export-control notice. The project therefore adopts these rules:

| Rule | Consequence |
| --- | --- |
| Firmware bytes are not tracked in this repository | Tracked files record release, path, size, SHA-256 and Git blob only |
| Payloads are opaque | The parser reads only header fields Linux defines and checks byte ranges. It never decodes, disassembles or patches payload contents. |
| A future installer or package redistributes the files unmodified with `LICENSE.amdgpu` | Packaging must carry the notice; any modified image would fall outside this grant |
| Export notice | Distribution decisions need review outside this technical study |

This is a technical reading of the license, not legal advice. Review by the
project owner is required before any distribution.

## Pinned artifacts

All files have mode `100644` in the pinned tree. Header and IP fields come from
each file's documented common header; they describe the file, not host hardware.

| File | Bytes | SHA-256 | Header | File IP field | `ucode_version` | Extension versions |
| --- | --- | --- | --- | --- | --- | --- |
| `green_sardine_ce.bin` | 36,608 | `3bba10cff48ec51eb63e78078456596258c9fea2e8b38d91d02be66c0aca70fd` | gfx 1.0 | 9.3 | `0x50` | feature 54 |
| `green_sardine_pfp.bin` | 85,760 | `94e1474b3c8d422e35661d2d584297d891464c173ebf4ad45536277bdf7bdcdc` | gfx 1.0 | 9.3 | `0xc5` | feature 54 |
| `green_sardine_me.bin` | 69,376 | `671af908c8d631e659895d62db2e6bffc1d2c87d6db4c08246be0216f468fb9c` | gfx 1.0 | 9.3 | `0xa7` | feature 54 |
| `green_sardine_mec.bin` | 268,224 | `0e4c6712f75af494c3fd70386bc07739cab3aafd77c4fe8efb46c0bc1ae4eba6` | gfx 1.0 | 9.3 | `0x1e3` | feature 54 |
| `green_sardine_mec2.bin` | 268,224 | identical to `mec` | gfx 1.0 | 9.3 | `0x1e3` | feature 54 |
| `green_sardine_rlc.bin` | 39,928 | `66f4397c2e9d19af245f9da290c8678eb1a51cc815fa7864a9383c2cd0db319d` | rlc 2.1 | 9.3 | `0x3c` | feature 1; save/restore lists version 1, feature 1 |
| `green_sardine_sdma.bin` | 17,408 | `cba8658ea950a99115ca46ee88c9622240632a0ca1731abbcd18b6d9be9e09de` | sdma 1.0 | 4.1 | `0x28` | feature 41 |
| `green_sardine_asd.bin` | 209,408 | `9c9307b1e87f25775c540f984b47baedbd7cdd1f5e240c7c21724d6810ac2f49` | psp 1.0 | 12.0 | `0x21000118` | SOS descriptor version 0 |
| `green_sardine_ta.bin` | 37,632 | `53ad33b376b85b90f858c8cc3b4bc6c709135b4bd37340ed69b1f00fecd6626b` | ta 1.0 | 10.0 | `0x0` | HDCP `0x17000053`, DTM `0x1200001f`; XGMI, RAS and secure display empty |
| `green_sardine_dmcub.bin` | 121,608 | `1c9d74c58dd14dacd6d4a330dcf9820b2263d6a7d39fa54c826636a797b4b6a8` | dmcub 1.0 | 3.0 | `0x0101002b` | instruction/constant 121,352 bytes; BSS 0 |
| `green_sardine_vcn.bin` | 405,952 | `ff1cbd575ae59ee317c6027ba57be7fc8861ac51fd6da44411ec8a1bbdfc7f26` | common 1.0 | 2.2 | `0x0811800d` | none |

Every header size equals its v6.12 structure size, and every payload starts at
byte 256. The file IP fields for the graphics, SDMA, ASD and VCN images agree with
the manifest's conditional GC 9.3.0, SDMA 4.1.2, MP0 12.0.1 and UVD 2.2.0 branches.
They do not verify those versions on the host. The TA field (10.0) and DMCUB field
(3.0) are not host block versions in the inspected source and are not interpreted.

## Header and bounds rules

[`amdgpu_ucode.h`](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/amd/amdgpu/amdgpu_ucode.h#L28)
defines a 32-byte little-endian common header followed by type-specific fields.
Linux's generic check
([`amdgpu_ucode_validate`](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/amd/amdgpu/amdgpu_ucode.c#L508))
only compares `size_bytes` with the file size. Consumers then trust header offsets
without bounds checks. The project parser keeps that check and adds bounds:

| Rule | Basis |
| --- | --- |
| `size_bytes` equals file size | Linux generic check |
| Kind and header version must have a known layout; header fits that layout and ends at or before the payload offset | v6.12 structures; stricter than Linux |
| Payload is non-empty and inside the file; no range wraps 32-bit arithmetic | Stricter than Linux; protects a future C implementation |
| gfx jump table `[ucode + 4·jt_offset, +4·jt_size)` inside the payload | Dword offsets as consumed for MEC ([`amdgpu_ucode.c` 879–884](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/amd/amdgpu/amdgpu_ucode.c#L879), [`gfx_v9_0.c` 3432–3435](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/amd/amdgpu/gfx_v9_0.c#L3432)) |
| RLC register lists (v2.0) and save/restore lists (v2.1) are byte ranges from file start, inside the file, after the header, with whole-dword sizes | [`amdgpu_rlc.c` 281–391](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/amd/amdgpu/amdgpu_rlc.c#L281); Linux silently truncates partial dwords |
| TA v1 regions use Linux's start rules: XGMI and HDCP start at the payload; RAS follows XGMI and DTM/secure display follow HDCP by their offsets; each lies inside the payload | [`parse_ta_v1_microcode`](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/amd/amdgpu/amdgpu_psp.c#L3587); the XGMI/HDCP offset fields are unused there |
| DMCUB instruction/constant region starts at the payload, includes 0x100-byte PSP header and footer, and is followed by any BSS data, all inside the payload | [`amdgpu_dm.c` 1199–1213 and 2449–2460](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/amd/display/amdgpu_dm/amdgpu_dm.c#L1199) |
| `crc32` field compared with a standard CRC-32 of the payload, reported but **not enforced** | Linux does not check it; algorithm and coverage are unspecified |

SDMA jump-table fields and the PSP descriptor's offset/size are reported, not
range-checked, because no inspected v6.12 consumer uses them as byte ranges.

Results for the pinned release:

| Image | Checked regions |
| --- | --- |
| CE, PFP, ME | Jump tables of 96 dwords inside their payloads |
| MEC, MEC2 | 224-dword jump table ending exactly at the file end |
| RLC | Payload 16,896 bytes, then five lists (format, restore, and save/restore control, GPM, SRM) tiling the rest of the file without gaps |
| TA | HDCP and DTM regions exactly tile the payload; the other descriptors are empty |
| DMCUB | Instruction/constant region covers the whole payload; no BSS data |
| ASD, SDMA, VCN | Common payload only |

The CRC field matches a standard CRC-32 of the payload only for VCN. For the
others, it is unknown whether a different algorithm, a different coverage or a
stale value is involved. Integrity rests on the pinned Git blobs and SHA-256 values,
not on that field.

## Selection and required/optional decisions

| Image | Linux v6.12 selection and use for this identity | Project decision |
| --- | --- | --- |
| CE, PFP, ME | GC 9.3.0 graphics rings | Required for graphics-ring bring-up, not for the first SDMA copy/fence |
| RLC | GC initialization | Required with GC bring-up |
| MEC | GC compute initialization | Required for the compute/shader milestone |
| MEC2 | Not requested for GC 9.3.0 ([`gfx_v9_0_load_mec2_fw_bin_support`](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/amd/amdgpu/gfx_v9_0.c#L1504)) | Not needed; the release's file is byte-identical to MEC |
| SDMA | SDMA 4.1.2 | Required for the first copy/fence experiment |
| ASD, TA | [`psp_v12_0_init_microcode`](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/amd/amdgpu/psp_v12_0.c#L48) requests both; errors propagate; secure display is cleared for non-Renoir | Treat as required if following the Linux PSP path; services and need remain an open security-processor question |
| DMCUB | DCE 2.1.0 selects it only when `ASICREV_IS_GREEN_SARDINE(external_rev_id)`; for GC 9.3.0 without the Renoir flag, `external_rev_id = rev_id + 0xa1` ([`soc15.c` 1128–1134](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/amd/amdgpu/soc15.c#L1128), [`dal_asic_id.h` 210–212](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/amd/display/include/dal_asic_id.h#L210)) | Display milestone; choice depends on a hardware-reported `rev_id` below `0x5E`, not the PCI revision. Otherwise Renoir's DMCUB is selected |
| VCN | UVD 2.2.0 prefix rule | Deferred |

This refines the manifest. The PCI-ID APU flag selects the Green Sardine prefix
for GC, SDMA and PSP, but display firmware needs an additional hardware revision
value that read-only host evidence does not supply. No security-processor
operating-system image is requested for this APU in the inspected PSP v12 path.
Whether platform firmware already runs it is a boot-state question, not settled
by these files.

## Reproduction

Use a new ignored directory `D` and run each command through `tools/baseline.py`'s
`capture` helper. The firmware remains in `D`.

```sh
id=ab23307cfe7f9366c819025ca3e4778299bc2db2
base=https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain
names="asd ce dmcub me mec mec2 pfp rlc sdma ta vcn"
curl -fsSL -o D/WHENCE "$base/WHENCE?id=$id"
curl -fsSL -o D/LICENSE.amdgpu "$base/LICENSES/LICENSE.amdgpu?id=$id"
for n in $names; do
  curl -fsSL -o D/green_sardine_$n.bin "$base/amdgpu/green_sardine_$n.bin?id=$id"
done
git init D/repo
git -C D/repo remote add origin https://gitlab.com/kernel-firmware/linux-firmware.git
git -C D/repo fetch --depth 1 --filter=blob:none origin tag 20260916
git -C D/repo rev-parse '20260916^{commit}'   # must print the commit above
git -C D/repo ls-tree 20260916 -- WHENCE LICENSES/LICENSE.amdgpu \
  $(for n in $names; do echo amdgpu/green_sardine_$n.bin; done)
python3 tools/amdgpu_firmware.py D/green_sardine_*.bin
```

Compare each `ls-tree` blob ID with `git hash-object` of the downloaded file and
each SHA-256 with the table above; `ls-tree` paths are literal, so all thirteen
must be listed. The parser must accept all eleven files and
report the regions above. A newer release may change these values; record it
as a new release rather than editing these digests.

`tests/test_amdgpu_firmware.py` uses synthetic headers only. It covers acceptance,
every rejection rule, filename-based kind selection, CLI exit codes and the
non-enforced CRC observation. Disabling any of ten parser rules in a scratch copy
makes the suite fail.

## Verification record

All 26 tests pass (13 existing, 13 new). Both download routes agree for all 13
files, and the parser accepts the 11 images. All ten parser-rule mutants are
detected. Links and whitespace were checked. No firmware was loaded or sent to
the GPU, no payload was decoded, and no GPU register or PCI configuration access
occurred.

## Remaining questions and gates

| Open item | Next evidence |
| --- | --- |
| Signed-image acceptance and PSP load path | Experimental boot only: accepted/rejected images, bounded rejection and recovery |
| Hardware `rev_id` for display firmware choice | Read through an owned device in an experimental environment; validate against `0x5E` |
| CRC field semantics | Primary AMD documentation, if any; do not infer from payload contents |
| ASD/TA services required by our stack | PSP interface study; decide before display/HDCP work |
| Release updates | Re-run the reproduction for a new tag and record differences |

| Gate | Measurable success criterion |
| --- | --- |
| Artifact identity | Loader refuses any image whose SHA-256 is not in a reviewed manifest for the running release |
| Header bounds | The in-driver parser applies these rules before any copy; fuzzed headers never read or copy outside the file |
| Redistribution | Any package carries unmodified images with `LICENSE.amdgpu` and its notices |
| Selection | Display firmware choice uses the hardware revision rule and fails closed when it is unavailable |
