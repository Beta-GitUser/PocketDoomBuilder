#!/bin/sh
# Static sanity checks for the Android build wiring.
#
# These run before the SDK/NDK download in CI, so a typo in the Gradle or CMake
# files fails in seconds instead of after several minutes of setup.

set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"

fail=0
note() { printf '  %s\n' "$1"; }
bad() { printf '  FAIL: %s\n' "$1"; fail=1; }

echo "== Android build file checks"

# --- required files ---------------------------------------------------------
for f in \
    Android/settings.gradle \
    Android/build.gradle \
    Android/gradle.properties \
    Android/gradlew \
    Android/gradle/wrapper/gradle-wrapper.jar \
    Android/gradle/wrapper/gradle-wrapper.properties \
    Android/app/build.gradle \
    Android/app/src/main/AndroidManifest.xml \
    Android/app/src/main/cpp/CMakeLists.txt \
    Android/app/src/main/java/com/pocketdoombuilder/MainActivity.java \
    Android/app/src/main/java/com/pocketdoombuilder/Diagnostics.java \
    Android/app/src/main/assets/icons.txt \
    Android/app/src/main/assets/Arimo-Regular.ttf \
    Android/third_party/imgui/imgui.cpp \
    Android/third_party/imgui/backends/imgui_impl_opengl3.cpp \
    Android/tools/smoke_test.sh \
    Android/tools/verify_apk.py \
    Android/tools/ci_log_annotation.sh
do
    [ -f "$f" ] || bad "missing $f"
done
note "required files present"

# --- CMake must build every source it references and link GLES3 -------------
cmake=Android/app/src/main/cpp/CMakeLists.txt
for src in $(grep -oE 'src/[A-Za-z0-9_]+\.cpp' "$cmake" | sort -u); do
    [ -f "Android/app/src/main/cpp/$src" ] || bad "CMakeLists references missing $src"
done
grep -q 'IMGUI_IMPL_OPENGL_ES3' "$cmake" || bad "CMakeLists does not define IMGUI_IMPL_OPENGL_ES3"
grep -q 'GLESv3' "$cmake" || bad "CMakeLists does not link GLESv3"
grep -q 'IMGUI_DIR' "$cmake" || bad "CMakeLists does not set IMGUI_DIR"
note "CMake source list consistent, GLES3 backend enabled"

# --- every cpp in src/ is compiled (catches files added but not hooked up) --
for f in Android/app/src/main/cpp/src/*.cpp; do
    base=$(basename "$f")
    grep -q "src/$base" "$cmake" || bad "$base is not listed in CMakeLists"
done
note "all translation units hooked into CMake"

# --- Gradle wiring ----------------------------------------------------------
grep -q "externalNativeBuild" Android/app/build.gradle || bad "app/build.gradle has no externalNativeBuild block"
grep -q "src/main/cpp/CMakeLists.txt" Android/app/build.gradle || bad "app/build.gradle does not point at the CMakeLists"
grep -q "ndkVersion" Android/app/build.gradle || bad "app/build.gradle does not pin an ndkVersion"
grep -q "^include ':app'" Android/settings.gradle || bad "settings.gradle does not include :app"
grep -q "com.android.application" Android/build.gradle || bad "root build.gradle has no AGP plugin"
ndk_version=$(grep -oE "ndkVersion '[^']+'" Android/app/build.gradle | sed "s/ndkVersion '//;s/'//")
note "Gradle wiring ok (ndkVersion $ndk_version)"

# AGP is checked in as a plugin version; the wrapper has to be new enough for
# it (AGP 8.7 needs Gradle 8.9) and the CMake version declared in Gradle has to
# be the one the workflow installs.
agp_version=$(sed -n "s/.*com.android.application' version '\([^']*\)'.*/\1/p" Android/build.gradle)
gradle_version=$(sed -n 's/.*gradle-\([0-9.]*\)-bin\.zip.*/\1/p' Android/gradle/wrapper/gradle-wrapper.properties)
cmake_version=$(grep -oE "version '[0-9.]+'" Android/app/build.gradle | head -1 | sed "s/version '//;s/'//")
[ -n "$agp_version" ] || bad "cannot read the AGP version from build.gradle"
[ -n "$gradle_version" ] || bad "gradle-wrapper.properties has no Gradle distributionUrl"
note "AGP $agp_version on Gradle $gradle_version, CMake $cmake_version"

wf=.github/workflows/build_android.yml

