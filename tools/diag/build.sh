#!/bin/sh
# Builds the cezanne-diag command-line tool (x86_64, this SDK).
# Usage: tools/diag/build.sh OUTPUT_DIR   (OUTPUT_DIR must not exist yet)
set -eu
[ $# -eq 1 ] || { echo "usage: $0 OUTPUT_DIR" >&2; exit 1; }
out=$1
[ ! -e "$out" ] || { echo "refusing to overwrite $out" >&2; exit 1; }
here=$(cd "$(dirname "$0")" && pwd)
core="$here/../../driver/core"
mkdir -p "$out"
xcrun clang++ -arch x86_64 -mmacosx-version-min=26.0 -std=c++17 -Wall -Wextra -Werror -Wconversion -O2 \
  -I "$core" "$here/cezanne_diag.cpp" "$core/cezanne_core.cpp" -framework IOKit -o "$out/cezanne-diag"
echo "$out/cezanne-diag"
