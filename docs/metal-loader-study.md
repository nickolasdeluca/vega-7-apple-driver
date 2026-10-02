# Tahoe Metal loader and factory investigation

The existing stack returns `GFX9_MtlDevice`, and runtime metadata identifies
candidate registration and initialization methods. This establishes useful
entry-point names and type encodings, **not a usable independent-driver ABI**.
Bundle admission, invocation order, ownership, and kernel communication remain
unresolved. Do not start a vendor plugin by calling these private methods.

Follow-up: the [admission study](metal-admission.md) now establishes ordered search
roots, named/principal-class precedence, the `_MTLDevice` class check and observed
enumeration call path. The [IOAccel inventory](ioaccel-abi.md) records partial
transport contracts. Our own bundle admission and a complete usable ABI remain
unproven; the text below records the earlier investigation's evidence limits.

## Experiment and evidence

Read-only study on 2026-10-02: macOS 26.4.1, build 25E253, x86_64;
PCI `1002:1638:c9`, host reporting Ryzen 5 5600GT. The existing
Lilu/NootedRed/Apple AMD stack remained the owner. Public `MTLCopyAllDevices`
enumeration was followed by Objective-C class/method metadata inspection and
symbol lookup. No private methods or resolved function pointers were invoked;
no custom user clients, GPU resources, queues, shaders or drawables were used.
Public enumeration may initialize the installed driver internally.

Raw evidence is local and ignored under `out/metal-loader-study/`. The final
reproducible probe's build, OS query and run all exited 0; stdout, stderr,
commands and timestamp are in `repro/capture.json` (13:30:10 UTC). The
registry correlation is independently recorded in the [baseline](hardware-baseline.md).

| Evidence | Observation | Limit |
| --- | --- | --- |
| Public enumeration | One device; registry ID `4294968382`; runtime class `GFX9_MtlDevice` | ID correlates within this boot only |
| Runtime superclass metadata | `GFX9_MtlDevice → GFX9AMD_MtlDevice → MTLIOAccelDevice → _MTLDevice → NSObject` | Inheritance does not specify initialization invariants |
| Registry property | `MetalPluginClassName=AMDMTLGFX9Device` | `objc_getClass` found no class with that name in this probe after enumeration; it is not the returned class |
| Installed bundle Info.plist | Identifier `com.apple.AMDRadeonX5000MTLDriver`; `NSPrincipalClass=GFX9_MtlDevice`; package type `BNDL`; version 7.0.1, short version 7.1.6 | Plist values do not prove which lookup key the Metal loader uses |
| Shared-cache image metadata | AMD image architecture `x86_64h`, minimum OS and SDK 26.4 | Installed development SDK is 26.5; private interfaces must be studied against the running build |
| Dependencies and imports | AMD image depends on Metal, private IOAccelerator and IOAccelMemoryInfo, IOSurface and other Apple frameworks; imports `MTLIOAccelDevice` and private ivars | The existing AMD plugin cannot be treated as an example of implementing only public `MTLDevice` |
| Metal image imports | `IOAccelDeviceCreateWithAPIProperty`, `IOAccelSharedCreate`, device/shared-memory helpers | Imports identify candidate dependencies, not a call trace or selector/structure ABI |

