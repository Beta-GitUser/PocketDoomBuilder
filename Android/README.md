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

### On GitHub Actions (the normal route)

Google only ships **x64** builds of the Android NDK, so an arm64-only
workstation cannot run a local NDK build at all. The APK is therefore built by
`.github/workflows/build_android.yml` on a GitHub-hosted `ubuntu-24.04` runner:

* **Automatically** on every push / pull request that touches `Android/**` or
  the workflow itself.
* **Manually** from the *Actions* tab → the Android workflow → *Run workflow*
  (GitHub may list it under the name it was first registered with), with these
  inputs:

  | Input | Meaning |
  | --- | --- |
  | `build_type` | `debug` (default) or `release` (release is signed with the debug key so it installs without a keystore) |
  | `abis` | `arm64-v8a` (default, for devices) or `arm64-v8a,x86_64` (adds emulator support); rewrites `abiFilters` via `Android/tools/set_abis.py` |
  | `run_host_tests` | compile + run the host unit tests and the shell preview first (default `true`) |
  | `upload_release` | also attach the APK to the rolling `android-latest` pre-release (default `false`) |
  | `run_emulator_smoke_test` | additionally boot an x86_64 emulator, install the APK and check it starts (default `false`; slow) |
  | `emulator_software_gpu` | try the emulator even without `/dev/kvm` (qemu TCG: very slow, may time out; only as a last resort) |

  The same thing from a terminal (address the workflow by file name; the
  display name GitHub has cached for it can differ):

  ```sh
  gh workflow run build_android.yml --ref <branch> \
      -f build_type=release -f abis=arm64-v8a
  ```

The finished APK is attached to the run as the artifact
`PocketDoomBuilder-<build_type>-apk` (file name
`PocketDoomBuilder-<build_type>-<sha>.apk`); the shell screenshot from the host
tests is attached as `shell-preview`. On failure the workflow also uploads the
Gradle reports and CMake logs (`build-logs`).

The optional emulator job is the only check that proves the APK *runs*: it
installs it on an API 30 x86_64 emulator, launches `MainActivity`, and requires
the process to stay alive, the activity to be resumed, `ImGui ready` in logcat,
and no `FATAL EXCEPTION` / `Fatal signal`. It uploads a screenshot and the full
logcat, so a crash on startup is diagnosable without a device in hand.

The emulator needs `/dev/kvm`, which standard GitHub-hosted runners do **not**
expose; the job detects that, says so in a warning plus the run summary, and
skips the boot (it does not fake a failure). `emulator_software_gpu=true`
forces a TCG (fully emulated) boot instead — it can take 20+ minutes and may
still time out, but it is the only way to get a boot on a hosted runner. On a runner with KVM — a larger
hosted runner or a self-hosted one — the test runs for real and fails the job
if the app does not start. The same check runs by hand against any connected
device:

```sh
sh Android/tools/smoke_test.sh app/build/outputs/apk/debug/app-debug.apk shot.png
```

### Locally (x64 or arm64 host with an x64 NDK)

Requirements: Android SDK 35, NDK `27.0.12077973`, CMake 3.22.1, JDK 17.

```sh
cd Android
./gradlew assembleDebug      # APK in app/build/outputs/apk/debug/
```

The Gradle wrapper (8.9) is checked in, and `local.properties` (or
`ANDROID_HOME` / `ANDROID_SDK_ROOT`) has to point at an SDK.

Dear ImGui (v1.92.9b-docking, docking branch) is vendored in
`Android/third_party/imgui`, so no network access is needed during the build
beyond Gradle's own dependency resolution.

## Startup

Native startup is incremental: `nativeCreate` sets up ImGui and reads the icon
manifest, then the GL thread decodes a batch of icons per frame while a plain-GL
progress bar is drawn in between, and the toolbar appears when the last icon is
packed. Nothing blocks the GL thread for more than a frame, so a slow start is
visible instead of looking like a hang.

Two things learned the hard way on a Mali-G52 device:

- The icon atlas is shelf-packed (`icons.cpp`). It used to place all 295 icons
  in square cells sized to the largest icon (1024x512), which came to
  **18450x17425 = 1226 MB**: the allocation failed, the texture stayed unbound
  and every toolbar icon drew as a black box. It is now 1024x2995 (11.7 MB) and
  `Android/tests/atlas_check.cpp` asserts no overlap, no clipping and no lost
  pixels.
- Debug builds are what CI ships, and AGP left the native library at `-O0`,
  which made the same startup take ~30 s. The build now defaults to
  `RelWithDebInfo` (symbols kept, so the hang watchdog's stack dump still works).

## The Help menu

The desktop's Help menu opens `Help/*.chm` in the Win32 help viewer, which does
not exist on Android. The port ships the manual's source pages (`Help/*.html` in
this repository, copied to `assets/help/`) and formats them at runtime:

| Desktop | Here |
|---|---|
| Reference Manual | `ui_help.cpp`: index from `assets/help-index.txt` (mirrors `Help/Contents.hhc`), page reader with working cross-page links and a Back stack |
| Keyboard Shortcuts Reference | the manual's own controls pages, until the action system is ported (the desktop generates the table from its action list) |
| Program Configuration Folder | writes the log to `Documents/PocketDoomBuilder` and reports the private files dir in the log |
| About this Editing Mode | the manual's `editingmodes.html` |
| GitHub issues tracker / Check for updates | opens the project's GitHub pages |
| Diagnostics and Log | the on-device log overlay (this port only) |
| About | three tabs, matching the desktop AboutForm |

