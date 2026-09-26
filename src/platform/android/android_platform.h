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

#include "input/input_action.h"
#include "input/keymap.h"
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

    // Key event from the activity's input buffer.
    void queueKeyEvent(int keyCode, int action, int repeatCount);
    void discardEvents() { pending_.clear(); }

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

    std::string dataDir() const override { return dataDir_; }
    std::string assetDir() const override { return dataDir_ + "/assets"; }
    std::vector<std::string> defaultMediaRoots() const override { return {"/storage/emulated/0"}; }

    void setKeepAwake(bool on) override;
    void requestQuit() override;
    bool quitRequested() const override;

    bool supportsWindowModes() const override { return false; }
    std::vector<std::string> displayNames() const override { return {"Primary"}; }
    void setFullscreen(bool /*on*/, int /*displayIndex*/) override {}

private:
    bool createContext();

    android_app* app_;
    JNIEnv* env_ = nullptr;  // this thread's, attached in the constructor
    std::string dataDir_;
    input::KeyMap keyMap_;
    std::vector<input::InputEvent> pending_;

    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLConfig config_ = nullptr;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLSurface surface_ = EGL_NO_SURFACE;
    bool contextLost_ = false;
    bool quitRequested_ = false;
    bool keepAwake_ = false;
    bool imguiActive_ = false;
};
