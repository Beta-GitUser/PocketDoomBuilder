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
* **Manually** from the *Actions* tab → *Build Android APK* → *Run workflow*,
  with these inputs:

  | Input | Meaning |
  | --- | --- |
  | `build_type` | `debug` (default) or `release` (release is signed with the debug key so it installs without a keystore) |
  | `abis` | `arm64-v8a` (default, for devices) or `arm64-v8a,x86_64` (adds emulator support); rewrites `abiFilters` via `Android/tools/set_abis.py` |
  | `run_host_tests` | compile + run the host unit tests and the shell preview first (default `true`) |
  | `upload_release` | also attach the APK to the rolling `android-latest` pre-release (default `false`) |
  | `run_emulator_smoke_test` | additionally boot an x86_64 emulator, install the APK and check it starts (default `false`; slow) |

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
skips the boot (it does not fake a failure). On a runner with KVM — a larger
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
