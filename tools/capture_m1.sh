#!/bin/bash
# Runs and captures one Metal track M1 boot (docs/test-boot.md, "Metal track
# M1") into out/test-efi/NAME. The user runs it once macOS is up on an M1 test
# EFI. It records the boot arguments, SIP state and our registry nodes,
# installs CezanneMTLDriver.bundle into /Library/GPUBundles (sudo), runs the
# admission probe, saves the bundle's message log and the unified log of the
# run, then removes the bundle again (sudo) and records that the directory is
# empty. The removal runs even if an earlier step fails. It writes only inside
# the new folder and /Library/GPUBundles/CezanneMTLDriver.bundle.
# Usage: tools/capture_m1.sh NAME   e.g. tools/capture_m1.sh boot-39-m1-rung1
set -u -o pipefail
[ $# -eq 1 ] || { echo "usage: $0 NAME" >&2; exit 2; }
case $1 in
boot-[0-9]*-m1-rung[12]) ;;
*) echo "name must look like boot-N-m1-rungR: $1" >&2; exit 2 ;;
esac
case $1 in
*[!a-z0-9-]*) echo "name must look like boot-N-m1-rungR: $1" >&2; exit 2 ;;
esac
root=$(cd "$(dirname "$0")/.." && pwd)
bundle="$root/out/metal-m1/CezanneMTLDriver.bundle"
probe="$root/out/metal-m1/metal-admission-probe"
target=/Library/GPUBundles/CezanneMTLDriver.bundle
[ -d "$bundle" ] || { echo "no $bundle; build it with driver/metal/build.sh" >&2; exit 1; }
[ -x "$probe" ] || { echo "no $probe (docs/test-boot.md, Metal track M1)" >&2; exit 1; }
dir="$root/out/test-efi/$1"
[ ! -e "$dir" ] || { echo "refusing to overwrite $dir" >&2; exit 1; }
mkdir -p "$dir"
echo "capturing into $dir"

sysctl -n kern.bootargs > "$dir/bootargs.txt" 2>&1
echo "boot-args: $(cat "$dir/bootargs.txt")"
if ! grep -q "cezanne-metal=1" "$dir/bootargs.txt"; then
  echo "no cezanne-metal=1 in the boot arguments; this is not an M1 boot. Nothing installed." >&2
  exit 1
fi
csrutil status > "$dir/csrutil.txt" 2>&1
/usr/sbin/nvram csr-active-config > "$dir/nvram-csr.txt" 2>&1
echo "$(cat "$dir/csrutil.txt")"
kmutil showloaded --list-only 2> "$dir/kmutil.stderr" | grep -i -E 'cezanne|nootedred|radeon|iographics|ioaccelerator' > "$dir/kmutil.txt"
ioreg -r -c CezanneGPU -a > "$dir/ioreg-gpu.plist" 2> "$dir/ioreg-gpu.stderr"
ioreg -r -c CezanneAccelerator -l > "$dir/ioreg-accelerator.txt" 2>&1
ioreg -r -c IOAccelerator -l > "$dir/ioreg-all-accelerators.txt" 2>&1
ls -ldO /Library/GPUBundles > "$dir/gpubundles-before.txt" 2>&1
ls -la /Library/GPUBundles >> "$dir/gpubundles-before.txt" 2>&1

remove() {
  sudo rm -rf "$target"
  ls -la /Library/GPUBundles > "$dir/gpubundles-after.txt" 2>&1
  if [ -e "$target" ]; then
    echo "WARNING: $target is still present; remove it in an M1 boot" | tee -a "$dir/gpubundles-after.txt" >&2
  else
    echo "removed $target"
  fi
}
trap remove EXIT

start=$(date '+%Y-%m-%d %H:%M:%S')
echo "$start" > "$dir/start.txt"
install=0
sudo ditto "$bundle" "$target" > "$dir/install.txt" 2>&1 || install=$?
echo "$install" > "$dir/install-exit.txt"
echo "install exit status: $install"
codesign -dv "$target" >> "$dir/install.txt" 2>&1

status=0
CEZANNE_M1_LOG="$dir/bundle.log" "$probe" > "$dir/probe.json" 2> "$dir/probe.stderr" || status=$?
echo "$status" > "$dir/probe-exit.txt"
cat "$dir/probe.json"
echo "probe exit status: $status"

/usr/bin/log show --start "$start" --info --debug --style compact \
  --predicate 'process == "metal-admission-probe" OR process == "MTLCompilerService" OR eventMessage CONTAINS[c] "GPUBundles" OR eventMessage CONTAINS[c] "Cezanne"' \
  > "$dir/unified-log.txt" 2> "$dir/unified-log.stderr"
echo "saved: $(cd "$dir" && ls | tr '\n' ' ')"
exit "$status"
