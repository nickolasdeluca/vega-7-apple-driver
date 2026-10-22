#!/bin/sh
# Builds CezanneMTLDriver.bundle (Metal track M1, docs/test-boot.md) for x86_64
# with the active Xcode SDK and signs it ad hoc. Builds only: it never installs
# the bundle. Installing into /Library/GPUBundles is a user step in an M1 boot.
# Method IMPs take self and _cmd whether they use them or not, hence
# -Wno-unused-parameter.
# Usage: driver/metal/build.sh OUTPUT_DIR   (OUTPUT_DIR must not exist yet)
set -eu
[ $# -eq 1 ] || { echo "usage: $0 OUTPUT_DIR" >&2; exit 1; }
out=$1
[ ! -e "$out" ] || { echo "refusing to overwrite $out" >&2; exit 1; }
here=$(cd "$(dirname "$0")" && pwd)
bundle="$out/CezanneMTLDriver.bundle"
mkdir -p "$bundle/Contents/MacOS"
xcrun clang -arch x86_64 -mmacosx-version-min=26.0 -bundle -fno-objc-arc -Wall -Wextra -Werror -Wno-unused-parameter \
  -framework Foundation "$here/CezanneMTLDriver.m" -o "$bundle/Contents/MacOS/CezanneMTLDriver"
cp "$here/Info.plist" "$bundle/Contents/Info.plist"
codesign --force --sign - "$bundle" >/dev/null
echo "$bundle"
