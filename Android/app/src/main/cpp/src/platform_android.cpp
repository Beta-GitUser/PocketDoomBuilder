// PocketDoomBuilder — Android port (Dear ImGui)
// platform_android.cpp — JNI entry points and the frame loop.
//
// The Java side (MainActivity) owns the window, the GLSurfaceView and the
// Storage Access Framework pickers; the GLSurfaceView's own EGL context is
// reused here (its render thread is the only thread touching GL), so this
// file only has to initialise ImGui, feed it input and submit frames.
//
// Using a plain Activity + SurfaceView (rather than a NativeActivity with
// android_native_app_glue) is what makes SAF possible: startActivityForResult
// results have to come back to a Java Activity.

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <cstdint>
#include <jni.h>

#include <GLES3/gl3.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "editor.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "theme.h"
#include "ui.h"
#include "ui_internal.h"

#define LOG_TAG "PocketDoomBuilder"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

AAssetManager* g_assets = nullptr;
bool g_imguiReady = false;
int g_width = 0;
int g_height = 0;

bool g_shutdownRequested = false;

// Gesture state.
float g_pinchDistance = 0.0f;
bool g_pinching = false;
float g_pinchCentreX = 0.0f;
float g_pinchCentreY = 0.0f;

bool readAsset(const std::string& path, std::vector<uint8_t>& out, void*) {
    if (g_assets == nullptr) return false;
    AAsset* asset = AAssetManager_open(g_assets, path.c_str(), AASSET_MODE_BUFFER);
    if (asset == nullptr) return false;
    const off64_t length = AAsset_getLength64(asset);
    out.resize((size_t)length);
    if (length > 0) {
        const int read = AAsset_read(asset, out.data(), (size_t)length);
        if (read < 0) {
            AAsset_close(asset);
            out.clear();
            return false;
        }
    }
    AAsset_close(asset);
    return true;
}

void loadFonts() {
    ImGuiIO& io = ImGui::GetIO();

    std::vector<uint8_t> regular;
    std::vector<uint8_t> bold;
    const bool haveRegular = readAsset("Arimo-Regular.ttf", regular, nullptr);
    const bool haveBold = readAsset("Arimo-Bold.ttf", bold, nullptr);

    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    cfg.PixelSnapH = true;
    cfg.FontDataOwnedByAtlas = true;

    pdb::Editor& ed = pdb::editor();
    if (haveRegular) {
        void* data = ImGui::MemAlloc(regular.size());
        std::memcpy(data, regular.data(), regular.size());
        ed.fontUI = io.Fonts->AddFontFromMemoryTTF(data, (int)regular.size(), 15.0f, &cfg);
        ed.fontSmall = ed.fontUI;
    }
    if (haveBold) {
        void* data = ImGui::MemAlloc(bold.size());
        std::memcpy(data, bold.data(), bold.size());
        ed.fontBold = io.Fonts->AddFontFromMemoryTTF(data, (int)bold.size(), 15.0f, &cfg);
    }
    if (!haveRegular) {
        ed.fontUI = io.Fonts->AddFontDefault();
        ed.fontSmall = ed.fontUI;
        LOGE("Arimo font missing from assets; using the ImGui default font");
    }
    if (ed.fontBold == nullptr) ed.fontBold = ed.fontUI;
}

// Upload the icon atlas as a single GL texture (icons come from the desktop's
// Source/Core/Resources + plugin resource PNGs, listed in assets/icons.txt).
void uploadIconTexture() {
    pdb::IconAtlas& atlas = pdb::editor().icons;
    if (atlas.empty()) return;

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlas.width(), atlas.height(), 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, atlas.pixels().data());
    atlas.setTexture((ImTextureID)(size_t)tex);
    LOGI("Icon atlas uploaded: %dx%d", atlas.width(), atlas.height());
}

