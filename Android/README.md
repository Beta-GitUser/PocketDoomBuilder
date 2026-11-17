# PocketDoomBuilder — Android port

A native Android port of **Ultimate Doom Builder**, rebuilt with
[Dear ImGui](https://github.com/ocornut/imgui) so the editor runs on a tablet
or phone while looking as close to the desktop interface as possible.

* **Priority 1 (this build):** re-create the desktop UI 1:1 — menu strip,
  toolbar, editing-mode toolbar, status bar, info panels, dockers and the 2D
  map view.
* **Priority 2 (later):** a touch-first interaction model on top of that.
* **Phase 1 scope:** interface shell + WAD loading + 2D wireframe rendering +
  pan / zoom / tap selection. Everything else is stubbed with a clear
  "not implemented in this build" message in the console, exactly as the
  desktop would report it.

## Status

| Area | State |
| --- | --- |
| UI shell (menus, toolbar, mode bar, dockers, info panel, status bar) | done, 1:1 layout from `MainForm.Designer.cs` |
| Toolbar / mode icons | the desktop's own PNG resources, decoded at startup |
| WAD parsing (IWAD + PWAD) | done |
| Map formats | Doom binary, Hexen binary, UDMF (read **and** write) |
| 2D rendering | vertices, linedefs, things, grid, view modes, selection/highlight |
| Pan / zoom / tap selection | done (wheel, drag, pinch, tap) |
| Editing, undo, prefabs, testing, script editor | stubbed |
| File IO | Storage Access Framework picker only (no bundled demo WAD) |

## Building

Requirements: Android SDK 35, NDK r26+, CMake 3.22+, JDK 17, Gradle 8.9+.

```sh
cd Android
gradle assembleDebug         # APK in app/build/outputs/apk/debug/
```

(`gradle wrapper` can be run once to generate `./gradlew` if you prefer the
wrapper; the wrapper JAR is not checked in here.)

Dear ImGui (v1.92.9b-docking, docking branch) is vendored in
`Android/third_party/imgui`, so no network access is needed during the build.

## Running the checks without a device

The platform-independent code (WAD/map codecs, view maths, PNG decoder) plus
the whole ImGui shell compile and run on the host with plain `g++`:

```sh
sh Android/tests/run_tests.sh            # unit tests
sh Android/tests/run_tests.sh --preview  # + headless shell screenshot
```

The preview tool (`Android/tools/render_preview.cpp`) runs the real UI code
against a small software rasteriser and writes `Android/tools/preview.png`,
which is handy for comparing the port against desktop screenshots.

## Layout

```
Android/
  app/src/main/cpp/src/     native shell (C++17, no exceptions/RTTI)
    map.h/.cpp              map data model (vertices, linedefs, sectors, things)
    wad.h/.cpp              WAD container read/write
    mapio*.cpp              Doom / Hexen / UDMF map lump codecs
    view2d.h/.cpp           camera, grid, hit testing, geometry helpers
    render2d.h/.cpp         ImGui draw-list map renderer
    theme.h/.cpp            colours from ColorCollection.cs + WinForms chrome
    icons.h/.cpp            icon atlas packing
    png.h/.cpp              dependency-free PNG decoder (built-in inflate)
    ui*.cpp                 the 1:1 interface
    editor.h/.cpp           application state
    platform_android.cpp    JNI entry points + GL/EGL plumbing
  app/src/main/java/.../MainActivity.java
                            GLSurfaceView host + SAF pickers (thin shim)
  app/src/main/assets/      fonts (Arimo) and the icon set + manifest
  tests/                    host-side unit tests
  tools/                    headless preview renderer
  third_party/imgui/        Dear ImGui (MIT)
```

## Notes on fidelity

* Colours come from `Source/Core/Rendering/ColorCollection.cs` (same indices,
  same defaults, same thing-colour rotation).
* Metrics come from `MainForm.Designer.cs` (1012 × 693 design size, 24 px menu,
  25 px toolbar, 23 px status bar, 30 px mode bar, 236 px dockers panel,
  106 px info panel, 138 px statistics block) and are scaled like the
  desktop's DPI auto-scaling.
* The status bar field order, the toolbar button order with its separators,
  and every menu label match the desktop designer file.
* Icons are the actual `Source/Core/Resources` and `Source/Plugins/*/Resources`
  PNGs, including the `.resx` aliases (e.g. `Script2` → `Script.png`).

## Licensing

The port keeps the upstream licence (see `COPYING`). Dear ImGui is MIT;
Arimo is Apache 2.0 (`app/src/main/assets/Arimo-LICENSE.txt`).

## Touch controls (phase 1)

| Gesture | Action |
| --- | --- |
| Tap | select the vertex / linedef / thing / sector under the finger |
| One-finger drag | pan the map |
| Two-finger drag | pan the map |
| Pinch | zoom around the pinch centre |
| Mouse wheel (if attached) | zoom around the pointer |
| Middle drag | pan |
| BACK | closes ImGui popups; the system back gesture then leaves the app |

Selection is resolved the same way the desktop does it: vertices first (they
are the hardest to hit), then linedefs, then things, then the innermost
sector. The hit band is widened for touch but falls back to the desktop's
8-pixel tolerance when a mouse is used.

## WAD formats

| Format | Detect | Read | Write |
| --- | --- | --- | --- |
| Doom binary | default | yes | yes |
| Hexen binary | `BEHAVIOR` lump present | yes (incl. 5 args, thing Z, ACS lump preserved) | yes |
| UDMF | `TEXTMAP` lump present | yes | yes |

Detection matches `DoomMapSetIO`/`HexenMapSetIO.IdentifyMapFormat` on the
desktop. UDMF fields the editor does not manage yet are carried through in
`MapElement::extra` (and in `MapSet::udmfRootExtra`) so a load → save
round-trip does not drop data.
