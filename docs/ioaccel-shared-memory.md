# IOAccel configuration and shared-memory field study

Static consumer data flow now identifies several configuration fields, a device
shared-memory result tuple and the dirty-ring writer's layout. These are
**version-specific wrapper expectations**, not a complete checked kernel ABI or
observed successful submission. No private calls, allocations, mappings or GPU
commands were manually issued. See the [transport inventory](ioaccel-abi.md) and
the separate [bundle admission requirements](metal-admission-requirements.md).

## Experiment record

Read-only study on 2026-10-02: macOS 26.4.1 build 25E253, x86_64, PCI
`1002:1638:c9`, host reporting Ryzen 5 5600GT; SDK 26.5. Our existing public
enumeration/metadata child was built with debug information and stopped after
enumeration under LLDB. Inspection read general Metal and IOAccelerator framework
instructions; it did not execute private getters or inspect Apple's AMD driver
implementation. The installed stack remained the graphics owner.

Ignored `out/metal-contract-detail/` preserves commands, stdout, stderr, UTC
timestamps, exit status, source revisions and SHA-256 digests:

| Capture | Result / principal evidence |
| --- | --- |
| `config-fields` (14:11:11 UTC) | Exit 0; device creation, configuration, peer, memory-size and trace getters |
| `shmem-fields` (14:12:47 UTC) | Exit 0; device shared-memory initializer/getters/destructor, shared creation and event tests |
| `dirty-ring` (14:14:04 UTC) | Exit 0; dirty setup, payload producers, flush, memory-data query and context fence mapping |
| `ring-writer` (14:14:59 UTC) | Exit 0; ring write loop, ordering instructions, base memory-property getters and a partial command-header finalizer |

The inherited [Metal base initializer capture](metal-admission.md) supplies named
ivar destinations for configuration getters. Symbol names and those destinations
support field labels below; their semantics are still static interpretations.
No kernel response bytes or completion results were collected. Do not confuse
wrapper-object offsets with method-result offsets. Output sizes below remain
initial caller capacities. Disassembly after a return is not evidence of another
executed operation.

## Device configuration expected by the framework

Device connection type 5, selector 0 writes into a contiguous 64-byte region of
the private CF device object. Getters read that region; Metal passes destinations
named `_configBits`, `_deviceBits`, `_textureRam`, `_videoRam`, `_accelID`,
`_sharedMemorySize`, `_peerGroupID`, `_peerIndex` and `_peerCount`.

| Byte offset within selector 0 result | Read width | Identified consumer / limit |
| --- | --- | --- |
| 0 | 32 bits | Configuration bits; full capability definitions unknown |
| 4 | 32 bits | Device bits; full definitions unknown |
| 8 | 64 bits | `_textureRam`; units and physical interpretation not independently verified |
| 16 | 64 bits | `_videoRam`; likewise a framework memory quantity |
| 24 | 32 bits | Accelerator ID |
| 28 | Unresolved | Not assigned a meaning |
| 32 | 64 bits | Shared-memory size getter |
| 40–47 | Unresolved | Not assigned a meaning |
| 48 | 64 bits | Peer group ID |
| 56 | 32 bits | Peer index |
| 60 | 32 bits | Peer count |

`IOAccelDeviceGetConfig64` copies 64-bit memory quantities, while
`IOAccelDeviceGetConfig` copies their low 32 bits. These getter variants are not
evidence of kernel version negotiation. The base Metal `hasUnifiedMemory`
implementation extracts configuration bit 12; vendor classes may override it.
The base initializer also consults that bit when adjusting its reported shared
and dedicated memory quantities. These are installed-stack reporting semantics,
not proof of Cezanne's physical memory architecture.

The other initialization queries stay distinct:

| Device selector / capacity | Static field consumption | Remaining limits |
| --- | --- | --- |
| 2 / 600 bytes | Result offset 0: 64-bit base subsequently dereferenced. Offset 20: signed 32-bit count. For multiple entries, 32-bit offsets beginning at byte 88 are added to the base to form counter pointers; initial 32-bit values are cached. Other fields are copied without resolved meanings. | Complete structure, count bounds, mapping extent/protection and counter ownership unknown |
| 7 / 24 bytes | Offset 0: 64-bit value returned by the global-trace-object-ID getter. Offset 8: 64-bit value retained on the branch where the 32-bit word at offset 16 is at least 4. | The guard is not established as an ABI version; the second value's semantics and remaining fields are unknown |

