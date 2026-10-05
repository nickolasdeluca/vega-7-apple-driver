#!/bin/sh
# Builds CezanneProbe.kext for x86_64 from this directory with the active
# Xcode SDK. Builds only: it never loads, installs or signs the kext.
# Usage: driver/probe/build.sh OUTPUT_DIR   (OUTPUT_DIR must not exist yet)
set -eu
[ $# -eq 1 ] || { echo "usage: $0 OUTPUT_DIR" >&2; exit 1; }
out=$1
[ ! -e "$out" ] || { echo "refusing to overwrite $out" >&2; exit 1; }
here=$(cd "$(dirname "$0")" && pwd)
sdk=$(xcrun --sdk macosx --show-sdk-path)
headers="$sdk/System/Library/Frameworks/Kernel.framework/Headers"
kext="$out/CezanneProbe.kext"
mkdir -p "$kext/Contents/MacOS" "$out/obj"
# IOPCIDevice is marked deprecated in favour of PCIDriverKit, which cannot host
# a GPU driver; only that warning is allowed.
common="-arch x86_64 -mmacosx-version-min=26.0 -isysroot $sdk -isystem $headers -nostdinc -mkernel
  -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT -Wall -Wextra -Werror
  -Wno-deprecated-declarations -O2"
xcrun clang $common -std=c11 -c "$here/kmod_info.c" -o "$out/obj/kmod_info.o"
xcrun clang++ $common -std=c++17 -fapple-kext -fno-rtti -fno-exceptions -fno-builtin \
  -c "$here/CezanneProbe.cpp" -o "$out/obj/CezanneProbe.o"
xcrun clang++ -arch x86_64 -mmacosx-version-min=26.0 -isysroot "$sdk" -nostdlib -Xlinker -kext \
  -Xlinker -export_dynamic "$out/obj/kmod_info.o" "$out/obj/CezanneProbe.o" -lkmodc++ -lkmod -lcc_kext \
  -o "$kext/Contents/MacOS/CezanneProbe"
cp "$here/Info.plist" "$kext/Contents/Info.plist"
echo "$kext"
