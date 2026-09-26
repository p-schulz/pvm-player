#include "platform/android/android_platform.h"

#include <android/log.h>
#include <game-activity/GameActivity.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <imgui.h>
#include <imgui_impl_android.h>

#include <ctime>

#include "platform/android/keymap_android.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "PVM", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "PVM", __VA_ARGS__)

AndroidPlatform::AndroidPlatform(android_app* app)
    : app_(app),
      dataDir_(app->activity->internalDataPath ? app->activity->internalDataPath : "."),
      keyMap_(input::android::defaultKeyMap()) {
    // GameActivity's own JNIEnv is only valid on the Java main thread; this
    // (native) thread needs to be attached to the JVM to call into Java.
    if (app->activity->vm->AttachCurrentThread(&env_, nullptr) != JNI_OK) {
        LOGE("AttachCurrentThread failed; calls into Java are unavailable");
        env_ = nullptr;
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

void AndroidPlatform::queueKeyEvent(int keyCode, int action, int repeatCount) {
    const std::optional<input::Phase> phase = input::android::phaseFromKeyEvent(action, repeatCount);
    if (!phase) {
        return;
    }
    // One physical key may emit several actions; each screen reacts to at
    // most one of them.
    for (const input::Action a : keyMap_.actionsFor(keyCode)) {
        pending_.push_back(input::InputEvent{a, *phase});
    }
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
    // The OS events themselves are pumped by android_main's loop.
    out.insert(out.end(), pending_.begin(), pending_.end());
    pending_.clear();
}

bool AndroidPlatform::translateKeyName(std::string_view name, std::vector<input::InputEvent>& out) const {
    const std::optional<int> code = input::android::codeFromName(name);
    if (!code) {
        return false;
    }
    for (const input::Action action : keyMap_.actionsFor(*code)) {
        out.push_back(input::InputEvent{action, input::Phase::Press});
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

void AndroidPlatform::requestQuit() {
    if (!quitRequested_) {
        quitRequested_ = true;
        GameActivity_finish(app_->activity);
    }
}

bool AndroidPlatform::quitRequested() const {
    return quitRequested_ || app_->destroyRequested != 0;
}