No public primary definition examined here supplies the private configuration
schema or a negotiation rule. Transport arguments alone do not establish what
the kernel validates or which configurations an independent driver must support.

## Device shared-memory result tuple

Shared connection type 6, selector 7 receives one scalar formed from the
initializer's 32-bit `shmemSize` argument and supplies 16 bytes of output capacity.
Tracing its outputs into `MTLIOAccelDeviceShmem` resolves the previous unnamed
tuple:

| Result byte offset | Width | Named Metal consumer |
| --- | --- | --- |
| 0 | 64 bits | `virtualAddress` |
| 8 | 32 bits | `shmemSize` |
| 12 | 32 bits | `shmemID` |

On transport error the wrapper clears all three caller outputs and returns the
error. The Metal initializer takes its failure branch; the successful
construction branch stores the outputs. Its destructor calls shared selector 8
through `IOAccelSharedDestroyDeviceShmem` when the ID is nonzero, then clears it.
That identifies a paired create/destroy handle, not when the kernel may safely
reclaim memory still referenced by GPU work.

The optional allocation-tracing list also records this tuple under a user-space
lock. That list is tracing bookkeeping, not the complete allocation ownership
graph. The create/destroy wrappers do not show a separate `IOConnectMapMemory`
call in these functions. Whether and how the kernel establishes or removes the
returned virtual address, validates the requested size, pins pages, enforces
access rights or handles process exit remains unresolved.

## Dirty-ring addresses, entries and publication

Shared selector 10 supplies 24 bytes of output capacity. Successful setup
consumes three 64-bit values as pointers; the writer establishes their roles:

| Result offset | Static use |
| --- | --- |
| 0 | Consumer/capacity header address: 32-bit consumer counter at pointed-to offset 0 and capacity at offset 4 |
| 8 | Producer-counter address: 32-bit producer counter at pointed-to offset 0 |
| 16 | Address of 16-byte entry slots |

Setup computes `capacity - 1`. The writer selects each slot with
`counter & (capacity - 1)` and copies 16 bytes. This is consistent with a
power-of-two ring, but the inspected setup does not demonstrate validation of
that invariant. Capacity, extent and power-of-two checks remain requirements
for a future compatible implementation, not verified kernel behavior.

User-space writers take an `os_unfair_lock`. The available-space calculation
uses the 32-bit consumer and producer counters plus capacity. On its publication
path the x86_64 instructions copy entries, perform a locked operation on stack
storage, then store the advanced producer counter. This is consistent with a
publication barrier; it does not specify portable C atomic orders, the kernel's
matching reads or the GPU's coherence rules. No concurrency experiment ran.

Inspected payload producers use these entry shapes:

| Producer branch | Entry contents, relative byte offsets |
| --- | --- |
| Buffer range fitting 32-bit offset/length | One entry: resource value at 0 (32 bits), tag 1 at 4, offset at 8 (32 bits), length at 12 (32 bits) |
| Buffer range requiring wider offset/length | Two entries: resource/tag 3/64-bit offset, followed by resource/tag 4/64-bit length, each with the payload at byte 8 |
| Face/level producer | One entry: resource at 0, tag 2 at 4, two forwarded 32-bit parameters at 8 and 12; full face/level interpretation remains unknown |
| Vendor-command producer | One entry: resource at 0, caller tag at 4, forwarded 64-bit payload at 8; the observed unsigned check accepts tags at least 255 |

A zero buffer-range length takes the producer's no-work branch. These records
describe resource dirty tracking, not GPU instruction packets or the complete
queue submission format. Resource IDs, authorization and consumer validation
remain unverified.

The writer's capacity/initial-counter branches call shared selector 11 via
`IOConnectCallScalarMethod`, without scalar payload or output. On a call failure
it resets the producer value from the consumer and returns zero; its settled
publication path returns the prior producer value. That is not a uniform
`IOReturn` success convention. `IOAccelSharedFlushDirtyRing` also uses selector
11 conditionally after counter arithmetic. Kernel consumption, notification,
counter wrap and loss/error recovery have not been observed.

