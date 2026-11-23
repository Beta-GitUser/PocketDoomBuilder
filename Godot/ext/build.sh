#!/bin/sh
# PocketDoomBuilder — GDExtension spike build script.
# Compiles pd_core_extension.cpp + the C++ core into a shared library.
#
# Usage: sh Godot/ext/build.sh [output_path]
# Default output: Godot/ext/pd_core.so

set -e

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SRC="$ROOT/Android/app/src/main/cpp/src"
EXT="$ROOT/Godot/ext"
OUT="${1:-$EXT/pd_core.so}"

CORE_SRC="$SRC/map.cpp $SRC/wad.cpp $SRC/mapio.cpp $SRC/mapio_binary.cpp $SRC/mapio_udmf.cpp $SRC/view2d.cpp $SRC/png.cpp $SRC/helpdoc.cpp"

echo "Building GDExtension -> $OUT"
g++ -shared -fPIC -std=c++17 -O2 \
    -I "$ROOT/Godot/tools" \
    -I "$SRC" \
    "$EXT/pd_core_extension.cpp" \
    $CORE_SRC \
    -o "$OUT"

echo "Built: $OUT"
