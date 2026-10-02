# Third-party Metal admission requirements

The loader path is understood sufficiently to identify its gates, but an
independent bundle's admission remains **unverified**. A Developer ID signature,
notarization, a recognized directory or successful PCI matching does not by
itself establish Metal or desktop compatibility. No supported third-party GPU
bundle installation/admission procedure has been established from the primary
references examined in these studies. This is a feasibility limit, not a proof
that every private vendor route is impossible.

This follows the [loader admission study](metal-admission.md). The
[shared-memory study](ioaccel-shared-memory.md) addresses the separate kernel
communication requirement.

## Host observations and evidence

Read-only study on 2026-10-02: macOS 26.4.1 build 25E253, x86_64, PCI
`1002:1638:c9`, host reporting Ryzen 5 5600GT; SDK 26.5. The installed graphics
stack remained the owner. Local evidence is preserved in ignored
`out/metal-contract-detail/`, with commands, UTC timestamps, stderr and exit
statuses. No trust helper or private user-client method was manually invoked,
and no installer ran. The installed stack's internal initialization during public
enumeration remains allowed observation. LLDB only inspected instructions in
our public-enumeration child.

| Capture | Observation |
| --- | --- |
| `policy-config` (14:10:55 UTC), exit 0 | `/System/Library/Sandbox/rootless.conf` lists `/Library/GPUBundles` with class `KernelExtensionManagement` |
| `bundle-metadata` (14:11:45 UTC), exit 0 | `/Library/GPUBundles` and the existing AMD Metal bundle directory report `restricted` flags |
| `sip-status`, `authenticated-root` (14:13:18 UTC), both exit 0 | `csrutil` reports SIP and authenticated-root protection enabled; this is reported status, not a complete audit of bootloader or runtime policy |
| `compositor-flags` (14:13:18 UTC), exit 0 | On-disk WindowServer executable reports code-signing flag `0x2000(library-validation)` |
| `compositor-signature` (14:11:45 UTC), exit 0 | Displayed WindowServer entitlements contain no `com.apple.security.cs.disable-library-validation` key; absence in this listing does not fully specify its effective runtime policy |
| `probe-signature` (14:11:45 UTC), exit 1 | `codesign` reports our development probe is not signed; successful enumeration through the existing Apple bundle is not a hardened-app admission test |
| `trust-confirm` (14:15:00 UTC), exit 0 | Static class-wrapper, rootless-check and authenticated-root helper control flow |

Rootless configuration text and directory flags are corroborating observations.
They do not define an accessible API that grants the protection class to a new
bundle. No file attributes, signatures, entitlements or protection settings were
changed. Full entitlement listings, process details and addresses stay untracked.

## Path trust is a kernel-policy question

The earlier resolver's `gpu_bundle_is_path_trusted` passes the candidate path and
`KernelExtensionManagement` to `rootless_check_trusted_class`. Static follow-up
shows that wrapper forwards the class, path and a sentinel descriptor to the
internal helper. Its path branch:

- Checks CSR mask 2. Pinned XNU identifies that mask as
  `CSR_ALLOW_UNRESTRICTED_FS`; the observed zero-result branch returns success
  before the remaining checks. This describes a conditional in inspected code,
  not a proposal to change the host's policy.
- Checks whether the path is on an authenticated root volume, using filesystem
  attributes. Its successful path returns success from the trust helper.
- Otherwise forms a Sandbox request with operation string `file-write-data`,
  the path and the supplied protection-class string. Its return calculation
  distinguishes request failure from the request's result word. The actual
  kernel rules deciding that result were not recovered or exercised.

The existing helper's shared-cache fallback remains as documented in the earlier
study. Neither that fallback nor a protected directory implies that our own
binary can become a shared-cache member or acquire the necessary path trust.
The exact installation authorization, signing requirements and class assignment
for third-party GPU bundles remain unknown.
[XNU CSR definitions, f6217f89](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/bsd/sys/csr.h),
[Filesystem attribute definitions](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/bsd/sys/attr.h)

