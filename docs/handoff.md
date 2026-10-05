# Project handoff

Read [AGENTS.md](../AGENTS.md) first. This is the tracked entry point for resuming
work without the previous conversation. The accepted scope and full milestone
sequence are in [discovery-plan.md](discovery-plan.md).

## Current checkpoint

Current research checkpoint: Cezanne shader compiler target, 2026-10-05, on
branch `cezanne-discovery`, extending `4966bc5`. This handoff is committed
with the continuation; check Git history for its commit rather than assuming a
recorded hash is HEAD.
The project remains in **read-only discovery and specification**. No independent
hardware, display or acceleration driver has been implemented or loaded.

Initial target and observed host: PCI `1002:1638`, revision `c9`, reported Ryzen
5 5600GT, macOS 26.4.1 build 25E253. SDK observations used 26.5 on x86_64; do not
silently equate SDK definitions with the runtime's private kernel contract.
The kernel reports `xnu-12377.101.15~1/RELEASE_X86_64`; the generic source study
uses pinned public `xnu-12377.1.9`. Their implementation agreement is unverified.

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
  copy would balance the normal path, but actual message production, callback
  cardinality and reset/reuse remain unverified.
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
  actual private reply production remains open.
- Installed IOKit dispatch-queue callout, dispatcher, async wrappers and generated
  `io_connect_async_method` stub compared with pinned source/IDL:
  [installed dispatch](iokit-async-dispatch.md). All compared offsets, limits,
  constants and argument orders match. The callouts do not set up arguments the
  dispatcher ignores. The stub enforces client-only count limits and writes reply
  scalar counts into a shared static used for `NULL` output counts. The IOAccel
  registration leaves reference slot 0 unwritten and treats a zero wake port as
  a fatal assertion. The dispatcher enforces no argument minimum or maximum; the
  producer must supply the consumer's seven words.

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

These are observations and static consumer expectations. Independent bundle
admission, a complete negotiated kernel ABI, mapping protection/ownership,
concurrency, GPU execution and desktop presentation remain unverified. An
authorized third-party Metal loading route has not been established. Apple AMD
binaries are observation references and remain excluded from the finished stack.

## Next task: Metal shader frontend boundary

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

## Parked task: trace IOAcceleratorFamily2 queue reply production

Continue read-only graphics-contract work before driver bring-up:

1. Read the [installed dispatch comparison](iokit-async-dispatch.md) and the
   [async-reply study](xnu-async-replies.md); reuse the saved consumer, producer
   and dispatch evidence. Use a new ignored output directory.
2. Locate `com.apple.iokit.IOAcceleratorFamily2` inside the world-readable kernel
   collections with read-only file inspection tools. Its bundle on disk holds
   metadata only. Record tool versions, failures and symbol availability; stripped
   symbols are unavailable, not absent code. Use no tool mode that builds, loads,
   installs or rewrites collections or extensions.
3. Map the command queue's async-reference storage and `sendAsyncResult64`-family
   send sites. For each, record the argument count, the words written at the
   consumer's offsets 0/16/32/48, sends per submitted entry and the
   error/cancellation/teardown paths. Also map `releaseAsyncReference64` sites.
   Compare with the generic sender, the observed consumer and the gates. No kernel
   debugging, messages, callbacks, connections or GPU work.

Status 2026-10-02: parked by the user before any instructions were read; another
agent may resume it. Partial ignored evidence is in `out/ioaccel-family-replies/`:
tool versions (cctools-1040, Apple LLVM 21.0.0) and `otool -h`/`-l` captures of
both kernel collections. The family is the fileset entry
`com.apple.iokit.IOAcceleratorFamily2` in `SystemKernelExtensions.kc`, not the
boot collection. No tool for reading a single entry's instructions had been
settled on; confirm the approach with the user before continuing.

Success means an attributed static map from family send sites to consumer
expectations, or a documented limit if the fileset or symbols are unavailable.
It does not establish runtime delivery, ordering, cancellation, drain or GPU
completion. Installed mapping reclamation, vendor/indirect alias access,
cross-thread reuse, kernel MIG server validation, libdispatch receive handling
and complete commit dispatch remain separate open interfaces.

Parallel areas of future investigation, when relevant: primary vendor admission
contracts and the PSP/ASD/TA service questions in the firmware study. Keep those separate from a focused lifecycle batch. Driver
installation/loading, PCI ownership, register writes or GPU takeover require an
explicitly available experimental environment and recovery path; this handoff
does not authorize them.

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
| `out/firmware-provenance/` | Pinned firmware, `WHENCE`, license, Linux v6.12 sources, GitLab partial clone, artifact index, parser output, tool mutation check, test run and preserved failures; firmware is never tracked |
| `out/firmware-provenance-reproduced/` | Fresh run of the documented download, blob verification and parser commands |
| `out/shader-target/` | Verified LLVM 20.1.7 archive, attestation and unpacked toolchain, pinned LLVM/Linux/Mesa sources, compile outputs, checker and mutation results, preserved failures |
| `out/shader-target-reproduced/` | Fresh run of the documented scripts against the verified toolchain: identical objects and checker summary |
| `out/iokit-async-reproduced/` | Fresh documented reproduction from extracted document code: separate launch, identical normalized instructions, 16 comparison groups, nine rejected mutations |
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
against evidence. For probe/tool changes, use meaningful failure-path tests and
reproduce the affected read-only commands. Verify and commit small focused
batches with the configured author and no agent co-author trailers. Keep raw
captures, machine identifiers and runtime addresses untracked. Update this file
as the next task and evidence limits change.
