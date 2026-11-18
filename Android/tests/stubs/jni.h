#pragma once
#include <cstddef>
#include <cstdint>
typedef int jint; typedef long long jlong; typedef unsigned char jboolean; typedef signed char jbyte;
typedef unsigned short jchar; typedef short jshort; typedef float jfloat; typedef double jdouble;
typedef jint jsize;
#define JNI_TRUE 1
#define JNI_FALSE 0
#define JNI_OK 0
#define JNI_VERSION_1_6 0x00010006
#define JNIEXPORT
#define JNICALL
struct _jobject; typedef struct _jobject* jobject; typedef struct _jobject* jclass;
typedef struct _jobject* jstring; typedef struct _jobject* jarray;
typedef struct _jobject* jbyteArray; typedef struct _jobject* jfloatArray;
typedef struct _jobject* jthrowable;
typedef struct _jmethodID* jmethodID;
struct JavaVM;
struct JNIEnv {
    jint GetJavaVM(JavaVM**); jclass GetObjectClass(jobject);
    jmethodID GetMethodID(jclass, const char*, const char*);
    void CallVoidMethod(jobject, jmethodID, ...);
    jboolean ExceptionCheck(); void ExceptionClear();
    jstring NewStringUTF(const char*);
    const char* GetStringUTFChars(jstring, jboolean*); void ReleaseStringUTFChars(jstring, const char*);
    jsize GetArrayLength(jarray); void GetByteArrayRegion(jbyteArray, jsize, jsize, jbyte*);
    void GetFloatArrayRegion(jfloatArray, jsize, jsize, jfloat*);
    void SetByteArrayRegion(jbyteArray, jsize, jsize, const jbyte*);
    jbyteArray NewByteArray(jsize); jobject NewGlobalRef(jobject);
    void DeleteGlobalRef(jobject); void DeleteLocalRef(jobject);
};
struct JavaVM { jint GetEnv(void**, jint); jint AttachCurrentThread(JNIEnv**, void*); jint DetachCurrentThread(); };
