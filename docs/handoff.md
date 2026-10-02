# Project handoff

Read [AGENTS.md](../AGENTS.md) first. This is the tracked entry point for resuming
work without the previous conversation. The accepted scope and full milestone
sequence are in [discovery-plan.md](discovery-plan.md).

## Current checkpoint

Current research checkpoint: generic mapping and user-client teardown, 2026-10-02,
on branch `cezanne-discovery`, extending `3d33ece`. This handoff is committed
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

These are observations and static consumer expectations. Independent bundle
admission, a complete negotiated kernel ABI, mapping protection/ownership,
concurrency, GPU execution and desktop presentation remain unverified. An
authorized third-party Metal loading route has not been established. Apple AMD
binaries are observation references and remain excluded from the finished stack.

## Next task: trace generic async replies and wake-port ownership

Continue read-only graphics-contract work before driver bring-up:

1. Read the [lifecycle](ioaccel-lifecycle.md), [block ownership](ioaccel-block-ownership.md)
   and [generic mapping](xnu-mapping-lifecycle.md) studies. Reuse saved callback
   payload/refcon consumers and source provenance; avoid repeating inspection.
2. Trace generic XNU async-result construction and wake-port ownership in pinned
   primary source, with matching IOKitUser message dispatch declarations/code.
   Account for reference packing, argument counts/bounds, port acquisition/release
   and error reporting. Keep actual private-family reply production unavailable.
3. Produce an attributed packing/ownership graph, reproducible source locations
   and measurable delivery/teardown gates. Do not send callbacks, open
   experimental clients, exercise teardown or submit work on the working GPU.

Success means generic source-backed message construction and port accounting,
compared with saved consumers, with an explicit boundary at unavailable family
production and the different installed kernel. This does not verify acceptance,
exactly-once delivery, ordering, cancellation or GPU completion. Installed mapping
reclamation, vendor/indirect alias access, cross-thread reuse and complete commit
dispatch remain separate open interfaces.

Parallel areas of future investigation, when relevant: primary vendor admission
contracts, offline firmware provenance/header validation and shader target
qualification. Keep those separate from a focused lifecycle batch. Driver
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

The generic mapping continuation ran all 13 existing tests successfully and
reproduced the documented source collector: 25 downloads and 21 source/header/
license files, verified against both pinned commit trees using Git blob hashes
and SHA-256. The embedded collector matches the executed script. Source locations,
borrowed evidence, 114 relative README/docs links and whitespace were checked.
The failed commit-named tree assertion is preserved with exit 1; the collector
using the explicit tree succeeded. Fresh OS/kernel queries establish the
source/runtime version mismatch.
The collector also rejected an existing output directory with exit 1 before
downloads; source hashes remained unchanged.
No new LLDB session, mappings, experimental clients or teardown operations were
performed. These checks verify collection and document consistency, not installed
kernel cleanup or GPU functionality. Detailed records remain in the local
execution ledger and experiment index. Independent review verified the corrected
qualification for family flags, with no Critical, Important or Minor issues remaining.
Verify new changes before claiming they pass.

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
