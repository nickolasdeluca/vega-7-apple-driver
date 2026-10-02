# Independent Cezanne graphics driver

Initial target: AMD Cezanne `1002:1638`, revision `c9`, Ryzen 5 5600GT,
macOS 26.4.1. Current milestone: read-only hardware discovery and graphics
interface specification. No independent driver is implemented or loaded yet.

Read [AGENTS.md](AGENTS.md) before working on the project.
For current progress and where to resume, read [the project handoff](docs/handoff.md).

## Capture the baseline

On macOS with Python 3.9 or newer (standard library only):

```sh
python3 tools/baseline.py --output out/baseline-local
```

Use a new output directory for each run; existing captures are never overwritten.
The collector runs fixed read-only `sw_vers`, `uname`, `sysctl`, `ioreg`,
`system_profiler`, and `kmutil showloaded` queries. It does not open a custom
user client, access GPU registers, install drivers, or change boot settings.
It saves each command's raw stdout/stderr plus the command, status, and exit
code in `report.json`. Queries time out after 60 seconds. Exit 0 means the main
sources yielded parsed data; exit 2 means a partial report; exit 1 indicates an
output/setup error. Individual missing properties and display lists are marked
unavailable even when the containing source succeeded.

Run as the logged-in user. A restricted execution sandbox can block registry,
CPU, loaded-component, or display access. Preserve that capture; a separately
authorized run outside the sandbox can establish the full baseline. No `sudo`
is required by these tools. Empty results do not prove hardware is absent.

`out/` is ignored. Raw reports can contain display serial numbers, EDID,
system identity, process information, and runtime addresses. Review and remove
those before sharing; tracked documents contain only a reviewed summary.

## Inspect Metal discovery

With Xcode command-line tools and the macOS SDK:

```sh
mkdir -p out/tools
xcrun clang -fobjc-arc -Wall -Wextra -Werror -framework Foundation -framework Metal tools/metal_inventory.m -o out/tools/metal-inventory
out/tools/metal-inventory > out/metal-inventory.json 2> out/metal-inventory.stderr
```

This optional probe reports public Metal device properties, registry IDs, and
loaded graphics image paths. It submits no work, allocates no GPU resources,
and requests no drawables. Device enumeration may initialize Apple's existing
driver internally. Exit 2 means no device was returned; inspect stderr and
execution restrictions before interpreting that result. The working stack is
an observation reference; its AMD binaries are excluded from the finished stack.

## Verify

```sh
python3 -m unittest discover -s tests -v
git diff --check
```

Tests cover PCI byte order and topology, unavailable fields, profiler schema
variants, malformed evidence, subprocess failures/timeouts, and overwrite
protection. The native inventory test compiles and runs on macOS with `xcrun`;
it is skipped elsewhere. Host observations require independent system queries.

## Specification

- [Hardware baseline](docs/hardware-baseline.md)
- [Cezanne hardware blocks](docs/cezanne-hardware.md)
- [Target IP and firmware selection manifest](docs/cezanne-target-manifest.md)
- [Tahoe graphics contracts and experiments](docs/graphics-contract.md)
- [Metal loader and factory investigation](docs/metal-loader-study.md)
- [Metal bundle admission and construction](docs/metal-admission.md)
- [Third-party Metal admission requirements](docs/metal-admission-requirements.md)
- [IOAccel communication ABI inventory](docs/ioaccel-abi.md)
- [IOAccel configuration and shared-memory fields](docs/ioaccel-shared-memory.md)
- [IOAccel notification and mapping lifecycle](docs/ioaccel-lifecycle.md)
- [IOAccel submission block ownership](docs/ioaccel-block-ownership.md)
- [Milestones and discovery decisions](docs/discovery-plan.md)