Apple describes SIP as mandatory access controls on protected filesystem
locations, including for processes with administrative privileges. That supports
treating path trust separately from ordinary write permissions; it does not
document this private GPU-bundle policy.
[System Integrity Protection](https://support.apple.com/guide/security/system-integrity-protection-secb7ea06b49/web)

## Application policy is a separate gate

Apple's published library-validation rules require Apple signing or the main
executable's Team ID for libraries loaded by an application with that validation
enabled. An application can carry a documented entitlement for third-party
plugins. The driver cannot assume that arbitrary applications or WindowServer
carry it. Passing our own development application's policy would not establish
system-wide availability.
[Library validation entitlement](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.security.cs.disable-library-validation)

The earlier dyld study identified `F_CHECK_LV`. Pinned XNU's corresponding
`fcntl` branch calls `mac_file_check_library_validation`, showing a further
kernel policy boundary. It does not supply the accepting rule for a new GPU
bundle. The source revision is an attributed reference, not proof of an exact
source match to this host. A GPU-specific exception or vendor authorization must
be established explicitly rather than inferred from general bundle loading.
[XNU library validation dispatch](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/bsd/kern/kern_descrip.c)

Sandbox file access also differs from signature and path trust. Pinned WebKit's
WebProcess sandbox source allows reads below `/Library/GPUBundles` inside its
system-graphics policy block. This shows a concrete consumer anticipating the
directory; it neither grants installation rights nor proves that any application
can load an arbitrary bundle. Build conditions and effective host sandbox rules
were not tested.
[WebKit WebProcess policy, 5cc9b179](https://github.com/WebKit/WebKit/blob/5cc9b1797e0e294e250d524f30347722bb0c99d5/Source/WebKit/WebProcess/com.apple.WebProcess.sb.in)

DriverKit documents user-space driver extensions, deployment through
SystemExtensions and entitlement requirements. Those documented mechanisms do
not establish admission to the separate private Metal bundle loader observed
here. PCI ownership and user-client access cannot be used as substitutes for a
verified graphics integration contract.
[DriverKit](https://developer.apple.com/documentation/driverkit)

## Requirements and acceptance gates

| Gate | Established requirement | Evidence still needed |
| --- | --- | --- |
| Accelerator discovery | Matching accelerator service, plugin-name property, class resolution and `_MTLDevice` subclass check | Our service appears in the loader's match set and survives its filtering |
| Bundle resolution | Existing directory in the resolver roots and accepted path trust | Authorized third-party installation/class assignment and both accepted/denied cases |
| Code loading | Per-process signature and library-validation policy; sandbox access where applicable | Our binary admitted in each target process under its recorded normal policy, with any GPU-specific exception explained |
| Initialization | Constructor reaches a compatible kernel service; IOAccel base initialization expects private configuration | Correct creation, negotiation, error handling and teardown with our service |
| Desktop use | Discovery alone establishes no drawable/compositor contract | Shared surfaces, synchronization and presentation verified with WindowServer |

Our unsigned enumeration probe, an ordinary hardened application, a sandboxed
application and WindowServer are distinct acceptance cases. None is a substitute
for the others. No new admission case passed in this study.

Before claiming a distributable independent Metal driver, resolve both the
authorized path-trust procedure and the target processes' code-loading policy.
The next read-only work can examine primary installer/vendor metadata or an
applicable Apple integration contract. Actual positive/negative bundle admission
tests require the explicitly available experimental environment and recovery
path. If the authorized route cannot be established, keep independent Metal and
desktop integration as open feasibility gates before substantial bring-up.

## Reproduce the observations

Capture each command separately with `tools/baseline.py`'s `capture` helper,
preserving failures and a UTC timestamp. These commands query existing state:

```sh
csrutil status
csrutil authenticated-root status
cat /System/Library/Sandbox/rootless.conf
ls -ldO /Library/GPUBundles /System/Library/Extensions/AMDRadeonX5000MTLDriver.bundle
codesign -d --verbose=4 /System/Library/PrivateFrameworks/SkyLight.framework/Resources/WindowServer
codesign -d --entitlements - /System/Library/PrivateFrameworks/SkyLight.framework/Resources/WindowServer
```

For static trust inspection, use the public child and stopping procedure in the
[admission study](metal-admission.md), then `disassemble --name` for
`rootless_check_trusted_class`, `rootless_check_trusted_internal` and
`is_path_on_authenticated_root_volume`. Inspect control flow through returns;
ignore padding beyond returns. Do not call these helpers in the target. If
inspection is denied, preserve the failure and leave that evidence unavailable.
