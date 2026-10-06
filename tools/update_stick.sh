#!/bin/sh
# Replaces the EFI folder on the USB test stick with a built test EFI and
# verifies the copy against its manifest (docs/test-boot.md, "Prepare the USB
# drive"). The user runs it; it touches only /Volumes/CZTEST on an external
# disk and never the internal EFI.
# Usage: tools/update_stick.sh STAGE   (copies out/test-efi/usb-stageSTAGE)
set -eu
volume=/Volumes/CZTEST
[ $# -eq 1 ] || { echo "usage: $0 STAGE" >&2; exit 2; }
case $1 in
'' | *[!0-9]*) echo "stage must be a number: $1" >&2; exit 2 ;;
esac
root=$(cd "$(dirname "$0")/.." && pwd)
build="$root/out/test-efi/usb-stage$1"
[ -d "$build/EFI" ] && [ -f "$build/manifest.json" ] ||
  { echo "no built test EFI at $build (see docs/test-boot.md)" >&2; exit 1; }

# The stick: mounted at exactly $volume, named CZTEST, on an external disk.
[ -d "$volume" ] || { echo "$volume is not mounted; plug in the stick" >&2; exit 1; }
info=$(diskutil info -plist "$volume") || { echo "diskutil cannot read $volume" >&2; exit 1; }
field() { printf '%s' "$info" | plutil -extract "$1" raw - 2>/dev/null || echo "?"; }
[ "$(field MountPoint)" = "$volume" ] && [ "$(field VolumeName)" = CZTEST ] ||
  { echo "$volume is not the CZTEST volume" >&2; exit 1; }
[ "$(field Internal)" = false ] && [ "$(field RemovableMediaOrExternalDevice)" = true ] ||
  { echo "$volume is not on an external disk; refusing" >&2; exit 1; }

echo "stage $1 -> $volume ($(field DeviceIdentifier))"
rm -rf "$volume/EFI"
ditto --norsrc --noextattr "$build/EFI" "$volume/EFI"
python3 "$root/tools/test_efi.py" verify --manifest "$build/manifest.json" --side test "$volume/EFI"