// Requests raised during a frame are handled by the Activity right after the
// draw call; these helpers call back into Java.
void notifyJava(JNIEnv* env, jobject activity) {
    pdb::Editor& ed = pdb::editor();
    if (!ed.requestOpen && !ed.requestSave && !ed.requestSaveAs) return;

    jclass clazz = env->GetObjectClass(activity);
    if (clazz == nullptr) return;

    if (ed.requestOpen) {
        jmethodID m = env->GetMethodID(clazz, "launchOpenPicker", "()V");
        ed.requestOpen = false;
        if (m) env->CallVoidMethod(activity, m);
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    if (ed.requestSave || ed.requestSaveAs) {
        const jboolean saveAs = ed.requestSaveAs ? JNI_TRUE : JNI_FALSE;
        jmethodID m = env->GetMethodID(clazz, "launchSavePicker", "(Z)V");
        ed.requestSave = false;
        ed.requestSaveAs = false;
        if (m) env->CallVoidMethod(activity, m, saveAs);
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    env->DeleteLocalRef(clazz);
}

} // namespace

extern "C" {

// Created on the GLSurfaceView's render thread, with its context current.
JNIEXPORT void JNICALL
Java_com_pocketdoombuilder_MainActivity_nativeCreate(JNIEnv* env, jobject thiz,
                                                     jobject assetManager, jint width,
                                                     jint height) {
    g_assets = AAssetManager_fromJava(env, assetManager);
    g_width = width;
    g_height = height;

    // A new GL context after a pause/context loss: tear the old ImGui state
    // down here, where the context is current, and rebuild below.
    if (g_imguiReady) {
        if (g_shutdownRequested) {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui::DestroyContext();
            g_imguiReady = false;
            g_shutdownRequested = false;
            pdb::editor().icons.setTexture(ImTextureID_Invalid);
        } else {
            return;
        }
    }

    pdb::Editor& ed = pdb::editor();
    ed.log("PocketDoomBuilder starting");
    ed.log("Ultimate Doom Builder interface, ported to Dear ImGui");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // the shell owns its layout
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.DisplaySize = ImVec2((float)width, (float)height);

    pdb::applyTheme(1.0f);
    loadFonts();
    ImGui_ImplOpenGL3_Init("#version 300 es");

    std::vector<std::string> warnings;
    pdb::loadIconsFromAssets(ed.icons, "icons.txt", readAsset, nullptr, warnings);
    for (const std::string& w : warnings) {
        LOGE("%s", w.c_str());
        ed.log(w, 1);
    }
    uploadIconTexture();

    g_imguiReady = true;
    LOGI("ImGui ready (%d icons)", (int)ed.icons.names().size());
    (void)thiz;
}

JNIEXPORT void JNICALL
Java_com_pocketdoombuilder_MainActivity_nativeResize(JNIEnv*, jobject, jint width,
                                                     jint height) {
    g_width = width;
    g_height = height;
    if (g_imguiReady) {
        ImGui::GetIO().DisplaySize = ImVec2((float)width, (float)height);
    }
}

JNIEXPORT void JNICALL
Java_com_pocketdoombuilder_MainActivity_nativeRender(JNIEnv* env, jobject thiz) {
    if (!g_imguiReady) return;
    if (g_shutdownRequested) {
        // Teardown happens here because ImGui's GL objects need the context.
        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext();
        g_imguiReady = false;
        g_shutdownRequested = false;
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)g_width, (float)g_height);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    pdb::drawMainWindow((float)g_width, (float)g_height);

    ImGui::Render();
    glViewport(0, 0, g_width, g_height);
    glClearColor(0.94f, 0.94f, 0.94f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    notifyJava(env, thiz);
}

JNIEXPORT void JNICALL
Java_com_pocketdoombuilder_MainActivity_nativeTouch(JNIEnv* env, jobject thiz,
                                                    jint action, jint pointerCount,
                                                    jfloatArray xs, jfloatArray ys) {
    if (!g_imguiReady) return;
    ImGuiIO& io = ImGui::GetIO();
    pdb::Editor& ed = pdb::editor();

    jfloat bufX[10] = {0};
    jfloat bufY[10] = {0};
    const jsize n = pointerCount > 10 ? 10 : pointerCount;
    if (n > 0 && xs != nullptr && ys != nullptr) {
        env->GetFloatArrayRegion(xs, 0, n, bufX);
        env->GetFloatArrayRegion(ys, 0, n, bufY);
    }
    (void)thiz;

    // action: 0 = down, 1 = move, 2 = up/cancel (see MainActivity).
    if (action == 0) {
        if (n == 2) {
            const float dx = bufX[1] - bufX[0];
            const float dy = bufY[1] - bufY[0];
            g_pinchDistance = std::sqrt(dx * dx + dy * dy);
            g_pinchCentreX = (bufX[0] + bufX[1]) * 0.5f;
            g_pinchCentreY = (bufY[0] + bufY[1]) * 0.5f;
            g_pinching = true;
            io.AddMouseButtonEvent(0, false);
        } else if (n >= 1) {
            io.AddMousePosEvent(bufX[0], bufY[0]);
            io.AddMouseButtonEvent(0, true);
        }
    } else if (action == 1) {
        if (n >= 2) {
            // Two-finger drag: pan by the centre movement, zoom by the
            // distance ratio. Both act on the map camera directly, which is
            // the touch equivalent of the desktop's pan_view / zoomin.
            const float dx = bufX[1] - bufX[0];
            const float dy = bufY[1] - bufY[0];
            const float distance = std::sqrt(dx * dx + dy * dy);
            const float cx = (bufX[0] + bufX[1]) * 0.5f;
            const float cy = (bufY[0] + bufY[1]) * 0.5f;

            if (!g_pinching) {
                g_pinchDistance = distance;
                g_pinchCentreX = cx;
                g_pinchCentreY = cy;
                g_pinching = true;
            }
            if (g_pinchDistance > 1.0f && distance > 1.0f) {
                const float factor = distance / g_pinchDistance;
                if (std::fabs(factor - 1.0f) > 0.002f) {
                    ed.view.zoomAt(cx, cy, factor, (float)g_width, (float)g_height);
                }
            }
            ed.view.pan(cx - g_pinchCentreX, cy - g_pinchCentreY);
            g_pinchDistance = distance;
            g_pinchCentreX = cx;
            g_pinchCentreY = cy;
            io.AddMouseButtonEvent(0, false);
        } else if (n == 1) {
            io.AddMousePosEvent(bufX[0], bufY[0]);
        }
    } else {
        io.AddMouseButtonEvent(0, false);
        g_pinching = false;
    }
}

JNIEXPORT void JNICALL
Java_com_pocketdoombuilder_MainActivity_nativeKey(JNIEnv*, jobject, jint keycode,
                                                  jint action, jint unicode) {
    if (!g_imguiReady) return;
    ImGuiIO& io = ImGui::GetIO();

    ImGuiKey key = ImGuiKey_None;
    switch (keycode) {
        case 4: key = ImGuiKey_Escape; break;      // BACK
        case 19: key = ImGuiKey_UpArrow; break;
        case 20: key = ImGuiKey_DownArrow; break;
        case 21: key = ImGuiKey_LeftArrow; break;
        case 22: key = ImGuiKey_RightArrow; break;
        case 61: key = ImGuiKey_Tab; break;
        case 66: key = ImGuiKey_Enter; break;
        case 67: key = ImGuiKey_Backspace; break;
        case 112: key = ImGuiKey_Delete; break;
        default: break;
    }
    if (key != ImGuiKey_None) {
        io.AddKeyEvent(key, action != 0); // action: 0 = up, 1 = down
    }
    if (unicode > 0 && action != 0) {
        io.AddInputCharacter((unsigned int)unicode);
    }
}

JNIEXPORT void JNICALL
Java_com_pocketdoombuilder_MainActivity_nativeOpenWad(JNIEnv* env, jobject thiz,
                                                      jbyteArray data, jstring name,
                                                      jstring uri) {
    if (data == nullptr) return;
    const jsize length = env->GetArrayLength(data);
    std::vector<uint8_t> bytes((size_t)length);
    if (length > 0) {
        env->GetByteArrayRegion(data, 0, length,
                                reinterpret_cast<jbyte*>(bytes.data()));
    }

    const char* nameChars = name ? env->GetStringUTFChars(name, nullptr) : nullptr;
    const char* uriChars = uri ? env->GetStringUTFChars(uri, nullptr) : nullptr;
    const std::string displayName = nameChars ? nameChars : "map.wad";
    const std::string uriString = uriChars ? uriChars : "";
    if (nameChars) env->ReleaseStringUTFChars(name, nameChars);
    if (uriChars) env->ReleaseStringUTFChars(uri, uriChars);

    pdb::Editor& ed = pdb::editor();
    ed.openWadFromMemory(bytes, displayName, uriString);
    pdb::requestFitToScreen();
    pdb::ui::resetMapViewState();
    (void)thiz;
}

JNIEXPORT jbyteArray JNICALL
Java_com_pocketdoombuilder_MainActivity_nativeSaveWad(JNIEnv* env, jobject thiz) {
    pdb::Editor& ed = pdb::editor();
    std::vector<uint8_t> bytes;
    std::string error;
    if (!ed.serialiseWad(bytes, error)) {
        ed.log(error, 2);
        ed.status = "Save failed: " + error;
        return nullptr;
    }
    jbyteArray result = env->NewByteArray((jsize)bytes.size());
    if (result != nullptr && !bytes.empty()) {
        env->SetByteArrayRegion(result, 0, (jsize)bytes.size(),
                                reinterpret_cast<const jbyte*>(bytes.data()));
    }
    ed.log("Map saved (" + std::to_string(bytes.size()) + " bytes)");
    ed.status = "Map saved";
    (void)thiz;
    return result;
}

JNIEXPORT void JNICALL
Java_com_pocketdoombuilder_MainActivity_nativePause(JNIEnv*, jobject) {
    // The EGL context goes away with the window; drop ImGui's GL objects on
    // the next render (or on the next surface creation, see nativeCreate).
    g_shutdownRequested = true;
    LOGI("paused");
}

// Called from the UI thread when the Activity goes away. The actual GL
// teardown is deferred to the render thread (see nativeRender).
JNIEXPORT void JNICALL
Java_com_pocketdoombuilder_MainActivity_nativeDestroy(JNIEnv*, jobject) {
    g_shutdownRequested = true;
    LOGI("shutdown requested");
}

} // extern "C"
