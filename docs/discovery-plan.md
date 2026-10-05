# Discovery milestone and implementation sequence

This document records the accepted conversation plan and the scope completed
in the initial milestone. Initial hardware: Cezanne `1002:1638:c9`, host reporting
Ryzen 5 5600GT, macOS 26.4.1. Expansion follows success on this target.

## Operating decisions

The root [AGENTS.md](../AGENTS.md) was the first implementation commit,
`147e4bd`, and defines architecture, verification, host constraints and commit
rules. Work uses branch `cezanne-discovery` in the initially empty checkout.
Small, focused batches use the configured Git author with no agent co-authors.

This milestone delivers diagnostic tooling and evidence-backed specifications.
No hardware-core implementation, driver installation, ownership change or GPU
register access belongs to this milestone. Native device enumeration uses the
existing stack and can initialize its own internal objects; it submits no GPU
work. It is observation evidence, not independent-driver verification.

Raw captures remain in ignored `out/` directories. The tracked baseline is a
reviewed summary without display serials, EDID, process information or machine
identity. Failures from the restricted execution sandbox remain available.
Python standard-library collection works independently of the optional SDK-built
Metal inventory. Hardware block versions and physical memory are not inferred
from the installed driver's product strings or Metal flags.

General Apple frameworks remain dependencies; Apple AMD driver binaries are
excluded from the finished stack. From scratch applies to host-side driver code.
AMD microcode is permitted, with signed-image validation and license/provenance
review before use. This milestone incorporates no firmware or upstream driver
code. The references are study inputs.

## Initial acceptance record

| Requirement | Evidence |
| --- | --- |
| Operating rules before tools | `147e4bd` contains only AGENTS.md |
| OS/build, CPU, PCI/revision, topology, memory, displays, loaded stack | [Baseline summary](hardware-baseline.md); six successful collector queries |
| Independent agreement and preserved failures | Textual PCI/display queries and public Metal inventory compared with normalized report; earlier restricted capture retained |
| Hardware block map | [Cezanne study](cezanne-hardware.md), pinned Linux v6.12 and AMD references; unresolved target values stated |
| Tahoe graphics study and early Metal discovery | [Contract study](graphics-contract.md); accelerator ID/plugin loading correlation, public contracts, unresolved vendor ABI |
| Verification | Standard-library tests for parsing, failure, timeout, malformed inputs and overwrite protection; macOS native probe build/run; host comparisons |
| Read-only host | Fixed OS queries, metadata inspection and public device-property inventory; no direct MMIO, driver loading or boot changes |

## Subsequent milestones

Experimental booting goes through a USB test EFI, with the untouched internal
EFI as the recovery path ([test boot](test-boot.md)). The driver's test
stages are passive attach (0) and read-only device access (1), booted in that
order; each later test stage needs a reviewed procedure and the user's approval. Investigate the Metal loader/factory
contract before substantial hardware bring-up. The architecture keeps the
Cezanne core, IOKit adapter, user-space driver and shader backend separate.

| Stage | Concrete deliverable | Gate |
| --- | --- | --- |
| 1. PCI ownership | Own narrowly matched PCI service and diagnostic interface | Exact target/revision, truthful identity and BAR reports; clean recovery; no competing owner |
| 2. Firmware/memory/interrupts/DMA | Signed firmware lifecycle, GPU mappings, IH handling, SDMA copy/fence | Guarded byte-for-byte copy and bounded fence/interrupt completion; verified cleanup on failure |
| 3. Display | Own DCN configuration for one named connector/mode | Stable test pattern with correct pitch, pixel format, timing and recovery |
| 4. Shader | Own command queue executes a fixed known kernel | CPU-reference output, guard integrity, correct synchronization and bounded completion |
| 5. Metal | Own discoverable vendor plugin, rendering, surfaces and presentation | Enumeration and correct rendering/presentation without Apple AMD binaries; cross-process sharing and lifetime verification |
| 6. Desktop and resilience | Composition, recovery, sleep/wake, multiple workloads | Reliable desktop, logged recovery, repeated suspend/resume and broader correctness/performance checks |

