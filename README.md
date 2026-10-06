# Independent Cezanne graphics driver

Initial target: AMD Cezanne `1002:1638`, revision `c9`, Ryzen 5 5600GT,
macOS 26.4.1. Current milestone: hardware discovery and graphics interface
specification. The first driver stages (passive attach, then read-only device
access) and a USB test boot are prepared; no independent driver has been
loaded yet.

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

## Check firmware headers

```sh
python3 tools/amdgpu_firmware.py path/to/green_sardine_*.bin
```

This reads AMD GPU microcode files and validates only the header fields Linux
v6.12 defines, plus the byte ranges its consumers use. Payloads are not decoded:
AMD's license forbids reverse engineering, decompiling or disassembling them.
Nothing is loaded or sent to hardware. Exit 0 means every file was accepted; 2
means at least one was rejected (see its `error`); 1 means a file could not be
read. Keep firmware under ignored `out/`; see
[firmware provenance](docs/firmware-provenance.md) for the pinned release.

## Prepare the USB test boot

`driver/kext/build.sh` builds `CezanneGPU.kext` from the IOKit adapter
(`driver/kext/`) and the hardware core (`driver/core/`). `tools/test_efi.py
build --stage N` derives a USB test EFI for an authorized stage from a copy of
the known-good OpenCore EFI, rejecting any change beyond the intended ones. Neither writes to
disks, NVRAM or EFI partitions. Follow [the test boot procedure](docs/test-boot.md);
`tools/update_stick.sh N` replaces the stick's EFI with stage N's test EFI
and verifies it;
experiments run only from that USB EFI. `tools/diag/build.sh` builds
`cezanne-diag`, which re-reads the driver's allowlisted registers through its
root-only diagnostic interface during a stage 4 or later test boot;
`--scratch-test` runs the stage 6 reversible write test, `--smu-query` the
stage 7 SMU version queries, `--gfxoff-disallow` the stage 8
`DisallowGfxOff` message and `--smu-metrics` the stage 9 metrics table;
`--psp-state` (stage 10, reads only) decodes the PSP ring mailbox and the
memory-hub apertures; `--psp-ring` (stage 11) creates and destroys the PSP
kernel-mode ring; `--psp-tmr` (stage 12) submits `SETUP_TMR` and
`DESTROY_TMR` through it; `--psp-sdma` (stage 13) loads the pinned SDMA0
firmware between them. Building the kext needs that firmware in ignored
`out/firmware-provenance/fw/` (see
[firmware provenance](docs/firmware-provenance.md)).

## Verify

```sh
python3 -m unittest discover -s tests -v
git diff --check
```

Tests cover PCI byte order and topology, unavailable fields, profiler schema
variants, malformed evidence, subprocess failures/timeouts, and overwrite
protection, and test-EFI derivation and rejection on synthetic OpenCore trees.
The hardware core's unit tests run against fake configuration space and
registers. The native inventory and kext tests compile on macOS with `xcrun`
(the kext is built, never loaded); they are skipped elsewhere. Host observations require independent system queries.

## Specification

- [Hardware baseline](docs/hardware-baseline.md)
- [Cezanne hardware blocks](docs/cezanne-hardware.md)
- [Target IP and firmware selection manifest](docs/cezanne-target-manifest.md)
- [Green Sardine firmware provenance and header validation](docs/firmware-provenance.md)
- [Cezanne shader compiler target](docs/shader-target.md)
- [USB test boot](docs/test-boot.md)
- [Tahoe graphics contracts and experiments](docs/graphics-contract.md)
- [Metal loader and factory investigation](docs/metal-loader-study.md)
- [Metal bundle admission and construction](docs/metal-admission.md)
- [Third-party Metal admission requirements](docs/metal-admission-requirements.md)
- [IOAccel communication ABI inventory](docs/ioaccel-abi.md)
- [IOAccel configuration and shared-memory fields](docs/ioaccel-shared-memory.md)
- [IOAccel notification and mapping lifecycle](docs/ioaccel-lifecycle.md)
- [IOAccel submission block ownership](docs/ioaccel-block-ownership.md)
- [IOAccel callback aliases and storage reuse](docs/ioaccel-buffer-reuse.md)
- [Generic XNU mapping and user-client teardown](docs/xnu-mapping-lifecycle.md)
- [Generic async replies and wake-port ownership](docs/xnu-async-replies.md)
- [Installed IOKit async dispatch and wrappers](docs/iokit-async-dispatch.md)
- [IOAcceleratorFamily2 queue reply production](docs/ioaccel-family-replies.md)
- [Milestones and discovery decisions](docs/discovery-plan.md)
