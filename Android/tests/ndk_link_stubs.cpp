// ndk_link_stubs.cpp — stand-ins for the NDK/GLES symbols the shell references,
// so the Android translation unit can be *linked* on the host.
//
// A -fsyntax-only check does not catch undefined symbols, and that is exactly how
// a missing function definition (installWorkAreaInsets) once reached CI and broke
// the NDK link. run_tests.sh does a real compile + shared link against this file.
//
// The GLES functions are deliberately NOT defined here: prebuilt/HW headers are
// unavailable, so run_tests.sh links with -Wl,--unresolved-symbols=ignore-in-object-files
// and then asserts that no symbol *from this repository* is undefined.
#include <android/log.h>
#include <android/asset_manager.h>
#include <GLES3/gl3.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdarg>
#include <cstdint>
extern "C" {
int __android_log_print(int, const char*, const char*, ...) { return 0; }
AAssetManager* AAssetManager_fromJava(JNIEnv*, jobject) { return nullptr; }
AAsset* AAssetManager_open(AAssetManager*, const char*, int) { return nullptr; }
int64_t AAsset_getLength64(AAsset*) { return 0; }
int AAsset_read(AAsset*, void*, size_t) { return 0; }
void AAsset_close(AAsset*) {}
int sigaction(int, const struct sigaction*, struct sigaction*) { return 0; }
int sigemptyset(void*) { return 0; }
int open(const char*, int, ...) { return -1; }
ssize_t write(int, const void*, size_t) { return 0; }
int close(int) { return 0; }
void _exit(int) {}
}
