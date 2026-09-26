#pragma once

// Android Platform: an EGL (OpenGL ES 3.0) context on the GameActivity's
// window, key events translated through input::android's key map, app-private
// storage for data and the extracted assets, one fixed fullscreen display.
//
// The EGL *context* outlives the window: when the window goes away (screen
// off, lid closed, app backgrounded) only the surface is destroyed and the
// context is kept, so every GL object App holds stays valid and a returning
// window just needs a new surface. If the driver does lose the context anyway
// (EGL_CONTEXT_LOST at swap time), contextLost() turns true and the caller
// rebuilds App from scratch.

#include <EGL/egl.h>
#include <jni.h>

#include <string>
#include <vector>

#include "input/gamepad_input.h"
#include "input/input_action.h"
#include "platform/platform.h"

struct android_app;

class AndroidPlatform final : public Platform {
public:
    explicit AndroidPlatform(android_app* app);

    ~AndroidPlatform() override;

    AndroidPlatform(const AndroidPlatform&) = delete;
    AndroidPlatform& operator=(const AndroidPlatform&) = delete;

    // --- Window lifecycle (driven by android_main) -----------------------
    // Creates the EGL context if there is none, a surface for the app's
    // current window, and makes them current. Returns false (having logged
    // why) on failure.
    bool attachWindow();
    // Destroys the surface; the context stays, current without a surface.
    void detachWindow();
    bool hasWindow() const { return surface_ != EGL_NO_SURFACE; }
    // Destroys the context too, to start over after a loss.
    void destroyContext();
    bool contextLost() const { return contextLost_; }

    // Raw input from the activity's input buffer. Auto-repeat, hysteresis and
    // long-press detection all happen in the (platform-free) PadTranslator.
    void queueKeyEvent(int keyCode, int action);
    void queueAxis(input::PadAxis axis, float value);
    // Forget everything held and drop what is queued: the app is losing the
    // focus and the matching key-up events will never arrive.
    void resetInput();
    // Drop queued events (input that arrives while nothing is drawn).
    void discardEvents();

    // --- Platform --------------------------------------------------------
    double now() const override;
    void framebufferSize(int& width, int& height) const override;
    void* glProcAddress(const char* name) const override;
    const char* glslVersion() const override { return "#version 300 es"; }
    void swapBuffers() override;

    bool imguiInit() override;
    void imguiNewFrame() override;
    void imguiShutdown() override;

    void pollEvents(std::vector<input::InputEvent>& out) override;
    bool translateKeyName(std::string_view name, std::vector<input::InputEvent>& out) const override;

    // Config and the unpacked assets live in the app's private files
    // directory; the OS-clearable page caches in its cache directory.
    std::string dataDir() const override { return dataDir_; }
    std::string assetDir() const override { return dataDir_ + "/assets"; }
    std::string cacheDir() const override { return cacheDir_; }
    // Movies and Music on the primary storage plus the root of every mounted
    // removable volume (SD card, USB), from StorageManager.
    std::vector<std::string> defaultMediaRoots() const override;
    // "All files access" (MANAGE_EXTERNAL_STORAGE), so std::filesystem and
    // libmpv can use plain paths on shared storage.
    bool storageAccessGranted() const override;
    void requestStorageAccess() override;

    void setKeepAwake(bool on) override;
    void requestQuit() override;
    bool quitRequested() const override;
    bool backQuitsAtRootMenu() const override { return false; }

    bool supportsWindowModes() const override { return false; }
    // From DisplayManager via the Activity: "Primary" first, then any other
    // connected displays. Nothing lets the user pick one yet.
    std::vector<std::string> displayNames() const override;
    bool hasLaunchDisplaySetting() const override { return true; }
    // Off until MediaCodec decoding has proven itself on the target device:
    // software decoding of 1080p60 is well within a Snapdragon 8 Gen 2.
    bool defaultHardwareDecoding() const override { return false; }
    void setFullscreen(bool /*on*/, int /*displayIndex*/) override {}

private:
    bool createContext();

    // Calls a no-argument method of the Java Activity by name (see
    // MainActivity.kt). The native thread is attached to the JVM (env_).
    std::string activityString(const char* method) const;
    std::vector<std::string> activityStrings(const char* method) const;
    bool activityBool(const char* method) const;
    void activityVoid(const char* method) const;

    android_app* app_;
    JNIEnv* env_ = nullptr;  // this thread's, attached in the constructor
    std::string dataDir_;
    std::string cacheDir_;
    input::PadTranslator pad_;
    mutable uint32_t nextSimulatedGroup_ = 1;

    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLConfig config_ = nullptr;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLSurface surface_ = EGL_NO_SURFACE;
    bool contextLost_ = false;
    bool quitRequested_ = false;
    bool keepAwake_ = false;
    bool imguiActive_ = false;
};