Apple documents `NSPrincipalClass` as identifying the bundle's principal class.
Its agreement with the observed class supports a factory hypothesis; it does
not establish that Metal reads this key or ignores `MetalPluginClassName`.
[NSPrincipalClass](https://developer.apple.com/documentation/bundleresources/information-property-list/nsprincipalclass)

The Metal image contains the strings `MetalPluginName` and
`MetalPluginClassName`. WebKit's pinned WebProcess sandbox source also explicitly
allows reading both registry properties. This supports their relevance to the
graphics stack; neither observation proves lookup order or search roots.
[WebKit sandbox, b8a7a626](https://github.com/WebKit/WebKit/blob/b8a7a626127c0010a557c9d6466fefd38d9477c1/Source/WebKit/WebProcess/com.apple.WebProcess.sb.in)

## Candidate factory metadata

These signatures were read with `class_copyMethodList` and
`method_getTypeEncoding`; no dispatch to the selectors took place. The encodings
below are observations on this architecture/build. Apple defines `@` as an
object, `#` as a class, `I` as an unsigned int and `v` as void; numeric offsets
are not a complete C header or ownership contract.
[Objective-C type encodings](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/ObjCRuntimeGuide/Articles/ocrtTypeEncodings.html)

| Owner and selector | Observed encoding | Candidate interpretation |
| --- | --- | --- |
| `+[GFX9AMD_MtlDevice registerDevices]` | `v16@0:8` | Class registration hook returning void |
| `-[GFX9_MtlDevice initWithAcceleratorPort:]` (also on its two immediate superclasses) | `@20@0:8I16` | Initializer returning an object, receiving a 32-bit unsigned argument |
| `-[MTLIOAccelService initWithAcceleratorPort:deviceClass:]` | `@28@0:8I16#20` | Associates a candidate service argument and device class |
| `-[MTLIOAccelServiceDescriptor initWithAcceleratorPort:deviceClass:]` | `@28@0:8I16#20` | Similar descriptor constructor |
| `-[MTLIOAccelServiceGlobalContext registerService:deviceClass:]` | `v28@0:8I16#20` | Candidate service/class registration |
| `-[MTLIOAccelServiceGlobalContext processPendingCreateIOAccelServiceRequests]` | `v16@0:8` | Candidate pending-service processing |
| `-[MTLIOAccelDevice deviceRef]`, `sharedRef` | `^{__IOAccelDevice=}16@0:8`, `^{__IOAccelShared=}16@0:8` | Private opaque connection objects |

The selector names suggest a registry-service-to-class-to-device path. We have
not observed those calls, established whether the unsigned argument is an
`io_service_t`, or resolved Mach-port retention, initialization failure and
teardown rules. Do not infer callable declarations from names alone.

`dlsym(RTLD_DEFAULT, ...)` found `MTLCreateDeviceWithID`,
`MTLCopyDeviceForRegistryID` and `MTLAddDevice`, owned by Metal.framework.
They appear in the installed SDK's export stub, but searches of its public Metal
headers found no declarations for them. Presence is not support for third-party
registration; their signatures, availability and call requirements are unknown.
Only the public enumeration/default-device functions were called.

## Admission policy and failed inspections

Logical image paths reported by dyld need not name standalone files. On this
host the AMD bundle's `Contents/MacOS` directory and Metal framework directory
do not contain their reported executables; the cache map lists both images.
`dyld_info -platform -linked_dylibs -exports -imports <logical-path>` successfully
inspected each cached image with exit 0 (`static.capture.json`). The AMD image is
`x86_64h`; selecting `-arch x86_64` failed.

Preserved failures include `nm`/`otool` against missing standalone binaries
(exit 1), bundle `codesign` inspection (exit 1, ambiguous bundle format), and
some `dyld_info` Metal section/Objective-C inspection attempts terminated by
SIGBUS (subprocess status -10). Partial output is not complete metadata.
These failures do not demonstrate an unsigned bundle, absent entitlements or
absence of a factory. Runtime metadata supplied the narrower evidence above.

For a hardened application, Apple's general library-validation rule admits
Apple-signed code or code with the application's Team ID; in-process plugins
inherit their host's entitlements. This is a deployment constraint to investigate,
not a demonstrated Metal-specific restriction or exception. No protections or
entitlements were changed during this study.
[Library validation](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.security.cs.disable-library-validation),
[Hardened Runtime](https://developer.apple.com/documentation/security/hardened-runtime)

Unresolved: supported bundle locations, accelerator matching filters, use of
the two registry keys versus principal class, factory invocation order,
system/plugin signing checks, sandbox permissions, and admission in ordinary
applications and WindowServer. The existence of `/Library/GPUBundles` alone
does not establish that this OS searches it.

## Minimum contract and feasibility gate

The first independent discovery experiment needs a truthful service identity,
device object, registry correlation, lifetime and failure handling. It must show
that normal public enumeration actually instantiates our implementation with
no Apple AMD image loaded. A mocked object handed directly to an application
does not pass discovery.

Rendering subsequently requires correct command queues/buffers, resources,
capability reporting, compilation/pipeline objects and synchronization;
IOSurface and drawable presentation add cross-process lifetime and compositor
requirements. This is a project requirements list derived from the public
[MTLDevice API](https://developer.apple.com/documentation/metal/mtldevice),
not a complete vendor protocol. Private base-class ivars and opaque IOAccel
objects make the existing class hierarchy insufficient to specify that protocol.

| Next experiment | Scope | Measurable success criterion |
| --- | --- | --- |
| Loader call-site and policy study | Read-only sources/metadata; do not invoke private factories or guess live user-client selectors | Establish lookup roots, property precedence, invocation order and validation branches with version-specific evidence; label any unavailable branch |
| IOAccel boundary inventory | Read-only permitted declarations and symbol metadata | List connection types, call/struct versions, shared-memory layouts, ownership and errors; absent definitions remain unknown |
| Synthetic discovery | Deferred until an experimental environment and recovery path are explicitly available | Public enumeration constructs our device; logs resolve admission and initialization; dyld evidence excludes every Apple AMD bundle; repeat creation and teardown without leaks/crashes |

**Feasibility remains open.** Candidate names are now known; independent bundle
admission and a usable IOAccel/Metal ABI have not been demonstrated. Continue
read-only investigation before substantial hardware implementation.

## Reproduce the runtime observation

Save the Objective-C example below as `out/metal-study/probe.m`. It inspects
metadata only after public enumeration. Capture each command with the existing
collector helper so stderr, exit status, timeout and partial output survive:

```python
import datetime, importlib.util, json
from pathlib import Path

spec = importlib.util.spec_from_file_location("baseline", "tools/baseline.py")
baseline = importlib.util.module_from_spec(spec)
spec.loader.exec_module(baseline)
output = Path("out/metal-study/capture-new")
output.mkdir(parents=True, exist_ok=False)
commands = {
    "os": ["/usr/bin/sw_vers"],
    "build": ["xcrun", "clang", "-fobjc-arc", "-Wall", "-Wextra", "-Werror",
              "-framework", "Foundation", "-framework", "Metal",
              "out/metal-study/probe.m", "-o", str(output / "probe")],
}
captured_at_utc = datetime.datetime.now(datetime.timezone.utc).isoformat()
report = {name: baseline.capture(name, argv, output)
          for name, argv in commands.items()}
if report["build"]["status"] == "ok":
    report["runtime"] = baseline.capture("runtime", [str(output / "probe")], output)
(output / "capture.json").write_text(json.dumps(
    {"captured_at_utc": captured_at_utc, "commands": report}, indent=2) + "\n")
```

Exit 2 from the probe means enumeration returned no devices, not that the GPU
or private interface is absent. Review captures before sharing. Runtime IDs and
full symbol output stay in ignored directories.

```objective-c
// Research example: metadata only after public Metal enumeration.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <objc/runtime.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

static NSArray *methods(Class cls) {
    NSMutableArray *items = [NSMutableArray array];
    unsigned int count = 0;
    Method *list = class_copyMethodList(cls, &count);
    for (unsigned int n = 0; n < count; n++) {
        NSString *name = NSStringFromSelector(method_getName(list[n]));
        if (![name containsString:@"initWithAcceleratorPort"] &&
            ![name isEqualToString:@"registerDevices"] &&
            ![name isEqualToString:@"registerService:deviceClass:"] &&
            ![name isEqualToString:@"processPendingCreateIOAccelServiceRequests"] &&
            ![name isEqualToString:@"deviceRef"] && ![name isEqualToString:@"sharedRef"]) continue;
        const char *type = method_getTypeEncoding(list[n]);
        [items addObject:@{@"selector": name,
            @"type_encoding": type ? [NSString stringWithUTF8String:type] : (id)[NSNull null]}];
    }
    free(list);
    return items;
}

int main(void) {
    @autoreleasepool {
        NSMutableArray *devices = [NSMutableArray array], *classes = [NSMutableArray array];
        for (id<MTLDevice> device in MTLCopyAllDevices()) {
            NSMutableArray *chain = [NSMutableArray array];
            for (Class cls = object_getClass(device); cls; cls = class_getSuperclass(cls))
                [chain addObject:NSStringFromClass(cls)];
            [devices addObject:@{@"name": device.name, @"registry_id": @(device.registryID),
                @"class_chain": chain}];
        }
        for (NSString *name in @[@"AMDMTLGFX9Device", @"GFX9_MtlDevice", @"GFX9AMD_MtlDevice",
            @"MTLIOAccelDevice", @"MTLIOAccelService", @"MTLIOAccelServiceDescriptor",
            @"MTLIOAccelServiceGlobalContext"]) {
            Class cls = objc_getClass(name.UTF8String);
            const char *image = cls ? class_getImageName(cls) : NULL;
            [classes addObject:@{@"name": name, @"present": @(cls != Nil),
                @"image": image ? [NSString stringWithUTF8String:image] : (id)[NSNull null],
                @"instance_methods": cls ? methods(cls) : @[],
                @"class_methods": cls ? methods(object_getClass(cls)) : @[]}];
        }
        NSMutableArray *symbols = [NSMutableArray array];
        for (NSString *name in @[@"MTLCreateSystemDefaultDevice", @"MTLCopyAllDevices",
            @"MTLCreateDeviceWithID", @"MTLCopyDeviceForRegistryID", @"MTLAddDevice"]) {
            void *symbol = dlsym(RTLD_DEFAULT, name.UTF8String);
            Dl_info info = {0};
            if (symbol) dladdr(symbol, &info);
            [symbols addObject:@{@"name": name, @"present": @(symbol != NULL),
                @"image": info.dli_fname ? [NSString stringWithUTF8String:info.dli_fname] : (id)[NSNull null]}];
        }
        NSError *error = nil;
        NSData *data = [NSJSONSerialization dataWithJSONObject:
            @{@"devices": devices, @"classes": classes, @"symbols": symbols}
            options:NSJSONWritingPrettyPrinted error:&error];
        if (!data) { fprintf(stderr, "%s\n", error.description.UTF8String); return 1; }
        if (fwrite(data.bytes, 1, data.length, stdout) != data.length || fputc('\n', stdout) == EOF) return 1;
        return devices.count ? 0 : 2;
    }
}
```
