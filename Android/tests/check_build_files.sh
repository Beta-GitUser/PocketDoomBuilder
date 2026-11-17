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
    for input in build_type abis run_host_tests upload_release; do
        grep -q "inputs.$input\|IN_$(echo "$input" | tr '[:lower:]' '[:upper:]')" "$wf" \
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
java=Android/app/src/main/java/com/pocketdoombuilder/MainActivity.java
for m in $(grep -oE 'native [a-zA-Z]+ native[A-Za-z]+' "$java" | awk '{print $3}'); do
    grep -q "Java_com_pocketdoombuilder_MainActivity_$m" Android/app/src/main/cpp/src/platform_android.cpp \
        || bad "no JNI implementation for $m"
done
note "JNI declarations and implementations match"

# --- native library name ----------------------------------------------------
grep -q 'System.loadLibrary("pocketdoombuilder")' "$java" || bad "MainActivity loads an unexpected library name"
grep -q 'add_library(pocketdoombuilder' "$cmake" || bad "CMake target name does not match the loaded library"
note "native library name consistent"

if [ "$fail" -ne 0 ]; then
    echo "build file checks FAILED"
    exit 1
fi
echo "build file checks passed"
