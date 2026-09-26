#pragma once

// Desktop Platform (macOS, Windows, Linux/Pi): a GLFW window with an OpenGL
// 3.3 core context, keyboard input through input::glfw's key map, and the
// executable's own directory for both data and assets.

#include <string>
#include <vector>

#include "input/input_action.h"
#include "input/keymap.h"
#include "platform/platform.h"

struct GLFWwindow;
struct GLFWmonitor;

class GlfwPlatform final : public Platform {
public:
    GlfwPlatform();
    ~GlfwPlatform() override;

    GlfwPlatform(const GlfwPlatform&) = delete;
    GlfwPlatform& operator=(const GlfwPlatform&) = delete;

    // Creates the window and makes its GL context current. Returns false
    // (having logged why) on failure.
    bool init(int width, int height, const char* title);

    double now() const override;
    void framebufferSize(int& width, int& height) const override;
    void* glProcAddress(const char* name) const override;
    const char* glslVersion() const override { return "#version 330 core"; }
    void swapBuffers() override;

    bool imguiInit() override;
    void imguiNewFrame() override;
    void imguiShutdown() override;

    void pollEvents(std::vector<input::InputEvent>& out) override;
    bool translateKeyName(std::string_view name, std::vector<input::InputEvent>& out) const override;

    std::string dataDir() const override { return exeDir_; }
    std::string assetDir() const override { return exeDir_; }
    std::vector<std::string> defaultMediaRoots() const override { return {"."}; }

    void setKeepAwake(bool on) override;
    void requestQuit() override;
    bool quitRequested() const override;

    bool supportsWindowModes() const override { return true; }
    std::vector<std::string> displayNames() const override;
    void setFullscreen(bool on, int displayIndex) override;

private:
    // keyMap_ = the GLFW defaults plus any overrides from keys.cfg in
    // dataDir() (see conf/keys.example.cfg).
    void loadKeyMap();
    GLFWmonitor* resolveMonitor(int displayIndex) const;

    static void errorCallback(int error, const char* description);
    static void framebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

    std::string exeDir_;
    GLFWwindow* window_ = nullptr;
    bool glfwInitialized_ = false;
    bool imguiInitialized_ = false;
    input::KeyMap keyMap_;

    // Filled by the GLFW callbacks (and the macOS media-key tap) while
    // glfwPollEvents() runs; drained by pollEvents().
    std::vector<input::InputEvent> pending_;
    mutable uint32_t nextGroup_ = 1;  // InputEvent::group: one per key event

    // Geometry to restore when leaving fullscreen: captured at window
    // creation and again whenever fullscreen is entered from a windowed
    // state, so toggling back lands where the window was.
    bool fullscreen_ = false;
    int windowedX_ = 0;
    int windowedY_ = 0;
    int windowedWidth_ = 1280;
    int windowedHeight_ = 720;
};
