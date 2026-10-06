#!/bin/bash
# Captures one test boot's evidence into out/test-efi/NAME (docs/test-boot.md,
# "Run a test boot"): the boot arguments, the loaded kexts, the cezanne-diag
# output (run with sudo and the given flags) and the driver's ioreg entry. The
# user runs it once macOS is up on the test EFI. It writes only inside the new
# folder and refuses an existing one, so earlier captures are never touched.
# Usage: tools/capture_boot.sh NAME [cezanne-diag flags...]
#   e.g. tools/capture_boot.sh boot-24-stage17 --gfxoff-disallow --gart-ih --psp-state
set -eu -o pipefail
[ $# -ge 1 ] || { echo "usage: $0 NAME [cezanne-diag flags...]" >&2; exit 2; }
case $1 in
boot-[0-9]*-stage[0-9]*) ;;
*) echo "name must look like boot-N-stageM: $1" >&2; exit 2 ;;
esac
case $1 in
*[!a-z0-9-]*) echo "name must look like boot-N-stageM: $1" >&2; exit 2 ;;
esac
name=$1
shift
root=$(cd "$(dirname "$0")/.." && pwd)
diag="$root/out/diag/cezanne-diag"
[ -x "$diag" ] || { echo "no $diag; build it with tools/diag/build.sh out/diag" >&2; exit 1; }
dir="$root/out/test-efi/$name"
[ ! -e "$dir" ] || { echo "refusing to overwrite $dir" >&2; exit 1; }
mkdir -p "$dir"

echo "capturing into $dir"
sysctl -n kern.bootargs > "$dir/bootargs.txt" 2>&1 || true
echo "boot-args: $(cat "$dir/bootargs.txt")"
grep -q "cezanne-stage=" "$dir/bootargs.txt" || echo "warning: no cezanne-stage in the boot arguments; is this the test EFI?" >&2
kmutil showloaded --list-only 2> "$dir/kmutil.stderr" | grep -i -E 'cezanne|nootedred|radeon' > "$dir/kmutil.txt" || true

# The diag output is shown as it runs and saved; its exit status is kept.
status=0
sudo "$diag" "$@" 2>&1 | tee "$dir/diag.txt" || status=$?
echo "$status" > "$dir/diag-exit.txt"

ioreg -r -c CezanneGPU -a > "$dir/ioreg.plist" 2> "$dir/ioreg.stderr" || true
echo "saved: $(cd "$dir" && ls | tr '\n' ' ')"
echo "cezanne-diag exit status: $status"
exit "$status"
