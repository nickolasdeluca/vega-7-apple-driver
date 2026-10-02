# Project handoff

Read [AGENTS.md](../AGENTS.md) first. This is the tracked entry point for resuming
work without the previous conversation. The accepted scope and full milestone
sequence are in [discovery-plan.md](discovery-plan.md).

## Current checkpoint

Current research checkpoint: callback-block ownership, 2026-10-02, on branch
`cezanne-discovery`, extending `a3b779e`. This handoff is committed
with the continuation; check Git history for its commit rather than assuming a
recorded hash is HEAD.
The project remains in **read-only discovery and specification**. No independent
hardware, display or acceleration driver has been implemented or loaded.

Initial target and observed host: PCI `1002:1638`, revision `c9`, reported Ryzen
5 5600GT, macOS 26.4.1 build 25E253. SDK observations used 26.5 on x86_64; do not
silently equate SDK definitions with the runtime's private kernel contract.

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

These are observations and static consumer expectations. Independent bundle
admission, a complete negotiated kernel ABI, mapping protection/ownership,
concurrency, GPU execution and desktop presentation remain unverified. An
authorized third-party Metal loading route has not been established. Apple AMD
binaries are observation references and remain excluded from the finished stack.

## Next task: trace callback-pointer aliases and command-buffer reuse

Continue read-only graphics-contract work before driver bring-up:

1. Read the [block-ownership study](ioaccel-block-ownership.md) and reuse its
   submission, descriptor/helper and completion/deallocation captures. The
   copied pointers are also stored directly in the command buffer's
   `_scheduledCallbackBlockPtr` and `_completedCallbackBlockPtr` aliases.
2. Trace `commitAndReset`, storage deallocation and reachable alias reads/writes.
   Account for clearing, reuse, waits and cleanup without treating direct pointer
   stores as extra owned references. Keep subclass dispatch and transitive
   cleanup boundaries explicit. Do not invoke reset, create buffers/queues or
   submit work.
3. Record an evidence-backed reset/cleanup graph, reproducible inspection commands
   and measurable future validation gates. Keep kernel acceptance, reply
   cardinality, status/time units and cancellation unverified unless independent
   producer evidence actually establishes them.

Success means an evidence-backed alias/reset ownership graph, or a precise
boundary where available implementation evidence ends. It does not mean
successful GPU completion or proven concurrency. A separate primary-source
map/unmap and user-client teardown study
can narrow generic lifetime rules, but must not be presented as proof of this
private family's behavior on the installed kernel.

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

The block-ownership continuation ran all 13 existing tests successfully, rebuilt
the unchanged public metadata child and reproduced 14 documented LLDB read
commands (12 disassemblies, a module-scoped lookup and the bounded helper reader).
The reader reproduced descriptor sizes 40/48/52 and both notification signatures.
Source/header digests, 91 relative links and whitespace were checked. Raw
captures retain two all-image lookup timeouts, two absent guessed method-owner
names, an enumeration locale warning and a GitHub HTML 503; module-scoped
lookups, inherited-owner disassemblies and primary-source fallbacks succeeded.
These checks verify evidence collection and document consistency, not GPU
functionality. Independent review found no Critical, Important or Minor issues.
Detailed verification records remain in the local execution
ledger and experiment index. Verify new changes before claiming they pass.

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