`helpdoc.cpp` is a small XHTML → formatted-lines parser: headings, paragraphs,
`<pre>` blocks, lists, tables (one line per row, `key - description`), inline
bold/italic/code and links. It is not a general browser, and it deliberately
drops the manual's boilerplate (`<object>`, `<script>`, the stylesheet link).
Layout is word-level flow so styled runs inside a paragraph wrap correctly.

The manual can also be exported: About → This port → *Save the manual to
Documents* concatenates every page into one self-contained HTML file, with the
cross-page links rewritten to in-document anchors.

Unit tests (`testHelpDoc`) parse eight pages and assert no raw markup or entities
leak, the index lists only pages that are actually packaged, and every link
resolves to a shipped page. `check_build_files.sh` verifies the pages, the index
and the JNI callbacks are all present.

## Safe area (notch / gesture bar)

The app draws edge-to-edge, and on Android 15 (API 35) it has to. A gear button
next to **ⓘ** opens a Settings tab with **Respect safe area (notch, cutout,
gesture bar)**; with it on, the display-cutout and system-bar insets from
`WindowInsets` are applied to both layers:

- the Java overlay and the on-screen buttons, by padding and margins;
- the ImGui shell, through `ImGuiPlatformIO::Platform_GetWindowWorkAreaInsets`,
  so the menu bar, toolbar and Help menus sit below the notch.

The setting is persisted (`PREF_SAFE_AREA`) and is off by default. Insets are
read on API 28+ (cutout) and use the deprecated system-bar insets below 30.

`run_tests.sh --preview` renders the shell with `PDB_INSETS="60,48,12,12"` and
asserts that no pixels are drawn inside the top or bottom inset, so a regression
in the inset plumbing fails the build instead of hiding the Help menu.

## When the app shows nothing

The app reports on itself, because on a phone there is no terminal to look at.

* **ⓘ button** (top-right corner, always present) opens a diagnostics panel with
  the startup log: device/ABI, whether the native library loaded, the GL
  version/renderer the device granted, whether the surface was created, the
  first frame, and every console line the editor produced (each init stage of
  the native side is logged too).
* **Copy log** puts it on the clipboard; **Save to Documents** writes
  `Documents/PocketDoomBuilder/PocketDoomBuilder-<timestamp>.txt`, which both
  buttons fill with the log of *all* runs, not just the current process.
* The panel opens **by itself** when the native library fails to load, when a
  JNI call throws, when the context is not OpenGL ES 3, or when nothing has been
  drawn 4 seconds after start.
* A native crash (`SIGSEGV`/`SIGBUS`/`SIGILL`/`SIGFPE`/`SIGABRT`) is caught and
  written to `last_crash.txt` in the app's files directory, with the signal,
  the fault address and the init stage it died in. The **next** launch shows
  that report before starting the renderer (with a *Continue* button), so a
  crash that reproduces reliably cannot hide its own evidence.

So if the screen is black: tap ⓘ, tap *Copy log*, and paste it into the issue
or chat. If the app dies instantly, open it again and the crash report is on
screen; use *Save to Documents* to keep it.

## Running the checks without a device

The platform-independent code (WAD/map codecs, view maths, PNG decoder) plus
the whole ImGui shell compile and run on the host with plain `g++`:

```sh
sh Android/tests/run_tests.sh            # unit tests (+ build-file checks)
sh Android/tests/run_tests.sh --preview  # + headless shell screenshot
sh Android/tests/check_build_files.sh    # static Gradle/CMake/workflow checks
python3 Android/tools/verify_apk.py out/PocketDoomBuilder-debug-abc1234.apk arm64-v8a
                                         # inspect a finished APK
```

`check_build_files.sh` needs no NDK: it verifies that every translation unit is
hooked into `CMakeLists.txt`, that the GLES3 backend define and libraries are
in place, that the Gradle wiring (namespace, `ndkVersion`, ABI list) matches the
workflow's pinned NDK, and that every `native*` declaration in `MainActivity.java`
has a matching `Java_com_pocketdoombuilder_MainActivity_*` implementation. It is
run automatically by `run_tests.sh` and as a pre-flight step of the CI build, so
a broken build script fails in seconds instead of after the SDK download.

`check_jni.py` compares each `private native` declaration in `MainActivity.java`
with its `Java_com_pocketdoombuilder_MainActivity_*` implementation, parameter
types included. A mismatch compiles and links cleanly and then throws
`UnsatisfiedLinkError` on the first call, i.e. it would only ever be noticed on
a device; `check_build_files.sh` runs the checker.

`verify_apk.py` is the counterpart for a *successful* build: the APK is a zip,
so it can be inspected without the SDK. It checks that the native library is
present for exactly the ABIs that were requested, that the icon manifest, the
icon set and both Arimo fonts are packaged uncompressed (the native side reads
them straight through `AAssetManager`), and that the manifest/dex/resource table
are there. The CI build runs it on the finished APK, so a green run means an
installable APK rather than just a green Gradle task. Its own self-test
(`Android/tests/test_verify_apk.py`, run by `run_tests.sh`) feeds it synthetic
APKs with each defect to make sure it actually rejects them.

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
