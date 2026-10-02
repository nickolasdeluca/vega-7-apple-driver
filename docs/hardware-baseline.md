# Working host baseline

Observed on 2026-10-02, macOS 26.4.1. This records the existing working stack;
it does not demonstrate an independent driver. Collection submitted no GPU
work and performed no direct register access, installation, or boot changes.

## Reproduce and audit

Follow the [collector instructions](../README.md). The full capture used
`python3 tools/baseline.py --output out/baseline-verified-20261002` outside the
execution sandbox, as the logged-in user. Its report timestamp is
`2026-10-02T13:02:33.594999+00:00`; all six source queries exited 0 and parsed.
Raw evidence remains locally under that ignored directory. The report preserves
original stdout, stderr, command arguments, and exit status.

Independent queries were saved under `out/independent-20261002`:

```sh
ioreg -p IOService -r -n IGPU -d 1 -l -w 0
system_profiler SPDisplaysDataType
out/tools/metal-inventory
```

All three exited 0. Compared the collector's little-endian PCI decoding with
textual `ioreg` properties and System Profiler identifiers; compared memory,
both display modes, rotation, and Metal support with the text report. The
registry root's `OS Build Version` agrees with `sw_vers`. Metal's registry ID
matches the captured accelerator. No display serials or raw EDID are tracked.

## Observations

| Item | Observed value | Evidence |
| --- | --- | --- |
| OS / build | macOS 26.4.1 / 25E253 | `sw_vers`; registry `OS Build Version` |
| Architecture | x86_64 | `uname -m` |
| CPU brand | AMD Ryzen 5 5600GT with Radeon Graphics | `sysctl -n machdep.cpu.brand_string` |
| GPU PCI identifiers | vendor 1002, device 1638, revision c9; class 030000 | registry; display profiler |
| GPU location | `IGPU`, reported BDF `10:0:0` | registry `pcidebug` |
| Driver's GPU label | AMD Radeon RX Renoir Graphics | profiler, registry model, Metal |
| ROM label | 13-CEZANNE-019 | profiler `spdisplays_rom-revision` |
| Reported graphics memory | 2 GB; registry `VRAM,totalMB=2048` | profiler; PCI registry properties |
| Metal support | Metal 3 | profiler `spdisplays_mtlgpufamilysupport=spdisplays_metal3` |
| Metal recommended working set | 2,147,483,648 bytes | inventory; advisory device property |
| Metal flags | low power false, removable false, unified memory false | inventory; installed-driver properties |

CPU brand, model strings, PCI properties, and memory values are OS reports.
The Renoir label does not change the Cezanne target. In particular, the Metal
unified-memory flag is not proof of physically separate RAM, and the recommended
working set is not a measurement of usable GPU memory.

| Display | Logical pixel configuration | Other reported properties |
| --- | --- | --- |
| Main 24B1W1G5 | 1920 × 1080 at 60.00 Hz | Online; mirror off; 30-bit ARGB2101010; rotation supported |
| Second 24B1W1G5 | 1080 × 1920 at 60.00 Hz | Online; mirror off; 30-bit ARGB2101010; rotation 90° |

The rotated display's logical dimensions do not determine scanout timing or
physical link programming. Cable types, connector wiring, and active DCN pipe
assignments remain unverified.

## Registry and loaded stack

The parent chain is `AppleACPIPlatformExpert → PCI0 → AppleACPIPCI → GP17 →
IOPP → IGPU`. Below IGPU, the capture includes these services:

```text
IGPU (IOPCIDevice)
├─ AMDSupport
├─ AMDRadeonX5000_AMDRadeonHWServicesVega
│  └─ AMDRadeonX5000_AMDRadeonHWLibsX5000
├─ AMDRadeonX6000_AmdRadeonControllerNavi10
│  └─ AMDRadeonX6000_AmdRadeonFramebuffer (three registry services)
│     ├─ IOFramebufferUserClient / IOFramebufferSharedUserClient
│     └─ IODisplayConnect → AppleDisplay (two connected displays)
├─ AMDRadeonX5000_AMDVega10GraphicsAccelerator
│  └─ AMDAccelDevice / AMDAccelSharedUserClient / AMDAccelCommandQueue /
│     AMDAccelSurface / AMDAccel2DContext / IOAccelDisplayPipeUserClient2
└─ AMDRadeonX6000_AmdAgdcServices → AppleGraphicsDevicePolicy
```

This is a class summary, not an exhaustive list. IOService is a graph: the plist
contains repeated accelerator entries with the same registry ID. Three
framebuffer services do not mean three active monitors or three verified ports.
The collector's reconstructed name paths omit some location suffixes; registry
IDs distinguish repeated names and are valid only within this boot.

`kmutil showloaded` reports Lilu 1.7.2, NootedRed 0.9.0, AMD support/framebuffer/
accelerator/HW services 7.0.1, AMD HWLibs 1.0, IOGraphicsFamily 600,
IOAcceleratorFamily2 487.4.3, and IOSurface 393.5.7. These are the existing
reference stack; the finished implementation excludes Apple's AMD binaries.
The class names containing Navi10 or Vega10 do not identify this APU's physical
hardware generation. The [graphics contract](graphics-contract.md) records the
Metal plugin metadata and matching evidence.

## Failures and limits

The earlier restricted capture `out/baseline-sandbox-20261002` exited 2:
CPU query exit 1 (`Operation not permitted`), registry query exit 1
(`can't open file`), and loaded-component query exit 71. Its profiler returned
the GPU but omitted connected displays. Both that evidence and the unrestricted
capture are retained; restrictions are not interpreted as missing hardware.
The native inventory similarly returned no devices and exit 2 in the sandbox.

Physical UMA reservation, accessible GPU memory limits, firmware image versions,
hardware IP revisions, interrupt behavior, connector routing, shader execution,
shared-surface interoperability, and presentation contracts are unavailable or
untested. Current MSI and power properties reflect the existing driver's state,
not configuration instructions for a new driver. Next experiments and success
criteria are in [the hardware map](cezanne-hardware.md) and
[the graphics contract](graphics-contract.md).
