# IOAccel communication ABI inventory

The public SDK describes a legacy surface protocol. The installed private
IOAccelerator framework uses additional device/shared/queue connections for
Metal. Static inspection now establishes several connection types, selectors
and buffer sizes, but **not complete kernel structures or a usable submission
ABI**. Numeric selectors are scoped to a connection type and OS build; do not
send the values below to the working driver as probes.

The [field study](ioaccel-shared-memory.md) now identifies partial configuration
fields, shared-memory tuple names and dirty-ring consumers. Kernel acceptance,
negotiation, protection and complete synchronization/lifetime rules remain open.

## Evidence and version boundaries

Read-only study on 2026-10-02: macOS 26.4.1 build 25E253, x86_64,
PCI `1002:1638:c9`, host reporting Ryzen 5 5600GT. Runtime framework code was
inspected in our public enumeration child's address space under LLDB, with no
target expressions or manually invoked private methods/functions. No custom
connections, selector calls, mappings, queues or GPU work were created by this
research code. Existing-stack internal enumeration behavior is not our hardware
verification. See the [admission study](metal-admission.md).

Ignored `out/metal-abi-study/` contains `loader-static`, `connection-static`,
`policy-detail` and `cleanup-static` captures: commands, stdout, stderr, UTC
times, all exit 0. The native `surface-layout.c` example compiled and ran with
exit 0 (`layout-build`, `layout`, 13:42:35 UTC). Its measurements concern SDK
26.5 x86_64 layout, not measured kernel acceptance on runtime 26.4.1.
Source revisions and SHA-256 are in local `source-index.json` and
`local-index.json`; downloaded references remain untracked.

## Public legacy surface protocol