## Follow-up investigation record

The [loader/factory study](metal-loader-study.md) identifies the observed
`GFX9_MtlDevice` class hierarchy and candidate registration/initialization
metadata. Its documented metadata probe compiles and reproduces the observation;
failed standalone-binary, signature and cache-section inspections are preserved.
Lookup roots, signing admission, invocation order and the usable IOAccel ABI
remain unresolved. No private candidate methods were called.

The [offline target manifest](cezanne-target-manifest.md) traces `1638` to the
Green Sardine APU flag and records conditional IP dispatch/firmware selection.
Actual host IP revisions remain unavailable. The
[firmware provenance study](firmware-provenance.md) pins the candidate images to
linux-firmware `20260916`, verifies them over two routes, records license
constraints and validates their documented headers with a tested bounded parser.
No firmware was loaded or interpreted beyond its headers. The
[shader target study](shader-target.md) selects `gfx90c` and records one kernel's
offline ISA, descriptor and metadata from a verified LLVM 20.1.7 toolchain.

The [admission study](metal-admission.md) subsequently establishes ordered resolver
roots, class precedence and the `_MTLDevice` check, records a public-enumeration
loader backtrace, and identifies a separate rootless trust layer. The
[IOAccel inventory](ioaccel-abi.md) records legacy SDK layouts and partial private
connection/selector/size/lifetime data from static call sites. Probe builds/runs,
bounded debugger reads and source checks were verified; no private calls were
manually sent to the driver. Actual independent admission, configuration fields,
shared-memory layout/ordering and kernel-side negotiation remain unresolved.

The [requirements follow-up](metal-admission-requirements.md) now records reported
SIP/authenticated-root status, protected GPU-bundle directories and WindowServer's
library-validation flag. It identifies the unresolved authorized installation
and application-policy route without assuming a Developer ID signature closes it.
The [field study](ioaccel-shared-memory.md) names partial configuration fields,
allocation address/size/ID, dirty-ring pointers/entries and publication instructions.
These are static consumer expectations, not observed kernel response or GPU work.

The [lifecycle continuation](ioaccel-lifecycle.md) now identifies the notification
callback, submission retains, callback release, asynchronous dispatch cancellation
and context finalization. Mapping reclamation and callback cardinality remain
unverified. The [block-producer study](ioaccel-block-ownership.md) then connects
two scheduling/completion copies to each entry, traces retained captures and
local transport-failure cleanup, and stops at the unobserved kernel delivery
boundary. The [reset/reuse study](ioaccel-buffer-reuse.md) then traces completion
predicates, reset state and pooled/unpooled storage cleanup. Its bounded code
scan validates only the two alias-slot producer references under supported
encodings; indirect/vendor accesses and outer callback drain remain unresolved.
The [generic mapping study](xnu-mapping-lifecycle.md) identifies client-set and
task-port owners, explicit unmap, close/no-senders and task VM removal in pinned
XNU source. The host reports a different XNU revision; installed family cleanup
and backing/GPU ownership remain unverified. The [async-reply study](xnu-async-replies.md)
then traces generic message packing, port ownership, send limits and dispatch
arity. Its offline layout/field fixtures are not live reply evidence. The
[installed dispatch comparison](iokit-async-dispatch.md) finds the IOKit receive
path, wrappers and generated client stub consistent with that source and IDL.
It adds client-only count checks and a shared output-count hazard. The
[family reply study](ioaccel-family-replies.md) now maps installed static
registration storage, seven-word producers, per-entry send attempts and
cancellation/teardown ownership. Runtime delivery and GPU completion remain
unverified. Separate future
studies include primary vendor
admission-contract evidence and the Metal shader frontend boundary. Kernel validation, negotiation,
complete ownership/ordering and actual independent bundle admission remain open.
Synthetic independent discovery remains gated on an experimental environment
and recovery path. If the required Metal integration interface cannot be established,
record that feasibility limit before investing in a large hardware stack. Do not
claim that a framebuffer, an LLVM target or registry metadata alone closes it.
