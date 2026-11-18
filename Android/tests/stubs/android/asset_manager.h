#pragma once
#include <jni.h>
#include <cstdint>
#include <cstddef>
typedef int64_t off64_t;
#define AASSET_MODE_BUFFER 1
struct AAssetManager; struct AAsset;
extern "C" AAssetManager* AAssetManager_fromJava(JNIEnv*, jobject);
extern "C" AAsset* AAssetManager_open(AAssetManager*, const char*, int);
extern "C" off64_t AAsset_getLength64(AAsset*);
extern "C" int AAsset_read(AAsset*, void*, size_t);
extern "C" void AAsset_close(AAsset*);
