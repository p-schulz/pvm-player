#include "platform/android/android_platform.h"

#include <android/log.h>
#include <game-activity/GameActivity.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <imgui.h>
#include <imgui_impl_android.h>

#include <android/input.h>

#include <cstdio>
#include <ctime>
#include <fstream>

#include "platform/android/keymap_android.h"

namespace {

// The default bindings plus the overrides in <dataDir>/keys.cfg, if any.
input::KeyMap loadKeyMap(const std::string& dataDir) {
    input::KeyMap map = input::android::defaultKeyMap();
    const std::string path = dataDir + "/keys.cfg";
    std::ifstream file(path);
    if (!file) {
        return map;  // optional; the defaults cover every action
    }
    for (const std::string& warning : map.loadOverrides(file, input::android::codeFromName, path)) {
        std::fprintf(stderr, "%s\n", warning.c_str());
    }
    std::fprintf(stdout, "Loaded key bindings from %s\n", path.c_str());
    return map;
}

}  // namespace

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "PVM", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "PVM", __VA_ARGS__)

AndroidPlatform::AndroidPlatform(android_app* app)
    : app_(app),
      dataDir_(app->activity->internalDataPath ? app->activity->internalDataPath : "."),
      pad_(loadKeyMap(dataDir_), input::defaultAnalogBindings()) {
    // GameActivity's own JNIEnv is only valid on the Java main thread; this
    // (native) thread needs to be attached to the JVM to call into Java.
    if (app->activity->vm->AttachCurrentThread(&env_, nullptr) != JNI_OK) {
        LOGE("AttachCurrentThread failed; calls into Java are unavailable");
        env_ = nullptr;
    }
    cacheDir_ = activityString("cachePath");
    if (cacheDir_.empty()) {
        cacheDir_ = dataDir_ + "/cache";
    }
}

AndroidPlatform::~AndroidPlatform() {
    destroyContext();
    if (display_ != EGL_NO_DISPLAY) {
        eglTerminate(display_);
        display_ = EGL_NO_DISPLAY;
    }
    if (env_) {
        app_->activity->vm->DetachCurrentThread();
    }
}

bool AndroidPlatform::createContext() {
    if (display_ == EGL_NO_DISPLAY) {
        display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (display_ == EGL_NO_DISPLAY || !eglInitialize(display_, nullptr, nullptr)) {
            LOGE("eglInitialize failed (0x%x)", eglGetError());
            display_ = EGL_NO_DISPLAY;
            return false;
        }
    }

    // ES 3.0, RGBA8, no depth or stencil: the app draws 2D into its own FBO.
    const EGLint configAttribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 0,
        EGL_STENCIL_SIZE, 0,
        EGL_NONE,
    };
    EGLint numConfigs = 0;
    if (!eglChooseConfig(display_, configAttribs, &config_, 1, &numConfigs) || numConfigs < 1) {
        LOGE("No EGL config for ES 3.0 RGBA8 (0x%x)", eglGetError());
        return false;
    }

    const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    context_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT, contextAttribs);
    if (context_ == EGL_NO_CONTEXT) {
        LOGE("eglCreateContext failed (0x%x)", eglGetError());
        return false;
    }
    contextLost_ = false;
    return true;
}

bool AndroidPlatform::attachWindow() {
    if (!app_->window) {
        return false;
    }
    if (context_ == EGL_NO_CONTEXT && !createContext()) {
        return false;
    }
    if (surface_ != EGL_NO_SURFACE) {
        detachWindow();
    }

    surface_ = eglCreateWindowSurface(display_, config_, app_->window, nullptr);
    if (surface_ == EGL_NO_SURFACE) {
        LOGE("eglCreateWindowSurface failed (0x%x)", eglGetError());
        return false;
    }
    if (!eglMakeCurrent(display_, surface_, surface_, context_)) {
        LOGE("eglMakeCurrent failed (0x%x)", eglGetError());
        eglDestroySurface(display_, surface_);
        surface_ = EGL_NO_SURFACE;
        return false;
    }
    eglSwapInterval(display_, 1);  // vsync

    // ImGui's Android backend holds the ANativeWindow it was initialised
    // with, so a new window means a fresh init.
    if (imguiActive_) {
        ImGui_ImplAndroid_Shutdown();
        ImGui_ImplAndroid_Init(app_->window);
    }
    return true;
}