# --- every build input the workflow offers must be consumed ----------------
# A workflow_dispatch input that is declared but never read is a silent
# no-op for the user, so check each one is actually used.
if [ -f "$wf" ]; then
    for input in build_type abis run_host_tests upload_release \
                 run_emulator_smoke_test emulator_software_gpu; do
        upper=$(echo "$input" | tr '[:lower:]' '[:upper:]')
        grep -q "inputs.$input\|IN_$upper\|\$$upper\|FORCE_SOFTWARE" "$wf" \
            || bad "workflow declares input '$input' but never reads it"
    done
    for script in Android/tools/set_abis.py Android/tools/verify_apk.py \
                  Android/tools/ci_log_annotation.sh Android/tests/check_build_files.sh; do
        grep -q "$(basename "$script")" "$wf" || bad "workflow never runs $(basename "$script")"
    done
    note "workflow inputs are all consumed"
fi

# --- the workflow must install the NDK version Gradle asks for --------------
if [ -f "$wf" ]; then
    grep -q "ndk;$ndk_version" "$wf" || bad "workflow does not install ndk;$ndk_version"
    grep -q "platforms;android-35" "$wf" || bad "workflow does not install platforms;android-35"
    grep -q "Android/tools/set_abis.py" "$wf" || bad "workflow does not call set_abis.py"
    grep -q "cmake;$cmake_version" "$wf" || bad "workflow does not install cmake;$cmake_version"
    grep -q "compileSdk 35" Android/app/build.gradle || bad "app/build.gradle does not compile against SDK 35"
    note "workflow installs the pinned NDK and SDK platform"
fi

