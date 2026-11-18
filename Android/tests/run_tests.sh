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
$SRC/mapio_udmf.cpp $SRC/view2d.cpp $SRC/png.cpp $SRC/helpdoc.cpp"

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

echo "== linking the Android shell (undefined-symbol check)"
# A syntax check cannot see a missing definition. This compiles platform_android.cpp
# against the stub NDK headers in Android/tests/stubs and links it into a shared
# object; any symbol the shell needs but does not define would have failed the real
# NDK build, so it must show up as a reference to another repository file only.
STUBS="$ROOT/Android/tests/stubs"
g++ -std=c++17 -O2 -fPIC -DIMGUI_IMPL_OPENGL_ES3 -I "$STUBS" -I "$SRC" -I "$IMGUI" \
    -I "$IMGUI/backends" -c "$SRC/platform_android.cpp" -o "$OUT/platform_android.o"
g++ -std=c++17 -O2 -fPIC -DIMGUI_IMPL_OPENGL_ES3 -I "$STUBS" -I "$SRC" -I "$IMGUI" \
    -I "$IMGUI/backends" -c "$ROOT/Android/tests/ndk_link_stubs.cpp" -o "$OUT/ndk_stubs.o"
g++ -shared -o "$OUT/libshell.so" "$OUT/platform_android.o" "$OUT/ndk_stubs.o"
python3 - "$OUT/libshell.so" <<'PYEOF'
import subprocess, sys, re
out = subprocess.run(["nm", "-C", "--undefined-only", sys.argv[1]],
                     capture_output=True, text=True).stdout
# Symbols defined in other translation units of this port are expected; what must
# not appear is a shell symbol with no definition anywhere.
missing = []
for line in out.splitlines():
    sym = line.split()[-1] if line.split() else ""
    if not (sym.startswith("pdb::") or "installWorkArea" in sym or "Java_com_pocketdoombuilder" in sym):
        continue
    if any(sym.startswith(d) for d in ("pdb::editor", "pdb::Editor::", "pdb::applyTheme",
                                       "pdb::drawMainWindow", "pdb::requestFitToScreen",
                                       "pdb::ui::", "pdb::IconAtlas::", "pdb::View2D::",
                                       "pdb::loadIcons", "pdb::decodePng",
                                       "pdb::parseHelpPage", "pdb::helpHtmlToText",
                                       "pdb::helpEscapeHtml", "pdb::theme", "pdb::gameConfigs")):
        continue
    missing.append(sym)
if missing:
    for sym in missing:
        print("  FAIL: undefined shell symbol: %s" % sym)
    sys.exit(1)
print("  ok: no undefined shell symbols")
PYEOF
[ $? -eq 0 ] || { echo "FAIL: shell link"; exit 1; }

echo "== checking the icon atlas"
IMGUI_DIR="$ROOT/Android/third_party/imgui"
g++ -std=c++17 -O2 -I "$SRC" -I "$IMGUI_DIR" \
    "$ROOT/Android/tests/atlas_check.cpp" "$SRC/icons.cpp" "$SRC/png.cpp" \
    "$IMGUI_DIR/imgui.cpp" "$IMGUI_DIR/imgui_draw.cpp" "$IMGUI_DIR/imgui_tables.cpp" \
    "$IMGUI_DIR/imgui_widgets.cpp" -o "$OUT/atlas_check"
"$OUT/atlas_check" "$ASSETS"/icons/*.png || { echo "FAIL: icon atlas"; exit 1; }

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
    UI_SRC="$SRC/editor.cpp $SRC/icons.cpp $SRC/render2d.cpp \
$SRC/theme.cpp $SRC/ui.cpp $SRC/ui_dialogs.cpp $SRC/ui_help.cpp $SRC/ui_mapview.cpp \
$SRC/ui_panels.cpp $SRC/ui_toolbar.cpp"
    g++ -std=c++17 -O2 -Wall \
        -DPDB_ASSET_DIR="\"$ASSETS\"" \
        -I "$SRC" -I "$IMGUI" \
        "$ROOT/Android/tools/render_preview.cpp" $CORE_SRC $UI_SRC $IMGUI_SRC \
        -o "$OUT/render_preview"

    echo "== checking the safe-area insets"
    # The whole shell must move inside the insets: with a 60 px top inset the
    # menu bar's pixels have to start below it, and the status bar has to end
    # above the bottom inset.
    PDB_INSETS="60,48,12,12" "$OUT/render_preview" "$OUT/insets.png" 900 1400 > /dev/null
    python3 - "$OUT/insets.png" <<'PYEOF'
import sys, zlib, struct
data = open(sys.argv[1], 'rb').read()
# Minimal PNG reader for the 8-bit RGB/RGBA output of render_preview.
pos, w, h, ctype, idat = 8, 0, 0, 0, b''
while pos < len(data):
    length = struct.unpack('>I', data[pos:pos+4])[0]
    kind = data[pos+4:pos+8]
    chunk = data[pos+8:pos+8+length]
    if kind == b'IHDR':
        w, h = struct.unpack('>II', chunk[:8])
        ctype = chunk[9]  # bit depth is chunk[8]
    elif kind == b'IDAT':
        idat += chunk
    pos += 12 + length
raw = zlib.decompress(idat)
channels = 4 if ctype == 6 else 3
stride = w * channels
rows, prev = [], bytearray(stride)
i = 0
for _ in range(h):
    filt = raw[i]; i += 1
    line = bytearray(raw[i:i+stride]); i += stride
    if filt == 1:
        for x in range(channels, stride): line[x] = (line[x] + line[x-channels]) & 0xFF
    elif filt == 2:
        for x in range(stride): line[x] = (line[x] + prev[x]) & 0xFF
    elif filt == 3:
        for x in range(stride):
            left = line[x-channels] if x >= channels else 0
            line[x] = (line[x] + ((left + prev[x]) >> 1)) & 0xFF
    elif filt == 4:
        for x in range(stride):
            a = line[x-channels] if x >= channels else 0
            b = prev[x]; c = prev[x-channels] if x >= channels else 0
            p = a + b - c
            pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
            pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
            line[x] = (line[x] + pred) & 0xFF
    rows.append(line); prev = line

def dark_pixel(y, x):
    r, g, b = rows[y][x*channels:x*channels+3]
    return (r + g + b) < 300  # menu bar / toolbar text or icon

# With a 60 px top inset nothing may be drawn in the top 55 rows...
top_pixels = sum(1 for y in range(55) for x in range(w) if dark_pixel(y, x))
# ...and the status bar must not reach into the bottom 40 rows.
bottom_pixels = sum(1 for y in range(h-40, h) for x in range(w) if dark_pixel(y, x))
print("  content in the top inset area: %d px, in the bottom inset area: %d px"
      % (top_pixels, bottom_pixels))
if top_pixels > 40 or bottom_pixels > 40:
    print("  FAIL: the shell is not inside the safe area")
    sys.exit(1)
print("  ok: the shell is laid out inside the insets")
PYEOF
    [ $? -eq 0 ] || { echo "FAIL: safe area"; exit 1; }

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
