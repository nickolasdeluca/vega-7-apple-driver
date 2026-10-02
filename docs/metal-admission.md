# Tahoe Metal bundle admission and construction

On this host, Metal's service loader resolves a trusted bundle, tries its named
class, falls back to its principal class, and requires an `_MTLDevice` subclass.
The resolver searches `/Library/GPUBundles` before `/System/Library/Extensions`.
This resolves the lookup questions from the [first loader study](metal-loader-study.md).
It does **not** demonstrate admission of our own bundle or supply a supported
vendor SDK. Trust, application library validation, and the kernel ABI remain
separate feasibility requirements.

## Experiment record and method

Read-only investigation on 2026-10-02, macOS 26.4.1 build 25E253, x86_64,
PCI `1002:1638:c9`, CPU reporting Ryzen 5 5600GT. The installed
Lilu/NootedRed/Apple AMD stack remained the reference owner. The SDK is 26.5.

The previous public enumeration/metadata example was built with debug information
and launched as our own child under LLDB. Inspection read loaded framework
instructions, symbols and data. A separate run stopped at the existing loader
while the child called `MTLCopyAllDevices`, recording its backtrace. No target
expressions, manual private function/selector calls, custom user-client opens,
GPU submissions, driver installation or configuration changes were performed.
Internal initialization by Apple's existing stack during enumeration is allowed
observation, not our implementation.

All raw output, stderr, command arguments, exit statuses and UTC capture times
are in ignored `out/metal-abi-study/`. Principal evidence:

| Capture | Result and evidence |
| --- | --- |
| `os` / `build` | Both exit 0; OS/build and native probe build |
| `loader-backtrace` (13:46:02 UTC) | Exit 0; `MTLCopyAllDevices → MTLRegisterDevices → -[MTLIOAccelServiceGlobalContext init] → getMetalPluginClassForService` |
| `admission-static` | Exit 0; property precedence, class check, service constructor and `MTLAddDevice` call sites |
| `trust-static` / `policy-detail` | Exit 0; resolver loop, trust helper and policy dependencies |
| `roots-reproduced` (13:48:00 UTC) | Exit 0; bounded debugger memory reads reproduce both resolver roots in order |
| `class-addresses` | Exit 0; runtime class pointer matches the loader's `_MTLDevice` reference in this boot |
| `connection-static` / `cleanup-static` | Exit 0; private IOAccel initializer and lifetime call sites; see [ABI inventory](ioaccel-abi.md) |

Two LLDB batches (`loader-path`, `trust-policy`) exited 1 because a requested
symbol spelling could not be resolved; batch execution stopped there. Their
partial disassembly and errors remain intact. Subsequent successful captures
resolve the relevant observations; a missing debug symbol is not absent code.
Some disassembly extends into padding/data after function returns; those bytes
are not interpreted as executable control flow. Full addresses, process IDs and
unfiltered framework output stay untracked.

## Established lookup order

The following is an interpretation of the captured x86_64 instructions, not
copied implementation code. The observed backtrace establishes that enumeration
reaches the helper; it does not prove every branch ran for this device.

1. `MTLIOAccelServiceGlobalContext` registers an `IOServicePublish` matching
   notification for `IOAccelerator`, iterates existing matches and resolves
   each service's device class.
2. `getMetalPluginClassForService` reads `MetalPluginName`, checks that it is a
   CFString, converts it, and calls `gpu_bundle_find_trusted` with an output path
   buffer. Resolution failure returns no class.
3. The resolver, owned by `libsystem_sandbox.dylib`, forms
   `<root>/<name>.bundle` for the two ordered roots above. It checks that each
   candidate exists as a directory and passes its trust helper. A missing
   candidate permits the next root; some inspection/trust errors terminate
   resolution. It returns zero with a copied path on the successful branch.
4. Metal creates an `NSBundle` for that path. A string-valued
   `MetalPluginClassName` is tried with `classNamed:`. If absent, malformed or
   unresolved, Metal asks for `principalClass`.
5. The selected class is tested with `isSubclassOfClass:` against `_MTLDevice`.
   The captured class-reference pointer was cross-checked with public runtime
   metadata. The check is not specifically against `MTLIOAccelDevice`.

