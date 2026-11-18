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

#include <chrono>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <algorithm>
#include <utility>
#include <map>
#include <sstream>
#include <cstring>
#include <string>
#include <vector>

#include "editor.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"
#include "png.h"
#include "theme.h"
#include "ui.h"
#include "ui_internal.h"

// Defined at the bottom of the file: assembles the whole manual into one document.
std::string buildManualExport();
// Defined near the bottom: hands ImGui the safe-area insets each frame.
void installWorkAreaInsets();

#define LOG_TAG "PocketDoomBuilder"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

AAssetManager* g_assets = nullptr;
bool g_imguiReady = false;
int g_width = 0;
int g_height = 0;

bool g_shutdownRequested = false;

// Keeps the ImGui UI out of the status bar, the display cutout and the gesture
// bar. The Java side sets these from the real WindowInsets and persists the
// user's choice; the native code only applies them.
int g_insetTop = 0;
int g_insetBottom = 0;
int g_insetLeft = 0;
int g_insetRight = 0;

// Where the Java side wants crash reports (its getFilesDir()).
std::string g_filesDir;

// Java side, so native log lines can be mirrored into the same log the
// diagnostics overlay shows.
JavaVM* g_vm = nullptr;
jobject g_activity = nullptr;
jmethodID g_appendNativeLog = nullptr;

// The last thing the app was doing, so a crash report says what failed.
char g_stage[128] = "startup";

void mirrorLog(const std::string& text, int level);

// Defined below; the startup loader near the top of the file uses them.
bool readAsset(const std::string& path, std::vector<uint8_t>& out, void* userData);