void AndroidPlatform::detachWindow() {
    if (display_ == EGL_NO_DISPLAY) {
        return;
    }
    // Keep the context current without a surface (EGL_KHR_surfaceless_context,
    // available on every Android EGL that has ES 3), so App can still free GL
    // objects while there is no window.
    if (context_ != EGL_NO_CONTEXT) {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, context_);
    } else {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    if (surface_ != EGL_NO_SURFACE) {
        eglDestroySurface(display_, surface_);
        surface_ = EGL_NO_SURFACE;
    }
}

void AndroidPlatform::destroyContext() {
    if (display_ == EGL_NO_DISPLAY) {
        return;
    }
    eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (surface_ != EGL_NO_SURFACE) {
        eglDestroySurface(display_, surface_);
        surface_ = EGL_NO_SURFACE;
    }
    if (context_ != EGL_NO_CONTEXT) {
        eglDestroyContext(display_, context_);
        context_ = EGL_NO_CONTEXT;
    }
}

void AndroidPlatform::queueKeyEvent(int keyCode, int action) {
    if (action == AKEY_EVENT_ACTION_DOWN) {
        pad_.keyDown(keyCode, now());  // the OS's own repeats are ignored inside
    } else if (action == AKEY_EVENT_ACTION_UP) {
        pad_.keyUp(keyCode, now());
    }
}

void AndroidPlatform::queueAxis(input::PadAxis axis, float value) {
    pad_.setAxis(axis, value, now());
}

void AndroidPlatform::resetInput() {
    pad_.releaseAll();
    discardEvents();
}

void AndroidPlatform::discardEvents() {
    std::vector<input::InputEvent> dropped;
    pad_.drain(dropped);
}

double AndroidPlatform::now() const {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
}

void AndroidPlatform::framebufferSize(int& width, int& height) const {
    EGLint w = 0, h = 0;
    if (surface_ != EGL_NO_SURFACE) {
        eglQuerySurface(display_, surface_, EGL_WIDTH, &w);
        eglQuerySurface(display_, surface_, EGL_HEIGHT, &h);
    }
    width = w;
    height = h;
}

void* AndroidPlatform::glProcAddress(const char* name) const {
    return reinterpret_cast<void*>(eglGetProcAddress(name));
}

void AndroidPlatform::swapBuffers() {
    if (surface_ == EGL_NO_SURFACE) {
        return;
    }
    if (!eglSwapBuffers(display_, surface_)) {
        const EGLint error = eglGetError();
        if (error == EGL_CONTEXT_LOST) {
            LOGE("EGL context lost");
            contextLost_ = true;
        } else {
            // e.g. EGL_BAD_SURFACE while the window is being torn down; the
            // next APP_CMD_TERM_WINDOW/INIT_WINDOW pair sorts it out.
            LOGE("eglSwapBuffers failed (0x%x)", error);
        }
    }
}

bool AndroidPlatform::imguiInit() {
    // Only display size and time come from ImGui's Android backend; its
    // input handling is not used (see pollEvents()).
    imguiActive_ = ImGui_ImplAndroid_Init(app_->window);
    return imguiActive_;
}

void AndroidPlatform::imguiNewFrame() {
    ImGui_ImplAndroid_NewFrame();
}

void AndroidPlatform::imguiShutdown() {
    if (imguiActive_) {
        ImGui_ImplAndroid_Shutdown();
        imguiActive_ = false;
    }
}

void AndroidPlatform::pollEvents(std::vector<input::InputEvent>& out) {
    // The OS events themselves are pumped by android_main's loop; here the
    // translator's timers (repeats, long presses, analog ticks) advance.
    pad_.update(now());
    pad_.drain(out);
}

bool AndroidPlatform::translateKeyName(std::string_view name, std::vector<input::InputEvent>& out) const {
    const std::optional<int> code = input::android::codeFromName(name);
    if (!code) {
        return false;
    }
    // A key name stands for one press, so its actions share a group.
    const uint32_t group = 0x80000000u | nextSimulatedGroup_++;
    for (const input::Action action : pad_.keyMap().actionsFor(*code)) {
        out.push_back(input::InputEvent{action, input::Phase::Press, 1.0f, group});
    }
    return true;
}

void AndroidPlatform::setKeepAwake(bool on) {
    if (on == keepAwake_) {
        return;
    }
    keepAwake_ = on;
    LOGI("keep screen on: %s", on ? "yes" : "no");

    // The flag has to be changed on the UI thread, which the Kotlin side
    // takes care of.
    JNIEnv* env = env_;
    if (!env) {
        return;
    }
    jobject activity = app_->activity->javaGameActivity;
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID method = env->GetMethodID(activityClass, "keepScreenOn", "(Z)V");
    if (method) {
        env->CallVoidMethod(activity, method, static_cast<jboolean>(on));
    }
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        LOGE("keepScreenOn() failed");
    }
    env->DeleteLocalRef(activityClass);
}

