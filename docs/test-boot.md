# USB test boot

Status, 2026-10-05: prepared and verified offline; **not yet booted**. The test
EFI is assembled under ignored `out/test-efi/usb/` and waits for a USB drive.

The host keeps booting from its **known-good** OpenCore EFI on the internal
macOS disk. Driver experiments run only after choosing a separate **test EFI**
on a USB drive from the firmware boot menu. Unplugging the drive and powering
off restores the known-good boot. This is the experimental environment
[AGENTS.md](../AGENTS.md) requires before any driver is loaded.

## Rules

1. Never modify the internal EFI partition. After each test session, confirm
   it still matches the hashes recorded at build time (see
   [Return to the known-good boot](#return-to-the-known-good-boot)).
2. Experimental kexts load only from the test EFI's `Kernel → Add` list. Never
   install them into `/Library/Extensions`, load them with `kmutil` or
   `kextload`, or add them to the known-good EFI. Anything installed on the
   macOS volume would also load in the known-good boot.
3. The probe declines to attach unless the boot arguments contain
   `-cezanne-probe`, which only the test EFI sets and the known-good EFI deletes.
4. Only the current stage is authorized. **Stage 0** is passive: attach to the
   Cezanne PCI device and record what the registry already reports. Each later
   stage (register reads, then writes, firmware, memory mapping, interrupts)
   needs its own reviewed update to this document and the user's approval
   before it is built into the test EFI.
5. After any test boot from stage 1 on, shut down completely and wait a few
   seconds before booting the known-good EFI, so GPU and firmware state cannot
   carry over a warm reboot. Stage 0 touches no hardware.
6. Record every test boot: date, build, test EFI manifest time, what was
   chosen at each menu, what the screen showed, and the commands and outputs
   listed below. Preserve failures.

## What the test EFI changes

`tools/test_efi.py` derives the test EFI from a copy of the known-good EFI and
rejects the result unless the configurations differ in exactly these values:

| Setting | Known-good | Test | Why |
| --- | --- | --- | --- |
| `Kernel → Add` | includes NootedRed, SMCRadeonSensors | both removed; `CezanneProbe.kext` appended (`MinKernel` 25.0.0, x86_64) | NootedRed drives the iGPU through Apple's AMD kexts; SMCRadeonSensors reads GPU sensor registers itself. Nothing else may touch the GPU. |
| `NVRAM → Add → 7C436110-…:boot-args` | `-NRedRBPlus` | `-v keepsyms=1 debug=0x100 msgbuf=1048576 -cezanne-probe` | Verbose boot, symbolized panics, a halt on panic instead of a reboot, a larger kernel log buffer, and the probe interlock |
| `Misc → Security → AllowSetDefault` | `true` | `false` | The test picker cannot store a new default boot entry in shared NVRAM |

Everything else is byte-identical: ACPI tables, quirks, kernel patches, device
properties, UEFI drivers, OpenCore binaries and `PlatformInfo` (SMBIOS), so
macOS and iCloud see the same machine. The tool also enforces these properties:

- **Each boot resets the other's NVRAM.** Both EFIs share firmware NVRAM.
  Every value the test EFI sets differently must be in both configs'
  `NVRAM → Delete`. Today both delete `boot-args` and `csr-active-config` before
  adding their own, so neither boot inherits the other's arguments.
- **The test EFI never registers itself.** `Misc → Boot → LauncherOption` must
  be `Disabled`, so the USB OpenCore never adds a firmware boot entry and cannot
  become the default.
- **No kept kext depends on a removed one,** judged by each enabled kext's
  `OSBundleLibraries`.
- **Files outside the config are copied unchanged.** The test tree must equal the
  known-good tree minus the two removed bundles and the `Config.plist.bak-*`
  backups, plus the probe; every common file must hash identically.
- **The result passes OpenCore's own validator,** `ocvalidate` from the same
  release.

Both configs keep SIP fully enabled (`csr-active-config` `00000000`). OpenCore
injects kexts at boot, so the probe needs no signing and no SIP change.

## Known-good OpenCore provenance

The known-good EFI reports `opencore-version` `DBG-107-2026-03-20` (OpenCore 1.0.7
DEBUG). The official `OpenCore-1.0.7-DEBUG.zip` from `acidanthera/OpenCorePkg`
release `1.0.7` was downloaded into `out/test-efi/opencore/`. Its SHA-256,
`3644db831dd18344896d7a86077b8c338c0eaa01b1579d7fa00785598cac1f2b`, matches the
GitHub asset digest. The installed `OpenCore.efi`, `BOOTx64.efi` and
`OpenRuntime.efi` hash identically to that release. The installed
`ResetNvramEntry.efi` does **not** match it, so its origin is unknown;
`HfsPlus.efi` is not part of the release and was not checked. The test EFI
copies all of them unchanged. The release's `ocvalidate` reports no issues for
either config.

## The stage 0 probe

`driver/probe/` builds `CezanneProbe.kext` (`org.cezanne-driver.probe` 0.1.0).
It matches `IOPCIMatch` `0x16381002`, checks the interlock and the registry
vendor/device IDs, then logs and publishes the registry's `vendor-id`,
`device-id`, `revision-id`, subsystem IDs and `class-code` as `CezanneProbe …`
properties. Missing values are logged as unavailable. It does not open its
provider, touch PCI configuration space or BARs, change decoding or bus
mastering, register interrupts or join power management. It has no
`OSBundleRequired`, so it is not marked as needed for safe-mode boots.

`tests/test_probe_kext.py` builds it and checks:

- The source contains none of the hardware-facing IOKit calls on its denylist.
  Virtual calls are indirect in the binary, so this source check is the guard
  for them.
- Every direct call target in the binary is on a fixed passive list.
- Every undefined symbol resolves through `kmutil libraries` to a declared
  library on this host. `kmutil` silently omits a symbol it cannot resolve
  (observed with a planted missing symbol), so the test compares against `nm -u`.
  Pass `kmutil` a resolved path: it reported `No extension` for the same bundle
  under the `/var` symlink.

Building only produces files. The test never loads the kext. The linker warns
that `libkmod.a` was built for macOS 26.5 while the kext targets 26.0; the host
runs 26.4.1, and whether the kernel accepts it is checked at the first test boot.

## Build the test EFI

On this Mac, with the internal EFI mounted read-only only for the copy (the
`diskutil mount` command needs `sudo`):

```sh
sudo diskutil mount readOnly disk1s1     # internal EFI; identify it with diskutil list first
mkdir -p out/test-efi
cp -Rp /Volumes/EFI/EFI out/test-efi/known-good-EFI
diskutil unmount /Volumes/EFI
driver/probe/build.sh out/test-efi/probe
python3 tools/test_efi.py build --known-good out/test-efi/known-good-EFI \
  --kext out/test-efi/probe/CezanneProbe.kext --output out/test-efi/usb \
  --ocvalidate out/test-efi/opencore/DEBUG/Utilities/ocvalidate/ocvalidate
```

`build` refuses an existing output. If any check fails it exits 2 and leaves
the tree under `usb.rejected-<UTC time>`, never under the name the next steps
copy. `out/test-efi/usb/manifest.json` records the SHA-256 of every file in
both trees, the removed bundles and the `ocvalidate` output. The test EFI
contains the host's SMBIOS serials; keep it in ignored `out/`.

## Prepare the USB drive

Erasing destroys the drive's contents. Identify it carefully: it must be the
`external, physical` disk whose size matches the stick, never `disk0` or `disk1`.

```sh
diskutil list external physical
diskutil eraseDisk FAT32 CZTEST GPT /dev/diskN      # N from the line above
ditto --norsrc --noextattr out/test-efi/usb/EFI /Volumes/CZTEST/EFI
python3 tools/test_efi.py verify --manifest out/test-efi/usb/manifest.json --side test /Volumes/CZTEST/EFI
diskutil eject /dev/diskN
```

`verify` must report `"match": true` (exit 0). It ignores macOS `._*` and
`.DS_Store` files.

## Before the first test boot

- Make a Time Machine (or equivalent) backup. A panic during a disk write can
  damage the shared macOS volume.
- Turn on Remote Login (System Settings → General → Sharing) and confirm SSH
  works from another device. Without NootedRed, the display may stay on the
  firmware framebuffer or stay black; whether macOS 26 reaches a usable desktop
  without GPU acceleration is unknown.
- Find the motherboard's boot-menu key and confirm the internal disk remains the
  default boot device in firmware setup.

## Run a stage 0 test boot

1. Plug in the drive, power on, open the firmware boot menu and choose the USB
   entry (often `UEFI: <drive name>`).
2. The OpenCore picker looks the same as usual. Choose the macOS volume.
   Verbose text instead of the Apple logo confirms the test boot arguments.
3. If boot stops, photograph the screen. `debug=0x100` keeps a panic on screen.
   Power off, unplug the drive, and boot the known-good EFI to collect logs.

Once macOS is up (locally or over SSH), record:

```sh
sysctl kern.bootargs                               # must contain -cezanne-probe
kmutil showloaded --list-only | grep -i -E 'cezanne|nootedred|radeon'
ioreg -r -c CezanneProbe -l -w0
/usr/bin/log show --last boot --predicate 'eventMessage CONTAINS "CezanneProbe:"'
sudo dmesg | grep 'CezanneProbe:'                  # kernel buffer, if the log query is empty
```

Use `/usr/bin/log`: in zsh, a bare `log` is a shell builtin.

Stage 0 succeeds when:

- `org.cezanne-driver.probe` is loaded and NootedRed is not.
- `ioreg` shows a `CezanneProbe` instance under the Cezanne PCI device with
  `CezanneProbe vendor-id` `0x1002`, `device-id` `0x1638` and `revision-id`
  `0xc9`.
- The log shows the `attached passively` line.

It also records the unknowns this boot answers: what the display does without
NootedRed, and whether the 26.0-targeted kext loads.

Failure evidence: the OpenCore log `opencore-*.txt` at the root of the USB
drive, panic reports in `/Library/Logs/DiagnosticReports/*.panic` (read from
the known-good boot), and screen photos.

## Return to the known-good boot

Shut down, unplug the drive, and power on normally. Then confirm:

```sh
sysctl kern.bootargs                               # -NRedRBPlus, no -cezanne-probe
sudo diskutil mount readOnly disk1s1
python3 tools/test_efi.py verify --manifest out/test-efi/usb/manifest.json --side known_good /Volumes/EFI/EFI
diskutil unmount /Volumes/EFI
```

`verify --side known_good` compares the internal EFI with the build-time hashes,
ignoring the config backups the test EFI does not copy. At preparation time it
reported `"match": true` for all 32 files.

## Unknowns and limits

- Untested: whether the firmware lists and boots the USB FAT32 partition, the
  display state without NootedRed, the kext's acceptance, and where the
  probe's `IOLog` lines appear in the unified log.
- The preparation checks prove the configs and files differ only as intended
  and that OpenCore's validator accepts them; they do not prove the boot works.
- The known-good copy was taken from a read-write mount the user created; no
  file was written to it, and `verify` matched afterwards.