Apple's public bundle contracts explain the meaning of the named/principal-class
operations, but do not promise Metal vendor admission or stability of this private
path. The earlier mismatch between registry `AMDMTLGFX9Device` and returned
`GFX9_MtlDevice` is consistent with the observed fallback; the selected branch
for that particular name was not traced.
[Bundle class lookup](https://developer.apple.com/documentation/foundation/bundle/classnamed(_:)),
[Principal class metadata](https://developer.apple.com/documentation/bundleresources/information-property-list/nsprincipalclass)

## Trust and code-loading policy

`gpu_bundle_is_path_trusted` calls
`rootless_check_trusted_class(path, "KernelExtensionManagement")`. If the
result is 1, it checks `_dyld_shared_cache_contains_path` and converts cache
membership into its success/failure result. The resolver accepts the helper's
zero result; a -1 error aborts the search. Thus a recognized search directory
alone does not admit an arbitrary bundle placed inside it.

Static inspection of `rootless_check_trusted_internal` shows dependencies on
`csr_check`, authenticated-root-volume inspection, file attributes, and a Sandbox
policy request. These observations establish a protection-sensitive trust layer.
They do not establish which signing identity, entitlement, installation approval
or protected-file metadata would admit our bundle. No trust helper was called
manually, no candidate installed, and no protection setting changed. The exact
kernel trust policy and the applicability of its branches to our future bundle
remain unresolved.

After bundle path trust, code loading still has the host process's requirements.
Apple documents that hardened applications normally accept Apple-signed libraries
or those with the application's Team ID, and plugins inherit the host's
entitlements. Pinned dyld source's file-mapping path registers code signatures
and uses `F_CHECK_LV` to preflight library validation, rejecting a denied mapping.
This source supports the separate validation layer; its revision is not a claim
that every cache/prebuilt path on this host executes that exact function.
[Library validation](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.security.cs.disable-library-validation),
[Hardened Runtime](https://developer.apple.com/documentation/security/hardened-runtime),
[dyld Loader.cpp, fd8d0c4d](https://github.com/apple-oss-distributions/dyld/blob/fd8d0c4d52320ebf64db34f3cb280310d905c5ae/dyld/Loader.cpp)

A bundle that loads in our development process has not demonstrated loading in
ordinary third-party applications or WindowServer. Their relevant validation
and sandbox policies are not established by this experiment.

## Construction and lifetime observations

The captured `MTLIOAccelService` initializer retains the accelerator service,
gets its registry ID, creates interest notifications and maintains service
collections. Its code consults compatibility/removability/ejection state; the
complete filtering contract has not been tested.

On its construction branch it allocates the resolved class, sends
`initWithAcceleratorPort:`, then, for a non-nil device, stores a weak reference,
sends `_setAcceleratorService:` and calls `MTLAddDevice`. The unsigned initializer
argument flows to public IOKit registry/retain/open operations as a service
handle. It is distinct from the connection subsequently returned by
`IOServiceOpen`. This narrows the earlier opaque argument without defining
every initializer invariant or object ownership rule.

`MTLIOAccelDevice`'s initializer calls
`IOAccelDeviceCreateWithAPIProperty(service, "Metal")` and queries configuration,
registry identity and memory properties. Consequently, device construction can
require a real compatible kernel service even before queues or resources are
explicitly requested. A bare fake registry node plus empty `MTLDevice` methods
would not implement this observed initialization path.

The earlier `+[GFX9AMD_MtlDevice registerDevices]` metadata remains an observed
vendor method. This study does not show that every plugin must implement that
hook; the demonstrated Metal service-construction path uses the class initializer.
See [IOAccel ABI inventory](ioaccel-abi.md) for the recovered transport details
and remaining shared-memory/kernel requirements.

## Reproduce without installing a plugin

Save the Objective-C example from [the first study](metal-loader-study.md) as
`out/metal-abi-repro/probe.m`. Build it with `-g -O0` and the previous warning
flags/frameworks. Run the following LLDB batch through `tools/baseline.py`'s
`capture` helper, retaining stdout, stderr, exit status and a UTC timestamp:

```sh
xcrun lldb --no-lldbinit --batch \
  -o 'settings set target.disable-aslr false' \
  -o 'breakpoint set --file probe.m --line 48' \
  -o run \
  -o 'disassemble --name getMetalPluginClassForService' \
  -o 'disassemble --name gpu_bundle_find_trusted' \
  -o 'disassemble --name gpu_bundle_is_path_trusted' \
  -o 'disassemble --name "-[MTLIOAccelService initWithAcceleratorPort:deviceClass:]"' \
  -o quit out/metal-abi-repro/probe
```

The line breakpoint is tied to that exact example and stops after enumeration.
To record the loader backtrace instead, use
`breakpoint set --name getMetalPluginClassForService` before `run`, followed by
`thread backtrace --count 12`. A successful LLDB exit does not alone prove that
the intended breakpoint was hit; inspect the stop reason and frames. If launch
or inspection is denied, preserve the result and do not bypass protections.

For ordered-root reproduction, save the LLDB Python example below in an ignored
directory and add `-o 'script exec(open("<example-path>").read())'` after `run`
in the line-breakpoint batch. It reads the resolver's instructions and two pointer
slots, with bounded string reads; it executes no functions in the target. Its
operand pattern and two-entry count are specific to this observed x86_64 build.
Assert a changed layout as unavailable rather than reusing old runtime addresses.

## Next experiments and gates

| Experiment | Allowed scope | Success criterion |
| --- | --- | --- |
| Admission policy specification | Read-only documentation/source/metadata | Establish required trust/signing/installation conditions for a third-party bundle under normal application policies; distinguish a documented route from an unresolved or unsupported one |
| IOAccel structure inventory | Offline definitions and static wrapper data flow | Resolve configuration versions/fields and shared-memory ownership, layout and ordering; unknown transport rows stay explicitly incomplete |
| Synthetic discovery | Deferred to explicitly available experimental environment and recovery path | Public enumeration constructs our device through the loader; explain trust and initialization; no Apple AMD images loaded; repeat creation/teardown and test denied admission |
| Application compatibility | Deferred beyond own discovery/submission | Test development app, ordinary hardened app, sandboxed app and compositor separately; correct lifetime and presentation without changing host protections as an implicit prerequisite |

No admission or hardware milestone is passed by this study. The concrete advance
is the version-specific lookup/constructor path and its separate trust gate.

```python
# Run with LLDB's script command after stopping the repository probe.
# Reads target instructions and data; executes no target functions.
import json,re,lldb
study_target=lldb.debugger.GetSelectedTarget()
study_process=study_target.GetProcess()
assert study_process.GetState()==lldb.eStateStopped
assert study_target.GetTriple().startswith('x86_64') and study_target.GetAddressByteSize()==8
contexts=study_target.FindFunctions('gpu_bundle_find_trusted')
assert contexts.GetSize()==1, 'resolver symbol unavailable or ambiguous'
instructions=contexts.GetContextAtIndex(0).GetSymbol().GetInstructions(study_target)
assert any(i.GetMnemonic(study_target)=='cmpq' and i.GetOperands(study_target)=='$0x10, %rbx'
           for i in instructions), 'unexpected resolver loop bound'
root_table=None
for instruction in instructions:
    if 'GPU_STANDARD_BUNDLE_PATHS' not in instruction.GetComment(study_target): continue
    operand=re.fullmatch(r'(0x[0-9a-f]+)\(%rip\), %rax',instruction.GetOperands(study_target))
    assert operand and instruction.GetMnemonic(study_target)=='leaq', 'unexpected resolver instruction'
    root_table=instruction.GetAddress().GetLoadAddress(study_target)+instruction.GetByteSize()+int(operand.group(1),16)
assert root_table is not None, 'path table not located'
# Two entries are used by the inspected x86_64 loop (offset 0 and 8, ends at 16).
roots=[]
for offset in (0,8):
    error=lldb.SBError()
    pointer=study_process.ReadUnsignedFromMemory(root_table+offset,8,error)
    assert error.Success(), str(error)
    value=study_process.ReadCStringFromMemory(pointer,1024,error)
    assert error.Success(), str(error)
    roots.append(value)
print(json.dumps({'resolver_roots_in_order':roots}))
```