namespace {

std::string toString(JNIEnv* env, jstring value) {
    if (!value) {
        return {};
    }
    const char* chars = env->GetStringUTFChars(value, nullptr);
    std::string result = chars ? chars : "";
    if (chars) {
        env->ReleaseStringUTFChars(value, chars);
    }
    return result;
}

// Clears (and reports) a pending Java exception; true if there was one.
bool clearException(JNIEnv* env, const char* what) {
    if (!env->ExceptionCheck()) {
        return false;
    }
    env->ExceptionDescribe();
    env->ExceptionClear();
    LOGE("%s failed", what);
    return true;
}

}  // namespace

std::string AndroidPlatform::activityString(const char* method) const {
    JNIEnv* env = env_;
    if (!env) {
        return {};
    }
    jobject activity = app_->activity->javaGameActivity;
    jclass activityClass = env->GetObjectClass(activity);
    std::string result;
    if (jmethodID id = env->GetMethodID(activityClass, method, "()Ljava/lang/String;")) {
        auto value = static_cast<jstring>(env->CallObjectMethod(activity, id));
        if (!clearException(env, method)) {
            result = toString(env, value);
        }
        if (value) env->DeleteLocalRef(value);
    }
    clearException(env, method);
    env->DeleteLocalRef(activityClass);
    return result;
}

std::vector<std::string> AndroidPlatform::activityStrings(const char* method) const {
    JNIEnv* env = env_;
    std::vector<std::string> result;
    if (!env) {
        return result;
    }
    jobject activity = app_->activity->javaGameActivity;
    jclass activityClass = env->GetObjectClass(activity);
    if (jmethodID id = env->GetMethodID(activityClass, method, "()[Ljava/lang/String;")) {
        auto array = static_cast<jobjectArray>(env->CallObjectMethod(activity, id));
        if (!clearException(env, method) && array) {
            const jsize count = env->GetArrayLength(array);
            for (jsize i = 0; i < count; ++i) {
                auto value = static_cast<jstring>(env->GetObjectArrayElement(array, i));
                result.push_back(toString(env, value));
                if (value) env->DeleteLocalRef(value);
            }
        }
        if (array) env->DeleteLocalRef(array);
    }
    clearException(env, method);
    env->DeleteLocalRef(activityClass);
    return result;
}

bool AndroidPlatform::activityBool(const char* method) const {
    JNIEnv* env = env_;
    if (!env) {
        return false;
    }
    jobject activity = app_->activity->javaGameActivity;
    jclass activityClass = env->GetObjectClass(activity);
    bool result = false;
    if (jmethodID id = env->GetMethodID(activityClass, method, "()Z")) {
        const jboolean value = env->CallBooleanMethod(activity, id);
        result = !clearException(env, method) && value == JNI_TRUE;
    }
    clearException(env, method);
    env->DeleteLocalRef(activityClass);
    return result;
}

void AndroidPlatform::activityVoid(const char* method) const {
    JNIEnv* env = env_;
    if (!env) {
        return;
    }
    jobject activity = app_->activity->javaGameActivity;
    jclass activityClass = env->GetObjectClass(activity);
    if (jmethodID id = env->GetMethodID(activityClass, method, "()V")) {
        env->CallVoidMethod(activity, id);
    }
    clearException(env, method);
    env->DeleteLocalRef(activityClass);
}

std::vector<std::string> AndroidPlatform::displayNames() const {
    std::vector<std::string> names = activityStrings("displayNames");
    if (names.empty()) {
        names.push_back("Primary");
    }
    return names;
}

std::vector<std::string> AndroidPlatform::defaultMediaRoots() const {
    std::vector<std::string> roots = activityStrings("mediaRoots");
    if (roots.empty()) {
        roots.push_back("/storage/emulated/0");
    }
    return roots;
}

bool AndroidPlatform::storageAccessGranted() const {
    return activityBool("hasStorageAccess");
}

void AndroidPlatform::requestStorageAccess() {
    activityVoid("requestStorageAccess");
}

void AndroidPlatform::requestQuit() {
    if (!quitRequested_) {
        quitRequested_ = true;
        GameActivity_finish(app_->activity);
    }
}

bool AndroidPlatform::quitRequested() const {
    return quitRequested_ || app_->destroyRequested != 0;
}