# --- headers include what they use ------------------------------------------
# libstdc++ 13 no longer pulls <cstdint> in through <string>/<vector>, which
# broke the CI build once (theme.cpp), so check for the include explicitly
# instead of relying on the compiler's transitive headers.
sources=$(ls Android/app/src/main/cpp/src/*.h Android/app/src/main/cpp/src/*.cpp \
    Android/tests/pdb_tests.cpp Android/tools/render_preview.cpp)
for f in $sources; do
    if grep -qE '\b(u?int(8|16|32|64)_t|u?intptr_t|u?intmax_t)\b' "$f"; then
        grep -qE '#include <cstdint>|#include <stdint.h>' "$f" \
            || bad "$(basename "$f") uses fixed-width integer types without <cstdint>"
    fi
    if grep -qE '\b(size_t|ptrdiff_t)\b' "$f"; then
        grep -qE '#include <(cstddef|stddef\.h|cstdio|cstdlib|cstring|string|vector|algorithm|cmath)>' "$f" \
            || bad "$(basename "$f") uses size_t without a header that provides it"
    fi
    if grep -qE '(std::)?(sqrt|fabs|floor|ceil|lround|round|atan2|hypot|sin|cos|tan|pow)\(' "$f"; then
        grep -qE '#include <cmath>|#include <math.h>' "$f" \
            || bad "$(basename "$f") uses math functions without <cmath>"
    fi
done
note "every translation unit includes the headers it uses"

# --- JNI entry points line up with the Java declarations --------------------
# Parameters have to match too: a mismatch builds fine and then throws
# UnsatisfiedLinkError on the first call, which is exactly the kind of bug
# that only shows up on a device.
java=Android/app/src/main/java/com/pocketdoombuilder/MainActivity.java
jni_out=$(python3 Android/tools/check_jni.py) || bad "JNI declarations do not match their implementations"
note "$(printf '%s' "$jni_out" | sed 's/^ *//')"

# --- diagnostics plumbing must stay wired --------------------------------
# The user has no PC, so the only way a startup failure can be reported is the
# in-app log: the Java activity has to register the overlay, the native layer
# has to mirror its log into it, and the manifest has to allow writing the log
# to Documents on old releases.
manifest=Android/app/src/main/AndroidManifest.xml
java=Android/app/src/main/java/com/pocketdoombuilder/MainActivity.java
grep -q "WRITE_EXTERNAL_STORAGE" "$manifest" || bad "manifest lacks the legacy Documents permission"
grep -q "maxSdkVersion=\"28\"" "$manifest" || bad "Documents permission must be capped at API 28"
grep -q "Diagnostics.init" "$java" || bad "MainActivity does not initialise Diagnostics"
grep -q "appendNativeLog" "$java" || bad "MainActivity has no appendNativeLog callback"
grep -q "appendNativeLog" Android/app/src/main/cpp/src/platform_android.cpp \
    || bad "native layer never mirrors its log to Java"
grep -q "installCrashHandler" Android/app/src/main/cpp/src/platform_android.cpp \
    || bad "native layer installs no crash handler"
grep -q "previousCrashReport" "$java" || bad "MainActivity ignores the previous crash report"
grep -q "fullReport" Android/app/src/main/java/com/pocketdoombuilder/Diagnostics.java \
    || bad "Diagnostics cannot export the log of earlier runs"
grep -q "glThreadStack" Android/app/src/main/java/com/pocketdoombuilder/Diagnostics.java \
    || bad "Diagnostics cannot report where the GL thread is stuck"
grep -q "asset read failed" Android/app/src/main/cpp/src/platform_android.cpp \
    || bad "asset reads are not verified (a short read corrupts PNG input)"
grep -q "Too many DEFLATE blocks" Android/app/src/main/cpp/src/png.cpp \
    || bad "the PNG inflate loop has no block bound"
grep -q "Shelf packing" Android/app/src/main/cpp/src/icons.cpp \
    || bad "the icon atlas is packed into uniform cells again (1226 MB on a phone)"
grep -q "GL_MAX_TEXTURE_SIZE" Android/app/src/main/cpp/src/platform_android.cpp \
    || bad "the icon atlas upload does not check the GL texture size limit"
grep -q "glGetError" Android/app/src/main/cpp/src/platform_android.cpp \
    || bad "a failed icon texture upload would go unreported (black icon boxes)"
grep -q "drawStartupFrame" Android/app/src/main/cpp/src/platform_android.cpp \
    || bad "startup progress is not drawn (init looks like a hang)"
grep -q "beginIconLoad" Android/app/src/main/cpp/src/platform_android.cpp \
    || bad "icon loading is not incremental (blocks the GL thread at startup)"
grep -q "RelWithDebInfo" Android/app/src/main/cpp/CMakeLists.txt \
    || bad "native code may be built unoptimised (startup 20x slower on a phone)"
grep -q "keepDebugSymbols" Android/app/build.gradle \
    || bad "debug builds strip the symbols the hang watchdog needs"

# --- Help menu -------------------------------------------------------------
grep -q "helpdoc.cpp" Android/app/src/main/cpp/CMakeLists.txt \
    || bad "the manual formatter is not compiled into the shell"
grep -q "ui_help.cpp" Android/app/src/main/cpp/CMakeLists.txt \
    || bad "the Help windows are not compiled into the shell"
test -f Android/app/src/main/assets/help-index.txt \
    || bad "the manual index (assets/help-index.txt) is missing"
test -d Android/app/src/main/assets/help \
    || bad "the manual pages (assets/help) are missing"
count=$(ls Android/app/src/main/assets/help/*.html 2>/dev/null | wc -l)
[ "$count" -ge 40 ] || bad "only $count manual pages are packaged (expected 40+)"
missing=0
while read -r page; do
    [ -f "Android/app/src/main/assets/help/$page" ] || { missing=$((missing + 1)); echo "    missing page: $page"; }
done <<EOF2
$(grep -v '^#' Android/app/src/main/assets/help-index.txt | cut -d'|' -f1 | tr -d ' ' | sort -u)
EOF2
[ "$missing" -eq 0 ] || bad "$missing page(s) in help-index.txt are not packaged"
grep -q "Reference Manual" Android/app/src/main/cpp/src/ui_toolbar.cpp \
    || bad "the Help menu lost the Reference Manual entry"
grep -q "Keyboard Shortcuts Reference" Android/app/src/main/cpp/src/ui_toolbar.cpp \
    || bad "the Help menu lost the shortcut reference entry"
grep -q "Diagnostics and Log" Android/app/src/main/cpp/src/ui_toolbar.cpp \
    || bad "the Help menu lost the diagnostics entry"
grep -q "exportManual" Android/app/src/main/cpp/src/platform_android.cpp \
    || bad "the manual cannot be exported from the app"
grep -q "public void exportManual" Android/app/src/main/java/com/pocketdoombuilder/MainActivity.java \
    || bad "the Java side has no manual export callback"
grep -q "public void openUrl" Android/app/src/main/java/com/pocketdoombuilder/MainActivity.java \
    || bad "Help links cannot open a browser"
grep -q "showConfigFolder" Android/app/src/main/java/com/pocketdoombuilder/MainActivity.java \
    || bad "Help > Program Configuration Folder has no implementation"
grep -q "logSink" Android/app/src/main/cpp/src/editor.cpp \
    || bad "Editor::log does not forward to the log sink"
grep -q "last_crash.txt" Android/app/src/main/cpp/src/platform_android.cpp \
    || bad "crash handler does not write a report the next launch can show"
grep -q "last_crash.txt" Android/app/src/main/java/com/pocketdoombuilder/Diagnostics.java \
    || bad "Diagnostics does not read the previous crash report"
note "diagnostics log, crash handler and Documents export wired"
if [ ! -f Android/app/src/main/java/com/pocketdoombuilder/Diagnostics.java ]; then
    bad "missing Diagnostics.java"
fi

# --- native library name ----------------------------------------------------
grep -q 'System.loadLibrary("pocketdoombuilder")' "$java" || bad "MainActivity loads an unexpected library name"
grep -q 'add_library(pocketdoombuilder' "$cmake" || bad "CMake target name does not match the loaded library"
note "native library name consistent"

if [ "$fail" -ne 0 ]; then
    echo "build file checks FAILED"
    exit 1
fi
echo "build file checks passed"