namespace {

// Startup progress.
//
// Icon loading used to run inside nativeCreate, blocking the GL thread for the
// whole load: the surface stayed black and an in-progress start looked exactly
// like a broken one. It now advances a few icons per frame while a plain-GL
// progress bar is drawn in between, so startup is visible.
struct StartupState {
    bool active = false;
    std::vector<std::pair<std::string, std::string>> entries; // name, asset path
    size_t next = 0;
    std::map<std::string, std::vector<uint8_t>> pixels;
    std::map<std::string, int> widths;
    std::map<std::string, int> heights;
};
StartupState g_startup;

// Plain GLES3 clear + scissor rectangles: no shaders, no textures, no ImGui, so
// it is safe on the GL thread before (and only before) the backend is ready.
void drawStartupFrame(int width, int height, int percent) {
    if (width <= 0 || height <= 0) return;
    glViewport(0, 0, width, height);
    glClearColor(0.09f, 0.10f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    const GLfloat barW = (GLfloat)width * 0.5f;
    const GLfloat barH = (GLfloat)(8.0f * (height > 1200 ? 2.0f : 1.5f));
    const GLfloat x0 = ((GLfloat)width - barW) * 0.5f;
    const GLfloat y0 = (GLfloat)height * 0.5f;
    glEnable(GL_SCISSOR_TEST);
    glScissor((GLint)x0, (GLint)y0, (GLsizei)barW, (GLsizei)barH);
    glClearColor(0.22f, 0.24f, 0.28f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    const int pct = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
    if (pct > 0) {
        glScissor((GLint)x0, (GLint)y0, (GLsizei)(barW * (GLfloat)pct / 100.0f),
                  (GLsizei)barH);
        glClearColor(0.35f, 0.65f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glDisable(GL_SCISSOR_TEST);
}

// Read the icon manifest (a few KB of text) and prepare the incremental load.
void beginIconLoad() {
    g_startup = StartupState();
    std::vector<uint8_t> manifest;
    if (!readAsset("icons.txt", manifest, nullptr)) {
        pdb::editor().log("Icon manifest not found; toolbar icons disabled.", 1);
        return;
    }
    std::istringstream stream(std::string(manifest.begin(), manifest.end()));
    std::string line;
    while (std::getline(stream, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        g_startup.entries.emplace_back(line.substr(0, eq), line.substr(eq + 1));
    }
    g_startup.active = !g_startup.entries.empty();
    LOGI("icon manifest: %zu entries", g_startup.entries.size());
}

// Decode up to `budget` icons; true once everything has been read.
bool advanceIconLoad(size_t budget, std::vector<std::string>& warnings) {
    const size_t end = std::min(g_startup.entries.size(), g_startup.next + budget);
    for (; g_startup.next < end; ++g_startup.next) {
        const std::string& name = g_startup.entries[g_startup.next].first;
        const std::string& path = g_startup.entries[g_startup.next].second;
        std::vector<uint8_t> bytes;
        if (!readAsset(path, bytes, nullptr)) {
            warnings.push_back("Icon asset missing: " + path);
            continue;
        }
        pdb::ImageRGBA img;
        std::string error;
        if (!pdb::decodePng(bytes, img, error)) {
            warnings.push_back("Icon '" + path + "' could not be decoded: " + error);
            continue;
        }
        g_startup.pixels[name] = std::move(img.pixels);
        g_startup.widths[name] = img.width;
        g_startup.heights[name] = img.height;
    }
    return g_startup.next >= g_startup.entries.size();
}

} // namespace

// Also mirrored into the in-app log: when a device hangs, the last stage is
// the only evidence of where it stopped, and on a phone there is no logcat.
void setStage(const char* stage) {
    std::snprintf(g_stage, sizeof(g_stage), "%s", stage);
    LOGI("stage: %s", stage);
    static char last[sizeof(g_stage)] = "";
    if (std::strcmp(last, stage) != 0) {
        std::snprintf(last, sizeof(last), "%s", stage);
        mirrorLog(std::string("init stage: ") + stage, 0);
    }
}

// Signal handler: write a short report next to the app's files so the next
// launch can show it. Async-signal-safe calls only (open/write/close).
extern "C" void crashHandler(int signal, siginfo_t* info, void*) {
    const char* dir = g_filesDir.empty() ? "/data/local/tmp" : g_filesDir.c_str();
    char path[512];
    std::snprintf(path, sizeof(path), "%s/last_crash.txt", dir);

    const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0) {
        char buffer[512];
        const int n = std::snprintf(buffer, sizeof(buffer),
                                    "fatal signal %d (%s) at %p\nstage: %s\n"
                                    "thread: gl renderer\n",
                                    signal, strsignal(signal),
                                    info != nullptr ? info->si_addr : nullptr, g_stage);
        if (n > 0) {
            const ssize_t ignored = write(fd, buffer, (size_t)n);
            (void)ignored;
        }
        close(fd);
    }
    LOGE("fatal signal %d, stage %s", signal, g_stage);
    _exit(1);
}

void installCrashHandler() {
    static bool installed = false;
    if (installed) return;
    installed = true;

    struct sigaction action {};
    action.sa_sigaction = crashHandler;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (int signal : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) {
        sigaction(signal, &action, nullptr);
    }
}

// Mirror a console line to logcat and to the Java log, so a text the editor
// only shows in its own console is not lost when the screen is black.
void mirrorLog(const std::string& text, int level) {
    if (level >= 2) {
        LOGE("%s", text.c_str());
    } else if (level == 1) {
        LOGI("warning: %s", text.c_str());
    } else {
        LOGI("%s", text.c_str());
    }
    if (g_vm == nullptr || g_activity == nullptr || g_appendNativeLog == nullptr) return;

    JNIEnv* env = nullptr;
    bool attached = false;
    if (g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
        attached = true;
    }
    jstring line = env->NewStringUTF(text.c_str());
    if (line != nullptr) {
        env->CallVoidMethod(g_activity, g_appendNativeLog, line);
        env->DeleteLocalRef(line);
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (attached) g_vm->DetachCurrentThread();
}

// Gesture state.
float g_pinchDistance = 0.0f;
bool g_pinching = false;
float g_pinchCentreX = 0.0f;
float g_pinchCentreY = 0.0f;

bool readAsset(const std::string& path, std::vector<uint8_t>& out, void*) {
    if (g_assets == nullptr) return false;
    AAsset* asset = AAssetManager_open(g_assets, path.c_str(), AASSET_MODE_BUFFER);
    if (asset == nullptr) {
        LOGE("asset missing: %s", path.c_str());
        return false;
    }
    const off64_t length = AAsset_getLength64(asset);
    if (length <= 0) {
        AAsset_close(asset);
        out.clear();
        LOGE("asset empty: %s", path.c_str());
        return false;
    }
    out.resize((size_t)length);

    // AAsset_read may return fewer bytes than asked for (and does on some
    // devices for assets inside the APK), so keep reading until the buffer is
    // full. A truncated PNG used to corrupt the decoder's input, which can
    // make the inflate loop spin forever on the GL thread.
    size_t total = 0;
    while (total < (size_t)length) {
        const int read = AAsset_read(asset, out.data() + total, (size_t)length - total);
        if (read <= 0) {
            AAsset_close(asset);
            out.clear();
            LOGE("asset read failed: %s (%zu/%lld bytes)", path.c_str(), total,
                 (long long)length);
            return false;
        }
        total += (size_t)read;
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

    const auto started = std::chrono::steady_clock::now();
    std::string note;

    GLint maxTexture = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexture);
    if (maxTexture > 0 && (atlas.width() > maxTexture || atlas.height() > maxTexture)) {
        note += " atlas exceeds GL_MAX_TEXTURE_SIZE (";
        note += std::to_string(maxTexture) + ")";
    }

    // Drain errors left over from earlier initialisation (ImGui's VAO and shader
    // setup leaves some on this driver) before checking our own upload. Reading a
    // stale flag here made the upload look like it failed, which disabled every
    // icon even though the texture was fine.
    for (int guard = 0; guard < 32 && glGetError() != GL_NO_ERROR; ++guard) {
        // discard pending errors
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (tex == 0) {
        pdb::editor().log("Could not create the icon texture; toolbar icons "
                          "disabled." + note, 2);
        atlas.setTexture(ImTextureID_Invalid);
        return;
    }
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlas.width(), atlas.height(), 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, atlas.pixels().data());
    const GLenum glError = glGetError();
    const double elapsed = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - started)
                               .count();

    // Report problems, but keep the texture: a texture that was created and
    // filled still draws, and "no icons at all" is worse than "icons that might
    // look wrong". The log carries the numbers needed to diagnose it.
    std::string summary = std::to_string(atlas.width()) + "x" +
                          std::to_string(atlas.height()) + " (" +
                          std::to_string(atlas.pixels().size() / 1024) + " KB) in " +
                          std::to_string((int)elapsed) + " ms, GL_MAX_TEXTURE_SIZE=" +
                          std::to_string(maxTexture);
    if (glError != GL_NO_ERROR) {
        note += " upload reported GL error 0x" + std::to_string((int)glError);
    }
    atlas.setTexture((ImTextureID)(size_t)tex);
    if (note.empty()) {
        pdb::editor().log("Icon atlas " + summary);
    } else {
        pdb::editor().log("Icon atlas " + summary + ";" + note + " (drawn anyway)", 1);
    }
}

// Requests raised during a frame are handled by the Activity right after the
// draw call; these helpers call back into Java.
void notifyJava(JNIEnv* env, jobject activity) {
    pdb::Editor& ed = pdb::editor();
    if (!ed.requestOpen && !ed.requestSave && !ed.requestSaveAs && !ed.requestOpenUrl &&
        !ed.showDiagnostics && !ed.requestOpenConfigFolder && !ed.requestExportManual) {
        return;
    }

    jclass clazz = env->GetObjectClass(activity);
    if (clazz == nullptr) return;

    // Help menu actions. Each is a plain Java method, so a failed lookup can only
    // mean the activity is an older build; the log says so instead of failing
    // silently.
    if (ed.requestOpenUrl) {
        jmethodID m = env->GetMethodID(clazz, "openUrl", "(Ljava/lang/String;)V");
        ed.requestOpenUrl = false;
        if (m) {
            jstring url = env->NewStringUTF(ed.pendingUrl.c_str());
            env->CallVoidMethod(activity, m, url);
            env->DeleteLocalRef(url);
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    if (ed.showDiagnostics) {
        jmethodID m = env->GetMethodID(clazz, "showDiagnosticsPanel", "()V");
        ed.showDiagnostics = false;
        if (m) env->CallVoidMethod(activity, m);
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    if (ed.requestOpenConfigFolder) {
        jmethodID m = env->GetMethodID(clazz, "showConfigFolder", "()V");
        ed.requestOpenConfigFolder = false;
        if (m) env->CallVoidMethod(activity, m);
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    if (ed.requestExportManual) {
        jmethodID m = env->GetMethodID(clazz, "exportManual", "(Ljava/lang/String;)V");
        ed.requestExportManual = false;
        if (m) {
            std::string html = buildManualExport();
            jstring text = env->NewStringUTF(html.c_str());
            env->CallVoidMethod(activity, m, text);
            env->DeleteLocalRef(text);
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
    }

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

// Help > About > "Save the manual to Documents": the desktop ships a single
// .chm; here every page is concatenated into one HTML file so it can be read in
// any browser without the app.
std::string buildManualExport() {
    std::vector<std::string> pages;
    {
        std::vector<uint8_t> index;
        if (readAsset("help-index.txt", index, nullptr)) {
            const std::string text(index.begin(), index.end());
            size_t pos = 0;
            while (pos < text.size()) {
                size_t end = text.find('\n', pos);
                if (end == std::string::npos) end = text.size();
                std::string line = text.substr(pos, end - pos);
                pos = end + 1;
                if (line.empty() || line[0] == '#') continue;
                const size_t bar = line.find('|');
                if (bar == std::string::npos) continue;
                std::string page = line.substr(0, bar);
                while (!page.empty() && (page.back() == ' ' || page.back() == '\r')) page.pop_back();
                if (page.empty()) continue;
                if (std::find(pages.begin(), pages.end(), page) == pages.end()) {
                    pages.push_back(page);
                }
            }
        }
    }

    std::string html =
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<title>PocketDoomBuilder - Reference Manual</title>"
        "<style>body{background:#fff;color:#222;font:14px/1.6 Verdana,sans-serif;"
        "max-width:900px;margin:0 auto;padding:16px}"
        "h1{color:#515151;border-bottom:2px solid #ddd;padding-bottom:4px}"
        "h2,h3{color:#33333}pre{background:#f4f4f4;color:#515151;padding:8px;"
        "overflow:auto}a{color:#1a5ab8}</style></head><body>"
        "<h1>PocketDoomBuilder Reference Manual</h1>"
        "<p>Exported from the app. Pages: " + std::to_string(pages.size()) + "</p>";
    for (const std::string& page : pages) {
        std::vector<uint8_t> bytes;
        if (!readAsset("help/" + page, bytes, nullptr)) continue;
        std::string text(bytes.begin(), bytes.end());
        // The pages are written to be shown one at a time and link to each
        // other by file name; in a single exported file those links have to
        // point at the in-document anchor instead.
        std::string patched;
        patched.reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i) {
            const bool href = text.compare(i, 6, "href=\"") == 0;
            if (!href) {
                patched += text[i];
                continue;
            }
            patched += "href=\"#";
            i += 5;
            // Copy the target page name, then close the quote.
            std::string target;
            size_t j = i + 1;
            while (j < text.size() && text[j] != '"' && text[j] != '#') target += text[j++];
            patched += target;
            patched += '"';
            i = j - 1;
            if (j < text.size() && text[j] == '#') {
                // Keep any in-page anchor after the page name.
                while (j < text.size() && text[j] != '"') {
                    if (text[j] != '#') patched += text[j];
                    ++j;
                }
                patched += '"';
                i = j;
            }
        }
        html += "<hr><div id=\"" + page + "\">";
        html += patched;
        html += "</div>";
    }
    html += "</body></html>";
    LOGI("manual export: %zu pages, %zu bytes", pages.size(), html.size());
    return html;
}

// The UI layer (manual pages, icon manifest) reads assets through this hook.
namespace pdb {
bool platformReadAsset(const std::string& path, std::vector<uint8_t>& out) {
    return readAsset(path, out, nullptr);
}
} // namespace pdb

extern "C" {

// Created on the GLSurfaceView's render thread, with its context current.
JNIEXPORT void JNICALL
Java_com_pocketdoombuilder_MainActivity_nativeCreate(JNIEnv* env, jobject thiz,
                                                     jobject assetManager, jint width,
                                                     jint height) {
    // Keep the VM and activity around: they are needed from the render thread
    // and from the crash handler's log mirror.
    if (g_vm == nullptr) env->GetJavaVM(&g_vm);
    if (g_activity == nullptr) g_activity = env->NewGlobalRef(thiz);
    if (g_appendNativeLog == nullptr && g_activity != nullptr) {
        jclass clazz = env->GetObjectClass(g_activity);
        if (clazz != nullptr) {
            g_appendNativeLog = env->GetMethodID(clazz, "appendNativeLog", "(Ljava/lang/String;)V");
            env->DeleteLocalRef(clazz);
            if (env->ExceptionCheck()) env->ExceptionClear();
        }
    }

    installCrashHandler();
    setStage("nativeCreate");
    g_assets = AAssetManager_fromJava(env, assetManager);
    g_width = width;
    g_height = height;
    LOGI("nativeCreate %dx%d", width, height);

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
    // Everything the editor reports from now on also reaches logcat and the
    // Java diagnostics log.
    ed.logSink = mirrorLog;
    ed.log("PocketDoomBuilder starting");
    ed.log("Ultimate Doom Builder interface, ported to Dear ImGui");

    setStage("ImGui::CreateContext");
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // the shell owns its layout
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.DisplaySize = ImVec2((float)width, (float)height);

    setStage("applyTheme");
    pdb::applyTheme(1.0f);
    setStage("loadFonts");
    loadFonts();
    setStage("ImGui_ImplOpenGL3_Init");
    installWorkAreaInsets();
    if (!ImGui_ImplOpenGL3_Init("#version 300 es")) {
        ed.log("OpenGL backend failed to initialise (no GLES3 context?)", 2);
    }

    setStage("beginIconLoad");
    beginIconLoad();

    g_imguiReady = true;
    setStage("ready");
    ed.log("ImGui context ready, " + std::to_string(g_startup.entries.size()) +
           " icons queued");
    (void)thiz;
}

// The Java side passes its getFilesDir() so a crash report can be written
// somewhere the next launch can read it.
JNIEXPORT void JNICALL
Java_com_pocketdoombuilder_MainActivity_nativeSetFilesDir(JNIEnv* env, jobject thiz,
                                                          jstring path) {
    if (path == nullptr) return;
    const char* chars = env->GetStringUTFChars(path, nullptr);
    if (chars != nullptr) {
        g_filesDir = chars;
        env->ReleaseStringUTFChars(path, chars);
        LOGI("files dir: %s", g_filesDir.c_str());
    }
    (void)thiz;
}

// ImGui asks for the work-area insets once per frame; ImGui's main viewport work
// rect (which the whole shell lays itself out in) is derived from these. This is
// the supported way to keep the interface out of the status bar, display cutout
// and gesture bar.
namespace {
ImVec4 getWorkAreaInsets(ImGuiViewport* viewport) {
    (void)viewport;
    return ImVec4((float)g_insetLeft, (float)g_insetTop, (float)g_insetRight,
                  (float)g_insetBottom);
}

void installWorkAreaInsets() {
    ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
    platformIo.Platform_GetWindowWorkAreaInsets = getWorkAreaInsets;
}
} // namespace

// Called from Java whenever the safe-area setting or the window insets change.
JNIEXPORT void JNICALL
Java_com_pocketdoombuilder_MainActivity_nativeSetFrameInsets(JNIEnv*, jobject, jint top,
                                                             jint bottom, jint left,
                                                             jint right) {
    g_insetTop = top;
    g_insetBottom = bottom;
    g_insetLeft = left;
    g_insetRight = right;
    LOGI("frame insets: t=%d b=%d l=%d r=%d", top, bottom, left, right);
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
    if (!g_imguiReady) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            LOGE("nativeRender called before the shell was ready");
            setStage("render-before-ready");
        }
        return;
    }
    if (g_shutdownRequested) {
        // Teardown happens here because ImGui's GL objects need the context.
        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext();
        g_imguiReady = false;
        g_shutdownRequested = false;
        return;
    }

    if (g_startup.active) {
        static std::vector<std::string> warnings;
        const bool done = advanceIconLoad(24, warnings);
        const int percent = g_startup.entries.empty()
                                ? 100
                                : (int)(g_startup.next * 100 / g_startup.entries.size());
        if (!done) {
            drawStartupFrame(g_width, g_height, percent);
            notifyJava(env, thiz); // keeps the startup watchdog quiet
            return;
        }
        for (const std::string& w : warnings) {
            pdb::editor().log(w, 1);
        }
        setStage("buildIconAtlas");
        pdb::editor().icons.build(g_startup.pixels, g_startup.widths, g_startup.heights);
        g_startup.pixels.clear();
        g_startup.widths.clear();
        g_startup.heights.clear();
        g_startup.entries.clear();
        setStage("uploadIconTexture");
        uploadIconTexture();
        g_startup.active = false;
        setStage("ready");
        pdb::editor().log("Toolbar ready (" +
                          std::to_string(pdb::editor().icons.names().size()) + " icons)");
        drawStartupFrame(g_width, g_height, 100);
        notifyJava(env, thiz);
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)g_width, (float)g_height);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    setStage("drawMainWindow");
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
Java_com_pocketdoombuilder_MainActivity_nativeDestroy(JNIEnv* env, jobject) {
    g_shutdownRequested = true;
    setStage("destroy");
    LOGI("shutdown requested");
    if (g_activity != nullptr) {
        env->DeleteGlobalRef(g_activity);
        g_activity = nullptr;
    }
    g_appendNativeLog = nullptr;
}

} // extern "C"
