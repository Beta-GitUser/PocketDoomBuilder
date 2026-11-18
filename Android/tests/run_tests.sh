#!/bin/sh
# Host-side checks for PocketDoomBuilder's platform-independent code and UI.
#
# Everything here runs with plain g++ (no Android SDK, NDK or CMake), which is
# what CI and the development sandbox have available:
#
#   1. unit tests   (Android/tests/pdb_tests.cpp)  — WAD/map codecs, view math,
#                                                    hit testing, PNG decoder
#   2. headless UI  (Android/tools/render_preview.cpp) — runs the real ImGui
#                                                    shell and writes a PNG
#
# Usage:  sh Android/tests/run_tests.sh [--preview]
#
# Set FUZZ=1 to also hammer the PNG decoder with truncated/corrupted icons
# (~20 s); run_tests.sh normally only checks that the decoder terminates for a
# sample of truncations.

set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SRC="$ROOT/Android/app/src/main/cpp/src"
IMGUI="$ROOT/Android/third_party/imgui"
ASSETS="$ROOT/Android/app/src/main/assets"
OUT="$ROOT/Android/tests/build"

mkdir -p "$OUT"

IMGUI_SRC="$IMGUI/imgui.cpp $IMGUI/imgui_draw.cpp $IMGUI/imgui_tables.cpp $IMGUI/imgui_widgets.cpp"
CORE_SRC="$SRC/map.cpp $SRC/wad.cpp $SRC/mapio.cpp $SRC/mapio_binary.cpp \
$SRC/mapio_udmf.cpp $SRC/view2d.cpp $SRC/png.cpp"

sh "$ROOT/Android/tests/check_build_files.sh"

echo "== building unit tests"
g++ -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter \
    -DPDB_ASSET_DIR="\"$ASSETS\"" \
    -DPDB_ICON_DIR="\"$ASSETS\"" \
    -I "$SRC" \
    "$ROOT/Android/tests/pdb_tests.cpp" $CORE_SRC \
    -o "$OUT/pdb_tests"

echo "== running unit tests"
"$OUT/pdb_tests"

if [ "${FUZZ:-0}" = "1" ]; then
    echo "== fuzzing the PNG decoder"
    g++ -std=c++17 -O2 -I "$SRC" "$ROOT/Android/tests/decode_fuzz.cpp" "$SRC/png.cpp" \
        -o "$OUT/decode_fuzz"
    "$OUT/decode_fuzz" "$ASSETS"/icons/*.png
fi

echo "== checking the APK verifier"
python3 "$ROOT/Android/tests/test_verify_apk.py"

if [ "${1:-}" = "--preview" ] || [ "${2:-}" = "--preview" ]; then
    echo "== building headless preview renderer"
    UI_SRC="$SRC/editor.cpp $SRC/icons.cpp $SRC/render2d.cpp $SRC/theme.cpp $SRC/ui.cpp \
$SRC/ui_dialogs.cpp $SRC/ui_mapview.cpp $SRC/ui_panels.cpp $SRC/ui_toolbar.cpp"
    g++ -std=c++17 -O2 -Wall \
        -DPDB_ASSET_DIR="\"$ASSETS\"" \
        -I "$SRC" -I "$IMGUI" \
        "$ROOT/Android/tools/render_preview.cpp" $CORE_SRC $UI_SRC $IMGUI_SRC \
        -o "$OUT/render_preview"

    echo "== rendering shell preview"
    # Render at the real UI size (the layout metrics depend on it), keep the
    # committed screenshot at half scale so the repository stays small.
    "$OUT/render_preview" "$OUT/preview-full.png" 1350 933
    # Round-trip through the downscaler at factor 1 first: it re-encodes the
    # PNG, so a decoder regression shows up here rather than as a corrupt
    # committed screenshot.
    python3 "$ROOT/Android/tools/downscale_png.py" \
        "$OUT/preview-full.png" "$OUT/preview-scaled.png" 1 > /dev/null
    echo "downscaler round-trip ok"
    python3 "$ROOT/Android/tools/downscale_png.py" \
        "$OUT/preview-full.png" "$ROOT/Android/tools/preview.png" 2
    echo "preview written to Android/tools/preview.png"
fi

echo "OK"
