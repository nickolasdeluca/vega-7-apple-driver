# Project handoff

Read [AGENTS.md](../AGENTS.md) first. This is the tracked entry point for resuming
work without the previous conversation. The accepted scope and full milestone
sequence are in [discovery-plan.md](discovery-plan.md).

## Current checkpoint

Current checkpoint, 2026-10-06, branch `cezanne-discovery`: **driver stages
0–17 succeeded on the USB test EFI** (boots 1–25; see the
[test boot log](test-boot.md#test-boot-log)). The driver can do the
following:
- read the GPU, the discovery table and the engine state;
- talk to the SMU (version, GFXOFF, metrics, SDMA power);
- run PSP ring commands (TMR setup, firmware load);
- load SDMA0 firmware;
- run a verified SDMA copy and fence (boot 21);
- read the display, MMHUB VM and IH state (boot 22);
- enable the MMHUB GART and IH ring 0, copy through the GART and receive the
  SDMA0 trap, then restore (boot 25).

Stage 17 (GART and the IH ring) succeeded in boot 25: an SDMA copy read
through a driver-built GART page table, its trap arrived in IH ring 0, and
everything was restored. Stage 18 (MSI interrupt delivery) delivered the
trap as an MSI in boot 26; the verify's count was fixed and rebuilt for boot
27; see "Next task" below. The sections that follow are the earlier discovery record and
still apply.

Completed work:

- Root operating rules, read-only baseline collector, public Metal inventory,
  parsing/failure tests and independently compared host baseline:
  [README](../README.md), [baseline](hardware-baseline.md).
- Hardware-block map and source-backed IP/firmware selection, with actual host
  block revisions and firmware artifacts still unknown:
  [hardware](cezanne-hardware.md), [target manifest](cezanne-target-manifest.md).
- Metal loader/class construction, path-trust and application-policy gates:
  [admission](metal-admission.md), [requirements](metal-admission-requirements.md).
- Partial IOAccel connection/selector/capacity inventory and configuration,
  allocation address/size/ID, dirty-ring and event/mapping consumer data flow:
  [ABI](ioaccel-abi.md), [shared-memory fields](ioaccel-shared-memory.md).
- Notification callback/refcon registration, partial callback payload consumers,
  two queue retains per submission entry versus one release per callback,
  asynchronous notification-port cancellation and context finalization:
  [lifecycle](ioaccel-lifecycle.md). The earlier automated safety-check stop did
  not recur during ordinary read-only own-child inspection. A loaded-module
  symbol/trampoline search found no explicit unmap symbol; this does not prove
  absence of indirect or kernel cleanup. Earlier claims of guaranteed mapping
  survival and immediate close were corrected, along with selector 5's output
  capacity (8 bytes).
- Metal submission producer fields, separate scheduling/completion block copies,
  their object captures and local transport-failure cleanup:
  [block ownership](ioaccel-block-ownership.md). These provide an ownership graph
  for inspected normal/error paths. Exactly one kernel delivery per distinct
  copy would balance the normal path. The subsequent family study below maps
  static send attempts; delivery, callback cardinality and reset/reuse remain
  unverified.
- Completion predicates, reset state, synchronous-debug semaphore prerequisite
  and pooled/unpooled storage cleanup:
  [buffer reuse](ioaccel-buffer-reuse.md). The bounded Metal code scan validates
  only the two producer references to alias slots under supported encodings.
  Completion wait does not establish outer callback/storage drain. Indirect or
  vendor alias access/clearing and cross-thread reuse remain unresolved.
- Generic descriptor/map owners, explicit unmap, connection close/no-senders,
  client destruction and task address-space removal:
  [XNU mapping lifecycle](xnu-mapping-lifecycle.md). Client mapping sets and
  exported map ports have different ownership paths; extra references and a
  mapping-set port can delay destruction. Generic cleanup does not establish
  installed family reclamation, backing release or GPU request termination.
- Generic async registration references, wake-port acquisitions/releases,
  message packing, send limits and IOKitUser dispatch arity:
  [async replies](xnu-async-replies.md). Offline layout/field fixtures distinguish
  registration count from reply count, padding from argument data and Mach port
  ownership from retained blocks/queues. Send success does not establish delivery;
  installed generic-helper equivalence and runtime delivery remain open.
- Installed IOKit dispatch-queue callout, dispatcher, async wrappers and generated
  `io_connect_async_method` stub compared with pinned source/IDL:
  [installed dispatch](iokit-async-dispatch.md). All compared offsets, limits,
  constants and argument orders match. The callouts do not set up arguments the
  dispatcher ignores. The stub enforces client-only count limits and writes reply
  scalar counts into a shared static used for `NULL` output counts. The IOAccel
  registration leaves reference slot 0 unwritten and treats a zero wake port as
  a fatal assertion. The dispatcher enforces no argument minimum or maximum; the
  producer must supply the consumer's seven words.

- Installed IOAcceleratorFamily2 queue registration, reply producers and
  cancellation/teardown paths: [family replies](ioaccel-family-replies.md).
  Read-only inspection of the original kernel-collection files establishes
  seven argument words with callback result zero, a normal scheduling send and
  deferred completion object, and two immediate sends on per-entry error paths.
  Block fences retain the registration owner, whose destructor releases the
  async reference. Cancellation detaches the submitter without clearing the
  block/port or sending a reply. These are static base-family paths; vendor
  overrides, kernel validation, delivery and hardware completion remain open.

- Green Sardine firmware candidates pinned to linux-firmware `20260916`
  (`ab23307…`), verified over kernel.org downloads and GitLab mirror blob IDs,
  with license constraints and a tested bounded header parser,
  `tools/amdgpu_firmware.py`: [firmware provenance](firmware-provenance.md). All
  eleven images pass; MEC2 is byte-identical to MEC and unused for GC 9.3.0.
  Display firmware choice depends on a hardware revision below `0x5E`. AMD's
  license allows binary-only redistribution with notices and forbids reverse
  engineering or disassembly, so payloads stay opaque and untracked. Signature
  acceptance and host IP versions remain unverified.
- Shader compiler target selected offline as `gfx90c`: [shader target](shader-target.md).
  Linux v6.12 KFD gives GC 9.3.0 `gfx_target_version` 90012; Mesa 26.2.4 uses
  `gfx909` for the same chip family. `gfx902`, `gfx909` and `gfx90c` resolve to
  identical LLVM feature sets and produced byte-identical code and descriptors
  for one fixed kernel, so the name changes only the ELF machine value. Use
  XNACK "any", no SRAM ECC, wave64; `gfx9-generic` (code object v6) is the
  fallback. The compiler is the official, attested LLVM 20.1.7 x86_64 archive
  under `out/` (23.1.2 has no x86_64 macOS build). A wave32 request silently
  emits no kernel. The host's GC version and any execution remain unverified.
- USB test boot prepared, not yet booted: [test boot](test-boot.md).
  `tools/test_efi.py build --stage N` derives a test EFI from a copy of the
  known-good OpenCore 1.0.7 EFI. It removes NootedRed and SMCRadeonSensors,
  adds `CezanneGPU.kext` and verbose boot arguments with `cezanne-stage=N`, and
  rejects an unauthorized stage, any other config or file difference, an NVRAM
  value the other boot would not reset, or a self-registering launcher. The
  release `ocvalidate` accepts both configs. Display behaviour without
  NootedRed, firmware boot-menu listing and the kext's acceptance are unknown
  until the first boot.
- Driver implementation started, built but never loaded:
  `driver/core/` is the freestanding hardware core (identity, bounded
  capability walk, D0/decode/BAR5 checks, two boot-state registers behind
  read-only callbacks). `driver/kext/` is the IOKit adapter, `CezanneGPU`,
  which replaced the passive probe. Stage 0 publishes registry identity. Stage
  1 reads configuration space, maps BAR5 read-only and uncached, and reads
  `MP0_SMN_C2PMSG_33` and `RCC_CONFIG_MEMSIZE`, the registers Linux v6.12 reads
  before its discovery code writes any register. Core unit tests run under
  ASan/UBSan with five rejected source mutants; kext tests check the source
  denylist and read-only mapping, direct call targets and symbol resolution.

These are observations and static consumer expectations. Independent bundle
admission, a complete negotiated kernel ABI, mapping protection/ownership,
concurrency, GPU execution and desktop presentation remain unverified. An
authorized third-party Metal loading route has not been established. Apple AMD
binaries are observation references and remain excluded from the finished stack.

## Next task: boot stage 18 again (boot 27)

**Resume here.** Boot 26 (2026-10-06) delivered SDMA0's trap as an MSI to the
kext's handler, but counted a second MSI that came before the trap (see the
boot 26 entry in the [test boot log](test-boot.md#test-boot-log)). The verify
now counts only MSIs after the submit and records each one's time. The
display test pattern follows as stage 19. Steps:

1. The user runs `tools/update_stick.sh 18` (the stick holds the first
   build), cold boots the stick, and runs
   `tools/capture_boot.sh boot-27-stage18 --gfxoff-disallow --ih-intr --psp-state`.
2. Read the output against "Stage 18 succeeds when" in
   [test-boot.md](test-boot.md) and record boot 27 in the log. Follow the
   [fix-within-a-stage rule](test-boot.md#fixing-defects-inside-a-stage)
   for defects. A kernel panic is a finding: power off, then read the panic
   log from the next normal boot.

**Builds:** `out/test-efi/usb-stage18` (`usb-stage18-build.json`),
`out/test-efi/driver`, `out/diag` (rebuilt after boot 26; the first stage 18
builds are kept as `superseded-*-stage18-count`); the stage 17 builds are kept as
`superseded-driver-stage17` and `superseded-diag-stage17`.

### Earlier: booting stage 17

Boot 24 stopped at the precondition check on live `HUBP0_DCHUBP_CNTL` status
bits, before any stage 17 write; the mask was widened (`964ecaa`) and boot 25
ran the stage with `tools/capture_boot.sh boot-25-stage17 --gfxoff-disallow
--gart-ih --psp-state`.

**Builds:**
- `out/test-efi/usb-stage17` (rebuilt after boot 24; its `manifest.json`
  is the build record);
- `out/test-efi/driver`;
- `out/diag`;
- the first stage 17 builds, kept as `superseded-driver-stage17-hubp`,
  `superseded-diag-stage17-hubp` and `superseded-usb-stage17-hubp`;
- the stage 16 builds, kept as `superseded-driver-stage16` and
  `superseded-diag-stage16`.

**What the implementation decided** (details in the
[stage 17 section](test-boot.md#stage-17-gart-and-the-interrupt-ring-proposal),
"Implementation"):
- Selectors 26–29 (check, enable, verify, restore); frame 2 goes through
  selector 23. Interface version 13.
- **The restore** runs by progress. The stage 15 stop runs it first if
  needed, so an abandoned connection is covered too.
- **The semaphore** is readable only through the stage 17 register page set,
  and only by `flushGart`.
- **Engine 17's address range was never measured;** the check expects 0/0.
  If either reads otherwise, it stops before any write: a finding.
- **The restore check reports `IH_RB_WPTR`** instead of requiring it (bit 19
  is the IH's own `RB_MAY_OVERFLOW` status).

### Earlier: proposing stage 17

Stage 16 succeeded in boot 22. The display, MMHUB VM and IH/NBIO values are
measured (see the [log](test-boot.md#test-boot-log)).
- **Display:** pipe 0 shows 1080p60 from the GOP framebuffer at
  `0xF400000000`.
- **VM:** context 0 is unprogrammed.
- **IH:** ring 0 is off.

`VM_INVALIDATE_ENG17_SEM` turned out to acquire on read. It is removed from
the list, and stage 17 must use Linux's acquire and release protocol.

### Earlier: booting stage 16

Stage 16 (a read-only inventory: display, MMHUB VM and IH/NBIO, 92
registers) is approved and built: `out/test-efi/usb-stage16`,
`--inventory16`. The user chose to measure all three areas first and then
write GART and IH with pinned values in stage 17.

### Earlier: choosing the next milestone

**Stages 0–15 succeeded. Boot 21 completed the "verified DMA copy and
fence" milestone.**
- SDMA0 ran PSP-loaded firmware and executed a driver-built ring: the ring
  test wrote `0xDEADBEEF`, then a 4 KiB copy matched word for word, and
  fence 1 arrived.
- No unexpected memory changed, and the engine halted and powered down
  cleanly.

Candidates for the next proposal (AGENTS.md "Future stages"):
- **Interrupts:** the IH ring, so fences can signal instead of being
  polled. The deferred `TRAP` and the IH reroute question from boot 15
  belong here.
- **GART and the default page:** VMID 0 page tables, so engines can reach
  system memory and the boot 14 zero default page is replaced by a scratch
  page.
- **Display:** one connector and mode with a test pattern, toward the
  display milestone; DCN 2.1 state first, read-only.

Each needs its own proposal.

### Earlier: retesting stage 15

Boot 20 showed the following:
- SDMA0 started with every exact value and stopped cleanly.
- The ring test timed out, with `GFX_RB_WPTR` reading 0 after a write
  of 1024: `submitSdma` omitted the `GFX_RB_WPTR_HI` write that Linux's
  `sdma_v4_0_ring_set_wptr` makes after the low dword.

[Fixed](test-boot.md#revision-stage-15-write-pointer-commit) and rebuilt
(`out/test-efi/usb-stage15`). Next: `tools/update_stick.sh 15` and a cold
boot. Defects found while testing an approved stage are fixed directly
and documented. Only new stages need a proposal
([rule](test-boot.md#fixing-defects-inside-a-stage)).

### Earlier: booting stage 15

Stage 15 is approved and built: `out/test-efi/usb-stage15`, `--sdma-copy`.
It applies the golden `GB_ADDR_CONFIG` and leaves the default page for
later. Next: a cold boot after a Time Machine backup, then record the
result.

### Earlier: proposing stage 15

Stages 0–14 succeeded. Boot 19:
- `PowerUpSdma`/`PowerDownSdma` answered `0x01`;
- all 31 SDMA registers are measured and identical across the power
  messages;
- the golden-setting results are computed (see the
  [boot 19 log](test-boot.md#test-boot-log)).

Next is the stage 15 proposal: golden settings plus the `sdma_v4_0_start`
writes with exact values computed from boot 19, no doorbell, the ring test,
then a 4 KiB `COPY_LINEAR` and a `FENCE` at carveout `0x40300000`, verified,
then halt, power down and tear down. It must decide whether to apply the
`GB_ADDR_CONFIG` golden value, and how to handle the zero default page.

### Earlier: planning the first copy

Stages 0–13 succeeded. In boot 18 the PSP accepted the pinned SDMA0 image:
- `LOAD_IP_FW` fenced 2 with status 0;
- `SDMA0_UCODE_CHECKSUM` went from 0 to `0x25a1ba79`;
- SDMA0 stayed halted;
- the teardown fenced 3.

`SDMA0_CLK_CTRL` lost `SOFT_OVERRIDE2` during the load. The next proposal
is the first verified DMA copy:
- keep the TMR and firmware (no teardown before the copy);
- program one SDMA0 ring (gfx queue) in the carveout, as `sdma_v4_0_gfx_resume`
  does;
- unhalt it (`sdma_v4_0_enable`);
- submit a copy packet between two checked carveout pages and a fence
  write;
- verify the destination and the fence, then halt it again.

Prerequisites to source:
- `sdma_v4_0` ring registers and packet formats (`vega10_sdma_pkt_open.h`);
- the address translation SDMA uses: VMID 0 and the system aperture, with
  the default-page finding from boot 14;
- the doorbell or write-pointer path.

### Earlier: booting stage 13

Stage 13 (copy the pinned SDMA0 image into a firmware buffer and load it
through `LOAD_IP_FW` between `SETUP_TMR` and `DESTROY_TMR`, SDMA0 left
halted) is approved and built: `out/test-efi/usb-stage13`,
`out/diag/cezanne-diag --psp-sdma`. The kext now embeds the firmware at
build time from ignored `out/` (SHA-256 pinned in `driver/kext/build.sh`).
Next: a cold boot after a Time Machine backup, then record the result. If
the PSP accepts the image, the next proposal is starting SDMA0: unhalt, ring
and a first copy with a fence, the "verified DMA copy" milestone.

### Earlier: proposing stage 13

Stages 0–12 succeeded. In boot 17, `SETUP_TMR` (TMR at GPU `0xF440400000`,
4 MiB) and `DESTROY_TMR` went through the PSP ring as frames 0 and 1. Both
fenced with status 0, and no unexpected memory changed. The next proposal
is `GFX_CMD_ID_LOAD_IP_FW` for SDMA0, after `SETUP_TMR`, as
`psp_load_non_psp_fw` does for Green Sardine. It needs:
- the pinned `green_sardine_sdma.bin`, its header parsed with
  `tools/amdgpu_firmware.py`, copied into a 1 MiB firmware buffer
  (`fw_pri`);
- the `psp_gfx_cmd_load_ip_fw` layout and `GFX_FW_TYPE_SDMA0`;
- the SDMA register readback that shows the firmware is running.

### Earlier: booting stage 12

Stage 12 (the first ring frame: `SETUP_TMR` through the ring, then
`DESTROY_TMR` and the ring destroy) is approved and built:
`out/test-efi/usb-stage12`, `out/diag/cezanne-diag --psp-tmr`. Next: a cold
boot after a Time Machine backup, then record the result. If it succeeds,
the next proposal is the first firmware load (`LOAD_IP_FW`, SDMA0) into the
TMR.

### Earlier: proposing stage 12

Stages 0–11 succeeded. In boot 16 the PSP created (`0x80020000`) and
destroyed (`0x80030000`) a kernel-mode ring at GPU `0xF440100000`, writing
nothing to its memory. `C2PMSG_69`/`70`/`71` keep their values after the
destroy (they are arguments, not ring state). The next proposal is the
first frame submitted through the ring, the TMR setup (`psp_tmr_init`,
`psp_tmr_load`), which needs:
- a command buffer, a fence buffer and a TMR region at checked carveout
  pages;
- `psp_gfx_cmd_resp` layouts from `psp_gfx_if.h`;
- the write-pointer protocol (`C2PMSG_67`, `psp_ring_cmd_submit`).

### Earlier: the revised stage 11

Boot 15 stopped after the PSP answered `GBR_IH_SET` with
`PSP_ERR_UNKNOWN_COMMAND` (`0x80080100`; the command ID is echoed in bits
19:16). No ring was created. The approved
[revision](test-boot.md#revision-stage-11-psp-responses-proposal) masks
responses like Linux (`0x8000FFFF`), drops the reroute, and tracks the ring
from the create write. It is built as `out/test-efi/usb-stage11`, and the
first build is kept as `superseded-*-stage11-reroute`. Next: a cold boot
of it, then record the result. If the ring is created and destroyed
cleanly, the next proposal is the first ring frame (TMR setup).

### Earlier: stage 11 build

Stage 11 (IH reroute, then creating and destroying the PSP kernel-mode
ring at GPU `0xF440100000`, no frames) is approved and built:
`out/test-efi/usb-stage11`, `out/diag/cezanne-diag --psp-ring`. Next: the
user copies it to the stick and boots it, after a Time Machine backup;
record the boot in the test boot log. If it succeeds, the next proposal is
the first ring frame: the TMR setup command (`psp_tmr_init`,
`psp_tmr_load`), which needs a command buffer, a fence buffer and a TMR
region in the carveout.

### Earlier: stage 10

Stage 10 succeeded in boot 14. The PSP is ready (`C2PMSG_64`
`0x80000000`) with no ring after a cold boot. Both hubs map FB
`0xF400000000`–`0xF47FFFFFFF` to physical `0x5C0000000`. The system aperture
already has the Renoir +1 workaround. AGP is unused, and the default page is
0, which needs handling before engines access memory.

Next, write the stage 11 proposal in test-boot.md, following the stage 10
section's outline:
- the two `GBR_IH_SET` commands;
- create the kernel-mode ring in one checked carveout page, then destroy it;
- load no firmware.

It needs the user's approval before it is built.

Stages 0–10 succeeded (see the [test boot log](test-boot.md#test-boot-log)).

- **The metrics table.** Stage 9 had the SMU write its 148-byte metrics table
  to a checked carveout page, giving live clocks, power and temperatures:
  GFX idle at 400 MHz and about 44 °C, CPU at 4.65 GHz all-core.
- **What it proves.** The GPU-address-to-physical translation for the
  carveout is correct; it is the first GPU-side write to memory.
- **Open finding.** One stage 7 boot attempt reset early.
- **Approval.** Further steps each need a proposal in test-boot.md and the
  user's approval.

## Offline task while waiting: Metal shader frontend boundary

Continue offline work before driver bring-up:

1. Read the [shader target study](shader-target.md), the shader rows of the
   [graphics contract](graphics-contract.md) and the
   [loader study](metal-loader-study.md). Use a new ignored output directory.
2. Determine what Apple's permitted, public Metal toolchain emits for one fixed
   MSL compute kernel. `xcrun -f metal` resolves in the selected Xcode, but
   `metallib` does not, and whether the compiler runs without a separate
   toolchain component is untested. Record the tool versions, stdout/stderr and
   exit status; if the compiler is unavailable, record that rather than
   installing anything without asking.
3. If it compiles, record the intermediate form's container and metadata (for
   example AIR's LLVM bitcode version, target triple, kernel argument and
   resource-binding metadata, intrinsic names) using public tools only. Note any
   license terms that limit inspection or reuse. Do not create a device library
   through the AMD plugin, dispatch anything, or translate AIR yet.

Success means a sourced statement of what a permitted frontend exposes and what
an independent backend would still lack, or an explicit list of what blocks the
question. Readable intermediate output does not establish that Metal would
accept an independent backend.

## Completed parked task: family queue reply production

Completed 2026-10-05: [the family reply study](ioaccel-family-replies.md) records
the requested producer/consumer map, argument count and fields, per-entry send
attempts, registration storage/release, and error/cancellation/teardown paths.
An offline LLVM utility reads the original fileset entry without extracting or
rewriting a collection. The original `out/ioaccel-family-replies/` captures remain
preserved; new evidence and a fresh documented reproduction are listed below.
No kernel debugger, private calls, callbacks, messages or GPU work were used.

Further queue-contract studies should examine the installed generic helper and
kernel MIG validation/error cleanup, vendor overrides and reference-word
initialization. Runtime delivery, ordering, cancellation drain, GPU completion,
installed mapping reclamation, vendor/indirect alias access, cross-thread reuse,
libdispatch receive handling and complete commit dispatch remain separate open
interfaces. The USB and offline frontend tasks above retain their scope.

Parallel areas of future investigation, when relevant: primary vendor admission
contracts and the PSP/ASD/TA service questions in the firmware study. Keep those separate from a focused lifecycle batch. Driver
loading, PCI ownership, register access or GPU takeover happen only through the
staged USB test boot in [test-boot.md](test-boot.md); stages 0 and 1 are
authorized, in that order.

## Evidence and reproduction

Saved evidence on this workspace is ignored and contains sensitive/raw details:

| Local location | Contents |
| --- | --- |
| `out/baseline-verified-20261002/report.json` | Normalized baseline and per-query evidence references |
| `out/independent-20261002/` | Independent PCI/display/Metal comparisons |
| `out/metal-abi-study/` | Loader/trust/connection/queue/finalizer captures and source indexes |
| `out/metal-abi-verified/` | Verified public-child, SDK-layout and root-inspection examples |
| `out/metal-contract-detail/` | Field/ring/mapping and policy captures, source digests and `experiment-index.json` |
| `out/ioaccel-lifecycle-detail/` | Queue/callback/notification/context disassembly, module symbols, primary sources, host queries and verification index |
| `out/ioaccel-block-ownership/` | Metal block producers/callers, descriptor/helper reads, completion/deallocation, preserved lookup failures, host queries, primary references and verification index |
| `out/ioaccel-buffer-reuse/` | Reset/wait/storage-pool disassembly, bounded alias scan, controlled bound rejection, SDK/primary contracts, host queries and verification index |
| `out/xnu-mapping-lifecycle/` | Pinned XNU/IOKitUser source and metadata, source/blob hashes, OS/kernel queries, preserved provenance failure and verification index |
| `out/xnu-mapping-reproduced/` | Fresh documented collection: 25 captured downloads and 21 verified source/header/license files |
| `out/xnu-async-replies/` | OS/kernel/SDK queries, offline layout build and bounded fixture checks, supplementary source reads, preserved lookup/web failures and verification index |
| `out/xnu-async-sources/`, `out/xnu-async-reproduced/` | Initial and final pinned source collection; final collector captured 28 downloads and verified 24 source/header/license files |
| `out/iokit-async-dispatch/` | OS/kernel/SDK queries, probe build, IOKit symbol lookups, readable and normalized disassembly, layout build, IDL routine IDs, checker and mutation results, preserved inspection-code failures, kernel-collection availability and verification index |
| `out/ioaccel-family-replies-20261005/` | Read-only collection inspection, family/kernel symbols, instructions, normalized calls, metadata/digests, synthetic fixtures, independent byte/linkage/schema checks, preserved failures and verification index |
| `out/ioaccel-family-replies-reproduced-20261005/` | Fresh reader build and documented captures, identical symbol/instruction/normalized outputs, four fixture cases and eight independent check groups |
| `out/firmware-provenance/` | Pinned firmware, `WHENCE`, license, Linux v6.12 sources, GitLab partial clone, artifact index, parser output, tool mutation check, test run and preserved failures; firmware is never tracked |
| `out/firmware-provenance-reproduced/` | Fresh run of the documented download, blob verification and parser commands |
| `out/shader-target/` | Verified LLVM 20.1.7 archive, attestation and unpacked toolchain, pinned LLVM/Linux/Mesa sources, compile outputs, checker and mutation results, preserved failures |
| `out/shader-target-reproduced/` | Fresh run of the documented scripts against the verified toolchain: identical objects and checker summary |
| `out/iokit-async-reproduced/` | Fresh documented reproduction from extracted document code: separate launch, identical normalized instructions, 16 comparison groups, nine rejected mutations |
| `out/test-efi/` | Known-good EFI copy, verified OpenCore 1.0.7 DEBUG release and `ocvalidate`, built `driver/CezanneGPU.kext`, derived `usb-stage0/` and `usb-stage1/` test EFIs with `manifest.json`; the earlier probe build is kept as `superseded-probe*`; contains SMBIOS serials, never tracked |
| `out/discovery-progress.md` | Local execution ledger; supplementary to this tracked handoff |

A fresh clone will not contain `out/`. Tracked documents supply reproduction
instructions; the public metadata child's source is in
[metal-loader-study.md](metal-loader-study.md), and the debugger stopping/capture
procedure is in [metal-admission.md](metal-admission.md). Build artifacts and new
captures belong in a new ignored output directory. Record OS/build, architecture,
PCI/revision, command, UTC time, stdout/stderr and exit status. Preserve failures,
stop at denied inspection and avoid carrying addresses across launches. Normal
public enumeration may initialize the existing stack internally; that is not
manual private invocation or independent-driver proof.

## Verification record and commands

The family-reply study independently compared 4,705 symbol entries and all
357,364 text bytes with the original collection. Its eight check groups cover
branch-stub linkage and selected schema/ownership instructions. Four controlled
fixture cases passed, including missing-entry/truncated-header rejection. The
documented reader/scripts reproduced identical captures and normalized outputs
in a fresh directory. The initial repository test capture passed all 26 tests;
the final capture passed all 42, including the separately added test-boot tests. No installed
family code was invoked. Tool/build/checker failures and denied sysctls remain
preserved; details and runtime limits are in [the study](ioaccel-family-replies.md).

The driver batch added 10 core/kext tests (replacing the 7 probe tests) and two
stage tests for the test-EFI tool; all 47 tests pass. The real stage 0 and stage
1 test EFIs passed `ocvalidate`, verified as copies, share identical known-good
hashes and differ only in `cezanne-stage`; a stage 2 build was rejected. The
internal EFI matched the known-good hashes again. The compiled register read is
a single 32-bit load. Nothing was booted or loaded.

The USB test boot preparation ran the probe build and test-EFI tests (16 new),
built the real test EFI with `ocvalidate` exit 0, and confirmed the internal EFI
still matched the known-good copy. A planted unresolved symbol was missed by
`kmutil` itself but rejected by both build checks. Preserved failures: the
non-root read-only mount, SDK header warnings under `-I`, the IOPCIDevice
deprecation, `kmutil` rejecting a `/var`-symlinked path, and zsh's `log`
builtin. Nothing was booted or loaded.

The shader target continuation verified the LLVM archive by digest and offline
attestation. Its checker passed and rejected 15 controlled defects, and the
documented scripts reproduced 21 byte-identical objects in a fresh directory.
Preserved failures: `llc` aborts on target-ID `-mcpu` values, clang needs
`-nogpulib`, a missing descriptor symbol in linked objects, and two corrected
checker drafts. Nothing was loaded or dispatched; details are in
[the study](shader-target.md).

The firmware continuation ran all 26 tests successfully: 13 existing and 13 new
synthetic-fixture parser tests. All 13 pinned files matched across the kernel.org
download and GitLab mirror blob IDs, and the parser accepted all 11 images.
Disabling any of ten parser rules in a scratch copy made the tests fail. The
documented download, verification and parser commands were rerun into a fresh
directory with identical results. Preserved failures: the license's moved path
(HTTP 404), a kernel.org partial-fetch timeout, an equivalent mutant and a
documented `ls-tree` glob that matched literally. No firmware was loaded, sent
to the GPU or interpreted beyond its documented headers. Detailed records remain
in the local execution ledger. The previous installed-dispatch verification is
recorded in [its study](iokit-async-dispatch.md). Verify new changes before
claiming they pass.

```sh
python3 -m unittest discover -s tests -v
git diff --check
git status --short
```

For documentation-only changes, check links, attribution and factual claims
against evidence. For driver/tool changes, use meaningful failure-path tests and
reproduce the affected read-only commands. Verify and commit small focused
batches with the configured author and no agent co-author trailers. Keep raw
captures, machine identifiers and runtime addresses untracked. Update this file
as the next task and evidence limits change.
