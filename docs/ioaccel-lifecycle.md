# IOAccel notification, cancellation and mapping lifecycle contract

This document records what the **public IOKit declarations** guarantee about
notification ports, asynchronous calls, memory mappings and connection release,
and maps those guarantees onto the private-framework observations already
committed in the [transport inventory](ioaccel-abi.md) and the
[field study](ioaccel-shared-memory.md). It adds no new private-binary analysis.
It describes no kernel-side behavior, GPU completion or concurrency result.

Scope of this batch: public declarations only. The private queue creation,
submission and finalizer paths are cited from the earlier documents; their
callback payload and cancellation details are **not** extended here and remain
open (see [Unresolved](#unresolved-paths)).

## Sources

Read on 2026-10-02 from the saved SDK 26.5 `IOKitLib.h` (x86_64 SDK; runtime is
macOS 26.4.1 build 25E253) and the pinned XNU/IOKitUser references already used
by the other studies. Saved copies are in ignored
`out/metal-contract-detail/` (`sdk-IOKitLib.h`, `xnu-IOMapTypes.h`,
`local-index.json` with digests).
[IOKitUser IOKitLib.h, 323ead89](https://github.com/apple-oss-distributions/IOKitUser/blob/323ead896d04424f87184d8f6ff0cce811aab106/IOKitLib.h),
[XNU IOMapTypes.h, f6217f89](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/iokit/IOKit/IOMapTypes.h)

## What the public contract states

| Topic | Declared behavior | What it does **not** say |
| --- | --- | --- |
| `IONotificationPortDestroy` | Destroys the notification object and any Mach port or run-loop source obtained from it; callers must not release those separately. | Whether messages already queued are delivered, dropped or still reference caller state afterwards. |
| `IONotificationPortGetMachPort` | Returns a port to listen on; ownership stays with the notification object. | Which message formats a given family sends on it. |
| Async call family (`IOConnectCallAsync*Method`) | Takes a wake port, a caller reference array with count, and normal scalar/structure arguments. | Reference-slot meanings, how many replies may arrive, or what happens to an in-flight request if the port is destroyed. |
| Callback typedefs (`IOAsyncCallback0/1/2/IOAsyncCallback`) | Callback receives the caller refcon, an `IOReturn` result, and zero or more extra arguments. | The extra arguments are family-defined; nothing here says what a private family places there. |
| `IOConnectRelease` | Removes one reference to the connect handle; **the last reference performs an implicit `IOServiceClose`**. | That any given release closes the connection, or what the kernel does with outstanding mappings and work at close. |
| `IOConnectMapMemory64` | Family interprets `memoryType`; address and size come back on success; with `kIOMapAnywhere` the caller supplies no address. | Which memory types a family supports, protection, caching, or maximum extent. |
| `IOConnectUnmapMemory64` | Removes a mapping made with the matching map call; caller passes the original memory type and the mapping address. | Behavior when the connection closes first, or whether unmapping is required before close. |
| `IOConnectAddClient` | Informs one connection of a second; documented as rarely used. | The semantics of the association or its teardown order. |

Option values from XNU: `kIOMapAnywhere = 0x1`, `kIOMapReadOnly = 0x1000`. The
earlier field study observes mapping option 1 in the context fence-buffer
wrapper; that is the placement option, not read-only protection.

## Ownership model implied by the declarations

These are consequences of the public text, not observations of the live driver.

1. **Reference counting on connections.** Each CF-wrapped private object holds
   one connect handle. Releasing the object's last reference reaches
   `IOConnectRelease`; the kernel closes only when the final connection
   reference goes, so an extra reference elsewhere keeps the connection open.
2. **Notification ports own their Mach port.** Destroying the notification
   object invalidates the port; any async request still registered against it
   has no documented fate.
3. **Mappings belong to a task, not to the connection object.** The unmap call
   takes a task port and the address, so a mapping outlives the Objective-C or
   CF object that created it unless the caller unmaps it. The declarations do
   not state that closing the connection removes it.
4. **Ordering between unmap, notification destroy and close is unspecified.**
   Nothing public requires or forbids any order.

## Bounded finalizer and mapping observations

From the saved `cleanup-static` capture (exit 0, 2026-10-02 13:47 UTC, already
cited by the [ABI inventory](ioaccel-abi.md)); static reading only, nothing
invoked:

| Finalizer | Cleanup steps visible in its instructions | Not visible |
| --- | --- | --- |
| `ioAccelDeviceFinalize` | Frees a buffer unless it is null or the inline storage, calls `IOConnectRelease`, zeroes the stored handle. | Any unmap, or wait for outstanding work. |
| `ioAccelSharedFinalize` | Releases a stored block, calls `IOConnectRelease`, zeroes the handle, then under an unfair lock walks and frees a tracking list, calling an optional hook per entry. | Any unmap; whether the per-entry hook releases kernel-side allocations is unresolved. |
| `ioAccelCommandQueueFinalize` | Destroys the notification port if present, calls `IOConnectRelease`, zeroes the handle, atomically decrements a global context count. | Any cancellation of requests registered against the port, or wait for completion. |

Across all saved captures, the only function containing `IOConnectMapMemory` is
`IOAccelContextGetFenceBuffer` (two call sites). **No captured function
references `IOConnectUnmapMemory`.** This covers only the functions disassembled
in this study. It does not show the framework lacks an unmap path, so the
absence is not evidence that mappings leak. It means mapping reclamation, if
any, is either elsewhere or left to connection close or process exit, and that
is unverified.

Resulting gap: the three finalizers close connections but show no explicit
cancellation, drain or unmap. Whether the kernel reclaims state at the final
`IOConnectRelease` is a kernel-side guarantee this study cannot establish.

## Unresolved paths

| Question | Why it is open | Evidence needed |
| --- | --- | --- |
| Callback payload for the private command-queue notification | Public typedefs leave the extra arguments family-defined. | Bounded static data flow of the private callback consumer, or an own protocol on an experimental host. |
| Cancellation of an in-flight queue request | No public text covers destroying a port with outstanding async work. | Kernel-side behavior; not available from user-space declarations. |
| Whether mappings from `IOAccelContextGetFenceBuffer` are ever unmapped | No unmap appears in any captured function; the first mapping is not undone on the second mapping's failure path. | A framework-wide symbol-import check for `IOConnectUnmapMemory` and its callers, or close-time reclamation on a recoverable test machine. |
| Kernel guarantees at close | Public docs state the implicit close only. | Experimental host with recovery path. |
| Behavior under GPU hang or process exit | Not observable statically. | Future gated experiments below. |

## Future test gates (not authorized on this host)

Each gate needs an experimental environment with a recovery path, per
[AGENTS.md](../AGENTS.md); none may run on the working host.

| Gate | Measurable pass criterion |
| --- | --- |
| Close with live mapping | Process unmaps or exits; no stale mapping access, kernel panic or leaked memory after repeated cycles. |
| Notification port destroyed with pending request | No callback after destroy; request reclaimed; no dangling reference. |
| Release ordering | Every permutation of unmap, port destroy and last release leaves the device usable. |
| Error-path cleanup | Failed second mapping leaves no residual first mapping. |
| Process kill during submission | Kernel frees queue/mapping state; a new process starts cleanly. |

## Reproduction

The public facts above come from reading `sdk-IOKitLib.h`:

```sh
grep -n -B2 -A12 'IOConnectUnmapMemory64\|IOConnectRelease\|IONotificationPortDestroy' \
  out/metal-contract-detail/sdk-IOKitLib.h
shasum -a 256 out/metal-contract-detail/sdk-IOKitLib.h
```

Compare the digest with the entry in the local `local-index.json`; a missing
`out/` means the SDK header must be read from the installed SDK instead and the
SDK version recorded.

No Apple implementation code is incorporated; attribution does not grant a
license to reuse it, and Apple AMD binaries remain excluded from the finished
stack.
