# IOAccel notification, cancellation and mapping lifecycle contract

Read-only inspection now identifies the queue notification callback, its partial
payload consumption, submission retains and callback releases. Notification-port
teardown cancels a dispatch source asynchronously. **Neither this cancellation
nor a successful transport call proves GPU completion or cancellation.** Mapping
reclamation and kernel-side ownership remain unresolved.

The [block-producer continuation](ioaccel-block-ownership.md) now connects two
distinct scheduling/completion copies to the submission entries and accounts
for local transport-failure cleanup. Kernel delivery remains unobserved.

This extends the earlier public-declaration and saved-finalizer study. It does
not implement a private protocol or exercise queues. See the
[transport inventory](ioaccel-abi.md) and [field study](ioaccel-shared-memory.md).

## Evidence and source boundaries

Host observation on 2026-10-02: macOS 26.4.1 build 25E253, x86_64, PCI
`1002:1638:c9`, reported Ryzen 5 5600GT. SDK headers are from 26.5. The public
metadata child was rebuilt with debug information and stopped after enumeration.
LLDB read instructions and symbols in the general IOAccelerator and IOKit
frameworks. No target expressions, manual private calls, experimental client
opens, queue creation, mappings or GPU submissions were made by the probe.
Existing-stack initialization during enumeration is not independent-driver proof.

The preceding handoff recorded an automated safety-check stop during a private
queue walkthrough. It did not record a technical inability to disassemble the
AMD Metal driver. This continuation's ordinary own-child LLDB inspection
succeeded without privilege elevation or changing host protections.

Ignored `out/ioaccel-lifecycle-detail/` preserves argv, stdout/stderr, status and
UTC times. `queue-lifecycle` (16:16:40 UTC), `callback-cleanup` and
`callback-guards` all exited 0. They cover creation/submission/finalization,
callback consumers and assertion branches, context mappings/finalization, and
notification-port implementation. `queue-lifecycle` also contains the loaded
IOAccelerator symbol table. `documented-lifecycle` reproduces the commands below.
Raw addresses and process information remain local. Disassembly padding after
returns/tail calls is not treated as code.

Primary references, read on 2026-10-02:

- [IOKitUser declarations, 323ead89](https://github.com/apple-oss-distributions/IOKitUser/blob/323ead896d04424f87184d8f6ff0cce811aab106/IOKitLib.h).
- [IOKitUser notification implementation, 323ead89](https://github.com/apple-oss-distributions/IOKitUser/blob/323ead896d04424f87184d8f6ff0cce811aab106/IOKitLib.c): reference implementation, not asserted to be the exact installed build.
- [XNU notification reference indices, f6217f89](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/iokit/IOKit/OSMessageNotification.h).
- [XNU mapping options, f6217f89](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/iokit/IOKit/IOMapTypes.h).
- [Apple dispatch cancellation guidance](https://developer.apple.com/library/archive/documentation/General/Conceptual/ConcurrencyProgrammingGuide/GCDWorkQueues/GCDWorkQueues.html), corroborated by installed SDK `dispatch/source.h`.

Saved source/header digests and capture results are indexed in `source-index.json`,
`local-index.json` and `experiment-index.json` in the output directory. The
optional SDK `IOKitLibPrivate.h` was unavailable; no private-header contents are
assumed. An initial Apple documentation URL returned 404; the primary archived
guide above and installed header supplied the cancellation contract instead.

## Public contracts and their limits

| Topic | Declared behavior | Limit |
| --- | --- | --- |
| Notification port | Owns its Mach receive port and run-loop source; destroy releases its infrastructure. | Public ownership text alone does not prove that all callbacks have returned at destroy's return. |
| Async transport | Takes a wake port, reference array and count, and scalar/structure arguments. Callback typedefs take refcon, result and family-defined arguments. | Does not define the private payload or number of replies. |
| `IOConnectRelease` | Drops a connection reference; the last reference implicitly closes the service connection. | An individual CF release or connection release need not close it. Kernel work reclamation is not specified here. |
| Map/unmap | Map returns address/size in a specified task; unmap takes connection, memory type, task and address. | A task argument does not prove survival after CF-object release or connection close. |
| `IOConnectAddClient` | Associates another connection. | Does not specify private-family ownership or teardown order. |

`kIOMapAnywhere = 1` controls placement; `kIOMapReadOnly = 0x1000` is separate.
The context wrapper passes 1. Its observed option does not establish actual
mapping protection or cache policy. The earlier claim that a mapping necessarily
outlives its CF/Objective-C owner unless explicitly unmapped was unsupported;
**that lifetime remains unknown**.

## Queue registration and submission references

These are static caller expectations on this build, not observed messages or
successful kernel calls:

1. `IOAccelCommandQueueCreateWithQoS` opens connection type 8, stores its handle
   in a CF object and associates the shared connection using `IOConnectAddClient`.
2. It creates a dispatch queue named
   `com.apple.IOAccelerator.CommandQueueCompletion`, creates a notification port,
   assigns that dispatch queue to the port, then releases its local dispatch-queue
   reference. `IOAccelCommandQueueSetDispatchQueue` later forwards to the same
   IOKit setter; the initial delivery queue is not necessarily permanent.
3. Queue selector 0 registers with `IOConnectCallAsyncScalarMethod`: wake port,
   reference count 3, no scalar payload or output. Reference slot 1 holds
   `ioAccelCommandQueueBlockFenceCallback`; slot 2 holds the queue object as
   refcon. XNU's public indices identify these positions; slot 0 is reserved.
   No additional `CFRetain` appears at this registration site.
4. Association/dispatch-queue creation failures release the CF object. A
   notification-port creation failure first releases the local dispatch queue.
   Async-registration failure destroys and clears the notification port, then
   releases the CF object. These branches do not establish complete construction
   cleanup: allocation failure after open and fatal helper branches still need
   separate accounting.
5. The later selector 5 process-information call supplies 1028 input bytes and
   an **8-byte initial output capacity**, correcting the earlier inventory's
   “no output” entry. The output is stored at queue-object offset `0x28` on
   success and zeroed on failure; its full contract remains unspecified here.

For positive entry count `n`, `IOAccelCommandQueueSubmitCommandBuffers` calls
`CFRetain(queue)` **twice per entry**, then sends selector 1 with structure size
`8 + 24*n`. A nonzero transport result releases the same `2*n` references.
A zero result leaves them outstanding in this wrapper. Count validation,
zero/negative-count behavior and kernel acceptance are not a usable submission
specification and must not be inferred from this size expression.

## Callback consumer and incomplete completion accounting

The registered callback checks non-null refcon, zero transport result and a
non-null block pointer. Its cold branches call `__assert_rtn`; they are not
recoverable error cleanup. The assertion text corroborates those three checks.
This does not establish how a GPU error is represented in the separate payload.

| Byte offset from callback argument-array pointer | Read and use on x86_64 |
| --- | --- |
| 0 | 64-bit block pointer; used as the block invocation's first argument |
| 16 | 64-bit value forwarded as its next argument |
| 32 | 64-bit value forwarded as its next argument |
| 48 | 32-bit value forwarded as its next argument |

The callback invokes the block, releases it with `_Block_release`, and finally
releases **one** queue CF reference. It does not read the other intervening slots
or check the callback argument count in the inspected instructions. This is a
partial consumer layout, not a complete message schema or evidence of which
values the kernel sends. Units, timestamps, status meanings, minimum accepted
message length and producer-side validation remain unknown.

The pinned IOKitUser dispatcher obtains function/refcon from the notification
reference array and selects callback arity from message length. This explains
how public callback types connect to the private consumer, without proving the
host's entire dispatch path or a private message's validity.

The [producer continuation](ioaccel-block-ownership.md) identifies two copied
blocks per entry, for scheduling and completion, plus local failure cleanup.
This supports an expected two-event design beyond retain-count arithmetic.
Exactly one kernel delivery for each copy would balance the inspected normal
path, but callback cardinality remains unverified. Nothing here proves successful
completion, balanced lifetimes under loss/duplicates, or GPU fence semantics.

## Notification cancellation and finalization

Installed IOKit instructions corroborate the following source-level pattern:
`IONotificationPortSetDispatchQueue` cancels/releases an old dispatch source,
adds an internal notification-object reference, installs a receive source and
sets `IONotificationPortRelease` as its cancellation handler. Destroy requests
source cancellation, releases the source and drops the owner's notification
reference. The internal release helper destroys the receive right and frees
storage only at its final reference. The port can therefore remain internally
alive while cancellation completes. This internal count is separate from the
queue CF retains above.

Apple's dispatch contract says cancellation is asynchronous, prevents new event
handler invocations, and lets an already running handler finish before the
cancellation handler runs. Consequently, destroy's return is **not a documented
callback-drain barrier**. This concerns notification infrastructure; it does not
establish cancellation of pending GPU requests or release of their retained
blocks/queues. [Dispatch cancellation](https://developer.apple.com/library/archive/documentation/General/Conceptual/ConcurrencyProgrammingGuide/GCDWorkQueues/GCDWorkQueues.html)

| Finalizer | Visible cleanup | Remaining limit |
| --- | --- | --- |
| `ioAccelDeviceFinalize` | Frees non-inline storage, releases and clears connection handle. | No explicit unmap or GPU wait visible. |
| `ioAccelSharedFinalize` | Releases a block, releases/clears connection, walks tracking list under lock with optional hook and frees entries. | Per-entry hook and kernel allocation ownership unresolved. |
| `ioAccelCommandQueueFinalize` | Destroys/clears notification port, releases/clears connection, decrements context count. | No explicit GPU cancel or drain visible; port destruction has the asynchronous behavior above. |
| `ioAccelContextFinalize` | Destroys/clears notification port, releases/clears up to 16 stored CF references, releases/clears connection, decrements context count. | No explicit unmap visible; released objects' transitive cleanup is not established. |

Device/shared rows reuse `out/metal-abi-study/cleanup-static`; queue/context rows
were captured again in this continuation. These finalizers **release connection
references**; close occurs only when the last reference is gone. Outstanding
submission retains may prevent the queue finalizer from running at all, so the
finalizer alone cannot demonstrate pending-work cancellation.

## Mapping search result

`IOAccelContextGetFenceBuffer` maps types 2 then 1 with `IOConnectMapMemory`.
It returns the first mapping's outputs before attempting the second. On the
second failure it clears the second pair of outputs but does not explicitly
unmap the first in this function. The context finalizer adds no direct unmap.

The loaded IOAccelerator symbol-table dump includes an `IOConnectMapMemory`
trampoline and no `IOConnectUnmapMemory` or `IOConnectUnmapMemory64` entry.
This broadens the earlier search of selected functions to the module's visible
symbol/trampoline inventory. It is **not** a complete import/bind or call-graph
proof: indirect calls, other modules, released objects and kernel reclamation
remain possible. Do not infer either a leak or guaranteed close-time cleanup.

## Next experiments and measurable gates

The [block-producer study](ioaccel-block-ownership.md) supplies an ownership graph
for the inspected normal/error paths. The [reset/reuse study](ioaccel-buffer-reuse.md)
adds bounded alias-reference results, completion predicates and storage pooling.
The completion predicate is not shown to drain outer callback/storage releases.
Kernel message validation remains unresolved; no callback should be manually
invoked and no GPU work submitted.

A primary-source study is next for XNU map/unmap and user-client teardown
ownership. It must distinguish generic source behavior from private-family
behavior and the installed kernel revision. A full bind inventory would narrow
the symbol-search limitation but would not alone settle mapping lifetime.

Future dynamic gates require an experimental environment and recovery path per
[AGENTS.md](../AGENTS.md); none is authorized on the working host:

| Gate | Measurable pass criterion for our future implementation |
| --- | --- |
| Pending-work cancellation | Every accepted request reaches one documented terminal outcome; all retained blocks/queues balance; cleanup completes within a stated timeout. |
| Notification teardown | No use of freed callback state; resources released after in-progress handlers finish; no delivery after an explicit drain boundary. |
| Mapping/close | All allocations reclaimed at the documented boundary; no stale mapping access or repeated-cycle growth. |
| Partial initialization | Each injected failure reclaims acquired state, including the first map if the second fails. |
| Release order/process exit | Supported orders and rejected invalid orders behave predictably; a killed client leaves a new client usable. |

These are requirements to test, not promises made by the installed driver.

## Reproduction

Use the public metadata child in [metal-loader-study.md](metal-loader-study.md)
and the command-capture helper in [metal-admission.md](metal-admission.md).
Build into a **new ignored directory**, record OS/build, PCI/revision, SDK and
UTC time, and preserve stdout/stderr/status for every batch. After LLDB stops at
`probe.m:48`, run the following debugger commands. They read code/symbols only;
do not use target expressions or call the functions. Stop if inspection is denied.

```text
disassemble --name IOAccelCommandQueueCreateWithQoS
disassemble --name IOAccelCommandQueueSubmitCommandBuffers
disassemble --name ioAccelCommandQueueBlockFenceCallback
disassemble --name ioAccelCommandQueueBlockFenceCallback.cold.1
disassemble --name ioAccelCommandQueueBlockFenceCallback.cold.2
disassemble --name ioAccelCommandQueueBlockFenceCallback.cold.3
disassemble --name IOAccelCommandQueueSetDispatchQueue
disassemble --name ioAccelCommandQueueFinalize
disassemble --name IOAccelContextGetFenceBuffer
disassemble --name ioAccelContextFinalize
disassemble --name IONotificationPortSetDispatchQueue
disassemble --name IONotificationPortDestroy
disassemble --name IONotificationPortRelease
image dump symtab IOAccelerator
```

Inspect the symbol-table portion separately from disassembly; retain a search's
no-match status rather than translating it into a whole-system absence. Confirm
each argument against SDK declarations and the architecture's calling convention.
If `out/` is absent, obtain the pinned sources above, read the installed SDK
headers and record new digests; older host captures are not prerequisites.

No Apple implementation code is incorporated. Attribution is not a license to
reuse it; Apple AMD driver binaries remain excluded from the finished stack.