Apple's pinned IOGraphics `IOAccelClientConnect.h` defines the public surface
client as type 0 and marks later types private. The installed SDK also exposes
`kIOAccelSurface2ClientType=0x20` in a conditional section omitted from that open
source header. This is distinct from the private Metal connections below.
[Client types, 76285384](https://github.com/apple-oss-distributions/IOGraphics/blob/76285384ff0ce63965a21b8023bf6d7e447fcc19/IOGraphicsFamily/IOKit/graphics/IOAccelClientConnect.h)

Pinned IOKitUser surface wrappers show connection creation, method arguments,
error propagation and closing. `IOAccelCreateSurface` opens type 0 or the
Surface2 type depending on mode, then sets ID/mode; it closes the connection
when that setup fails. `IOAccelDestroySurface` closes it. This source is a
reference, not proof that these calls cover modern Metal presentation.
[Surface wrappers, 323ead89](https://github.com/apple-oss-distributions/IOKitUser/blob/323ead896d04424f87184d8f6ff0cce811aab106/graphics.subproj/IOAccelSurfaceControl.c)

| Surface operation | Selector from SDK enum | Input shape | Output shape |
| --- | --- | --- | --- |
| Set ID/mode | 7 | Two 64-bit scalar words: ID, mode | None |
| Set scale | 8 | One scalar option word plus caller-sized scaling structure | None |
| Set framebuffer shape | 9 | Two scalars: options, framebuffer index; variable region structure | None |
| Set shape/backing/length | 17 | Five scalars: options, framebuffer index, backing address, rowbytes, length; region structure | None |
| Flush | 10 | Two scalars: framebuffer mask, options | None |
| Read | 5 | `IOAccelSurfaceReadData` structure | None |
| Read/write lock | 12 / 14 | None | Caller-sized `IOAccelSurfaceInformation` structure |
| Read/write lock with options | 0 / 3 | One scalar option word | Caller-sized surface-information structure |
| Read/write unlock | 13 / 15 | None | None |
| Read/write unlock with options | 1 / 4 | One scalar option word | None |

The wrapper supplies an output capacity for lock calls; these rows do not
establish a fixed accepted kernel output size. Region size is its header plus
`num_rects` bounds elements. A future implementation must validate counts,
overflow and surface extents independently.
[Surface method/region definitions](https://github.com/apple-oss-distributions/IOGraphics/blob/76285384ff0ce63965a21b8023bf6d7e447fcc19/IOGraphicsFamily/IOKit/graphics/IOAccelSurfaceConnect.h),
[Surface structure definitions](https://github.com/apple-oss-distributions/IOGraphics/blob/76285384ff0ce63965a21b8023bf6d7e447fcc19/IOGraphicsFamily/IOKit/graphics/IOAccelTypes.h)

Native SDK layout observations:

| Type / constant | x86_64 SDK 26.5 value |
| --- | --- |
| `IOACCEL_TYPES_REV` | 12; header revision, not negotiated private Metal ABI version |
| Public surface method count | 18 |
| `sizeof(IOAccelBounds)` | 8 bytes |
| `sizeof(IOAccelDeviceRegion)` / offset of `rect` | 12 / 12 bytes |
| `sizeof(IOAccelSurfaceScaling)` | 44 bytes |
| `sizeof(IOAccelSurfaceInformation)` / offset of `rowBytes` | 88 / 32 bytes |
| `sizeof(IOAccelSurfaceReadData)` / offsets of `client_addr`, `client_row_bytes` | 32 / 16, 24 bytes |

`kIOAccelNumSurfaceMemoryTypes` starts at zero: this public enum does not specify
a usable surface mapping. It does not prove the private implementation has none.
Legacy lock/unlock and flush also do not define Metal fence or drawable ownership.

## Private framework transport recovered from call sites

These are static data-flow interpretations using public IOKit function
declarations and the x86_64 calling convention. They describe what the wrapper
supplies, not a successful call result or the kernel's checked dispatch table.
Each output byte count below is an **initial caller capacity**, not proof of an
exact response length. Kernel-side layouts, validation and negotiation are
unavailable unless explicitly stated.

| Wrapper | Connection type passed to `IOServiceOpen` | Additional observations |
| --- | --- | --- |
| `IOAccelDeviceCreateWithAPIProperty` | 5 | Metal's base initializer supplies service handle and API string `Metal` |
| `IOAccelSharedCreate` | 6 | Separate shared connection from the device's retained service |
| `IOAccelCommandQueueCreateWithQoS` | 8 | Associates connections with `IOConnectAddClient`, creates notification/completion infrastructure |
| `IOAccelContextCreate` | Forwarded caller argument | No constant type established by this function; do not guess it from queue type |

| Connection / wrapper | Selector and public transport call | Scalar inputs / structure bytes supplied | Output capacity / remaining unknown |
| --- | --- | --- | --- |
| Device / optional API-property setup | 9, `IOConnectCallStructMethod` | 16-byte bounded string buffer | None; API negotiation semantics/status handling incomplete |
| Device / initial configuration query | 2, struct call | No input | 600 bytes; partial counter/base data flow identified in the field study; complete layout and accepted configuration version unknown |
| Device / subsequent query | 0, struct call | No input | 64 bytes; partial bits, memory and peer fields identified in the field study |
| Device / further query | 7, struct call | No input | 24 bytes; field semantics unknown |
| Shared / creation query | 9, struct call | No input | 16 bytes; not the same selector contract as device selector 9 |
| Shared / `IOAccelSharedSetupDirtyRing` | 10, struct call | No input | 24 bytes; consumer/capacity, producer and entry pointers identified; mapping lifetime and complete ordering contract unknown |
| Shared / `IOAccelSharedAllocateFenceMemory` | 12, struct call | 8-byte structure containing forwarded argument | 8 bytes; exact allocation/result semantics unknown |
| Shared / `IOAccelSharedCreateDeviceShmem` | 7, `IOConnectCallMethod` | One scalar, no structure | 16-byte structure; virtual address (64-bit), size and ID (32-bit each) inferred from named consumers; version/protection/ownership incomplete |
| Shared / `IOAccelSharedDestroyDeviceShmem` | 8, scalar call | One scalar | None; associated allocation/lifetime rules incomplete |
| Queue / async callback registration during creation | 0, `IOConnectCallAsyncScalarMethod` | Wake port, reference count 3, no scalar payload | `NULL` output count: IOKit substitutes a shared zero capacity and stores the reply's scalar count back into it ([installed dispatch](iokit-async-dispatch.md)); callback/refcon slots and partial consumer recorded in the lifecycle study |
| Queue / creation process-information call | 5, struct call | 1028 bytes | 8-byte initial output capacity; not a command-buffer format |
| Queue / `IOAccelCommandQueueSubmitCommandBuffers` | 1, `IOConnectCallMethod` | No scalar input; structure size `8 + 24*n` on the positive-count path; `n` read from header offset 4 | No output; Metal producer writes two 32-bit shmem IDs and two copied block pointers per entry; complete format, handle validation, synchronization and kernel bounds checks unknown |

The public [IOKit call API](https://developer.apple.com/documentation/iokit/1514240-ioconnectcallmethod)
defines scalar/structure transport, not private selector meanings. Identical
selector numbers in different connection types are independent. Do not turn this
table into guessed headers or calls against the live driver.

Recovered size arithmetic does not supply a versioned submission structure.
Neither zero-sized input nor a pointer-like output establishes whether the
kernel maps storage, returns a user address, or transfers a handle. No complete
mapping contract, mapping protection, resource descriptor, shared-memory header,
atomic ordering or cross-process ownership contract has been established. The
field study records the context mapping types found later without assuming a
complete context or kernel contract.

## Ownership and failure observations

The private wrappers use CF runtime objects. Public release wrappers lead to
`CFRelease`; static finalizers for device/shared/queue objects call
`IOConnectRelease`, and the queue finalizer destroys its notification port.
Apple's IOKit declaration says the last connection reference implicitly closes
the service connection. This differs from concluding that every object release
immediately closes it. Full retain graphs and outstanding-command cleanup remain
unresolved.
[IOKit connection lifetime declarations](https://github.com/apple-oss-distributions/IOKitUser/blob/323ead896d04424f87184d8f6ff0cce811aab106/IOKitLib.h)

Construction branches check open/allocation/call errors and can return nil or
release partially built objects. Some calls have different handling: the device
API-property call is not followed by the same error check as its configuration
query. `IOAccelContextCreate` has an abort branch for one open error. This study
does not assign undocumented meanings to every error code or promise uniform
fallback behavior. Future adapter code needs bounded failure handling rather
than copying observed process-fatal behavior.

For submission, the studied wrapper validates some user-space object/count
conditions, retains the queue before the transport call and releases retained
references on submission failure. The [lifecycle study](ioaccel-lifecycle.md)
counts two retains per positive entry and one release per callback. The
[block-producer study](ioaccel-block-ownership.md) identifies scheduling/completion
copies and local failure cleanup; actual kernel delivery, reset/reuse and full
ownership remain unresolved. The lifecycle study also traces asynchronous
notification-port cleanup and corrects selector 5's output capacity to 8 bytes.
Completion behavior and kernel resource lifetimes were not exercised. Successful
enumeration does not verify submission, fences, cancellation or cleanup after
GPU failure.

## Reproduction and next gates

Use the public inventory child and captured-command procedure in the
[admission study](metal-admission.md). After stopping at the documented line,
inspect the named wrappers with `disassemble --name <wrapper>` and the three
`ioAccelDeviceFinalize`, `ioAccelSharedFinalize`,
`ioAccelCommandQueueFinalize` symbols. This reads code; it does not invoke them.
Keep assembly, addresses and failures locally. Recheck each supplied register/
stack argument against SDK IOKit declarations before interpreting a row.

For SDK layout, save the C example below inside a new ignored output directory
and compile it with `xcrun clang -Wall -Wextra -Werror <source> -o <binary>`.
It prints `sizeof`, `offsetof` and enum constants only, opens no services and
needs no GPU. Capture its build/run with the collector helper. Record SDK version,
architecture, stdout/stderr and exit status; do not carry these layouts across
architectures or runtime versions as verified wire formats.

| Next experiment | Scope | Measurable gate |
| --- | --- | --- |
| Versioned configuration/shared-memory inventory | Offline primary definitions and bounded static data flow | Field-level layouts, version/capability negotiation, mapping protections and ownership; missing evidence remains unavailable |
| Notification/fence lifetime study (partial consumer/cleanup traced: [lifecycle](ioaccel-lifecycle.md)) | Read-only declarations/call sites | Identify references, callback payload, success/error/cancellation paths and close behavior; separate source inferences from observed completions |
| Independent diagnostic protocol | Later implementation within repository; no host driver loading | Own versioned protocol with explicit sizes and validation; do not label it Metal-compatible without matching the required boundary |
| Compatible service and submission | Deferred to experimental boot/recovery and verified hardware queues | Initialization negotiates correctly; guarded copy/shader results match references; shared-memory bounds, isolation, timeout and cleanup tested |

No Apple framework or driver implementation code is incorporated here. References
are attributed study inputs; review licenses before reuse. General Apple OS
framework dependencies remain permitted, while Apple AMD binaries are excluded
from the finished stack.

```c
#include <IOKit/graphics/IOAccelSurfaceConnect.h>
#include <stddef.h>
#include <stdio.h>
int main(void) {
    printf("IOACCEL_TYPES_REV=%d\n", IOACCEL_TYPES_REV);
    printf("surface_client=%d surface2_client=%d public_client_count=%d\n", kIOAccelSurfaceClientType, kIOAccelSurface2ClientType, kIOAccelNumClientTypes);
    printf("selectors id=%d scale=%d shape=%d flush=%d read=%d read_lock=%d write_lock=%d backing_length=%d public_count=%d\n", kIOAccelSurfaceSetIDMode, kIOAccelSurfaceSetScale, kIOAccelSurfaceSetShape, kIOAccelSurfaceFlush, kIOAccelSurfaceRead, kIOAccelSurfaceReadLock, kIOAccelSurfaceWriteLock, kIOAccelSurfaceSetShapeBackingAndLength, kIOAccelNumSurfaceMethods);
    printf("sizes bounds=%zu region=%zu scaling=%zu info=%zu read_data=%zu\n", sizeof(IOAccelBounds), sizeof(IOAccelDeviceRegion), sizeof(IOAccelSurfaceScaling), sizeof(IOAccelSurfaceInformation), sizeof(IOAccelSurfaceReadData));
    printf("offsets region_rect=%zu info_rowbytes=%zu read_addr=%zu read_rowbytes=%zu\n", offsetof(IOAccelDeviceRegion,rect), offsetof(IOAccelSurfaceInformation,rowBytes), offsetof(IOAccelSurfaceReadData,client_addr), offsetof(IOAccelSurfaceReadData,client_row_bytes));
    return 0;
}
```