## Events, mapping and incomplete command headers

`IOAccelDeviceTestEventFast` inspects eight packed 64-bit event words: it uses
the low 32 bits as an index, treats low value -1 as a sentinel, and compares the
high 32 bits with cached/current mapped 32-bit counter values. This ties the
selector 2 counter pointers to an event consumer. It does not establish valid
index ranges, sequence-wrap rules, cancellation or a complete fence protocol.

`IOAccelContextGetFenceBuffer` statically calls `IOConnectMapMemory` for memory
types 2 and 1 on its context connection, passing mapping option 1 and returning
addresses/sizes through distinct outputs. SDK 26.5 and pinned XNU name option 1
`kIOMapAnywhere`; it is not the `kIOMapReadOnly` option. Kernel mapping protection,
the context's caller-selected connection type and full unmapping lifetime are
not established. If the second mapping fails, the first successful mapping is
not explicitly undone in this function; cleanup elsewhere remains to be traced.
[IOKit mapping declarations](https://github.com/apple-oss-distributions/IOKitUser/blob/323ead896d04424f87184d8f6ff0cce811aab106/IOKitLib.h),
[XNU mapping options, f6217f89](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/iokit/IOKit/IOMapTypes.h)

A command-storage finalizer supplies only fragments: a branch writes a 32-bit
byte-distance value at pointed-to offset 12; another writes an incremented count
at offset 8 and advances by `16 + 8*count`; its final path sets a bit in byte 15
of another referenced header. Without corresponding creation/reader contracts,
these fragments do not define header identity, version, bounds or accepted
commands. They are retained as leads rather than guessed submission structures.

## Reproduction and remaining gates

Use the [admission study's](metal-admission.md) exact public child, debug build,
capture helper and line-48 breakpoint. After stopping, the following LLDB
commands inspect the principal field and ring evidence without calling it:

```text
disassemble --name IOAccelDeviceCreateWithAPIProperty
disassemble --name IOAccelDeviceGetConfig64
disassemble --name IOAccelDeviceGetSharedMemorySize
disassemble --name IOAccelDeviceGetPeerInfo
disassemble --name "-[MTLIOAccelDevice initWithAcceleratorPort:]"
disassemble --name IOAccelSharedCreateDeviceShmem
disassemble --name "-[MTLIOAccelDeviceShmem initWithDevice:shmemSize:]"
disassemble --name "-[MTLIOAccelDeviceShmem virtualAddress]"
disassemble --name "-[MTLIOAccelDeviceShmem shmemSize]"
disassemble --name "-[MTLIOAccelDeviceShmem shmemID]"
disassemble --name "-[MTLIOAccelDeviceShmem dealloc]"
disassemble --name IOAccelSharedSetupDirtyRing
disassemble --name _ioAccelSharedWriteDirtyResourceCommand
disassemble --name IOAccelSharedDirtyResourceBufferRange
disassemble --name IOAccelSharedFlushDirtyRing
```

Check the intended stop reason and every command's output; process exit 0 alone
is insufficient. Preserve missing symbols, denied launches and changed layouts
as unavailable evidence. Never reuse raw addresses across launches or send these
selectors as live probes.

| Next work | Scope / measurable gate |
| --- | --- |
| Version and ownership contracts | Read-only primary definitions/call-site study: establish negotiation, accepted sizes, counter extent, protection and lifetime; distinguish unknown policy from recovered consumer fields |
| Notification and shutdown | Read-only: identify callback payloads, outstanding references, cancellation and mapping cleanup paths; no assertion of observed GPU completion |
| Independent protocol design | Later repository-only implementation: explicit own versions, sizes, bounds and lifetime with meaningful offline tests; no Metal compatibility claim from that protocol alone |
| Actual shared-memory/submission validation | Deferred experimental environment: accepted initialization, mapping isolation, concurrent publication, wrap, failure/close and verified copy/shader/fence results |

The advance is a field-level expectation inventory. Actual returned data,
kernel-side validation and a complete interoperable ownership/synchronization
contract remain open. No Apple implementation code is incorporated; attribution
does not grant a license to reuse it, and Apple AMD binaries remain excluded from
the finished stack.
