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

These stages are deferred; experimental booting and a recovery path are required
before ownership or hardware execution. Investigate the Metal loader/factory
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
Actual host IP revisions and firmware artifact versions/digests remain unavailable.
No firmware was acquired or loaded.

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
Primary-source XNU map/unmap and user-client teardown ownership are the next
focused read-only task. Separate future studies include primary vendor
admission-contract evidence and offline firmware
provenance/header and compiler-target work. Kernel validation, negotiation,
complete ownership/ordering and actual independent bundle admission remain open.
Synthetic independent discovery remains gated on an experimental environment
and recovery path. If the required Metal integration interface cannot be established,
record that feasibility limit before investing in a large hardware stack. Do not
claim that a framebuffer, an LLVM target or registry metadata alone closes it.
