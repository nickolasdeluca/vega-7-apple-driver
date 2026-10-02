# Project handoff

Read [AGENTS.md](../AGENTS.md) first. This is the tracked entry point for resuming
work without the previous conversation. The accepted scope and full milestone
sequence are in [discovery-plan.md](discovery-plan.md).

## Current checkpoint

Last research batch: `49a7adf`, 2026-10-02, on branch `cezanne-discovery`.
Check current Git status/history rather than assuming this remains HEAD.
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

These are observations and static consumer expectations. Independent bundle
admission, a complete negotiated kernel ABI, mapping protection/ownership,
concurrency, GPU execution and desktop presentation remain unverified. An
authorized third-party Metal loading route has not been established. Apple AMD
binaries are observation references and remain excluded from the finished stack.

## Next task: notification, cancellation and mapping cleanup

Continue the read-only IOAccel lifecycle study before attempting driver bring-up:

1. Reuse saved queue creation/submission/finalizer and context-mapping captures.
   Start with `IOAccelCommandQueueCreateWithQoS`,
   `IOAccelCommandQueueSubmitCommandBuffers`, `ioAccelCommandQueueFinalize`,
   `ioAccelSharedFinalize` and `IOAccelContextGetFenceBuffer`.
2. Trace notification registration, callback payloads, retained references,
   success/error/cancellation and connection/mapping cleanup. Use primary
   declarations and targeted static inspection of general OS frameworks.
3. Produce a lifecycle contract document with sourced facts, local observations,
   unresolved paths, reproducible commands and measurable future test gates.
   Update the existing inventories where new evidence resolves their limits.

Success means documenting callback/ownership/close data flow to the extent
supported by evidence, and clearly identifying missing cancellation, unmapping
and kernel-side guarantees. Static inspection cannot verify GPU completion or
concurrency. Do not manually invoke private helpers/selectors, open experimental
user clients, create queues/mappings or submit work on the working host.

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

The last research batch verified all 13 existing tests, the documented 15-command
static-inspection batch and six policy queries, source digests, relative links
and whitespace. Independent review checked the field/policy claims; a scope
sentence was clarified to distinguish manual calls from internal initialization.
These are past results; verify new changes before claiming they pass.

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
