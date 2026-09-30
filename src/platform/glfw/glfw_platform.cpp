#include "platform/glfw/glfw_platform.h"

#include "gl.h"
// GLFW must be included after glad so it doesn't pull in its own GL headers.
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

#include "input/keymap_persist.h"
#include "platform/glfw/keymap_glfw.h"
#include "platform/glfw/paths.h"

#ifdef __APPLE__
#include "platform/glfw/macos_media_keys.h"
#include "platform/glfw/macos_sleep_guard.h"
#endif

#if defined(_WIN32)
// Without this, windows.h's min/max macros would shadow any std::min/
// std::max used in this file or a header it pulls in (see the identical
// guard in retro_core.cpp/audio_out.cpp, added after this exact collision
// broke the Windows build there).
#define NOMINMAX
#include <windows.h>
#endif

GlfwPlatform::GlfwPlatform() : exeDir_(exeDir()) {}

GlfwPlatform::~GlfwPlatform() {
#ifdef __APPLE__
    macos_media_keys::shutdown();
    macos_sleep_guard::shutdown();
#endif
    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    if (glfwInitialized_) {
        glfwTerminate();
    }
}

bool GlfwPlatform::init(int width, int height, const char* title) {
    glfwSetErrorCallback(errorCallback);

    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialize GLFW\n");
        return false;
    }
    glfwInitialized_ = true;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window_) {
        std::fprintf(stderr, "Failed to create GLFW window\n");
        return false;
    }

    // Remembered as the "restore to windowed" geometry if fullscreen gets
    // turned on later.
    windowedWidth_ = width;
    windowedHeight_ = height;
    glfwGetWindowPos(window_, &windowedX_, &windowedY_);

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);  // vsync

    if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress))) {
        std::fprintf(stderr, "Failed to initialize glad (load GL function pointers)\n");
        return false;
    }

    glfwSetFramebufferSizeCallback(window_, framebufferSizeCallback);
    glfwSetWindowUserPointer(window_, this);
    glfwSetKeyCallback(window_, &GlfwPlatform::keyCallback);

    std::fprintf(stdout, "OpenGL: %s / GLSL: %s / Renderer: %s\n",
                 glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION),
                 glGetString(GL_RENDERER));

    loadKeyMap();
    pad_ = std::make_unique<input::PadTranslator>(keyMap_, input::defaultAnalogBindings());

#ifdef __APPLE__
    // Lets the hardware Play/Pause media key act as PlayPause in addition to
    // Space, regardless of which screen is showing (the action is simply
    // ignored where it means nothing).
    macos_media_keys::install(
        [this]() { pending_.push_back(input::InputEvent{input::Action::PlayPause, input::Phase::Press}); });
#endif
    return true;
}

double GlfwPlatform::now() const {
    return glfwGetTime();
}

void GlfwPlatform::framebufferSize(int& width, int& height) const {
    glfwGetFramebufferSize(window_, &width, &height);
}

void* GlfwPlatform::glProcAddress(const char* name) const {
    return reinterpret_cast<void*>(glfwGetProcAddress(name));
}

void GlfwPlatform::swapBuffers() {
    glfwSwapBuffers(window_);
}

bool GlfwPlatform::imguiInit() {
    // Installs ImGui's own GLFW callbacks, chaining the key callback set in
    // init() -- which is why init() must have run first.
    imguiInitialized_ = ImGui_ImplGlfw_InitForOpenGL(window_, /*install_callbacks=*/true);
    return imguiInitialized_;
}

void GlfwPlatform::imguiNewFrame() {
    ImGui_ImplGlfw_NewFrame();
}

void GlfwPlatform::imguiShutdown() {
    if (imguiInitialized_) {
        ImGui_ImplGlfw_Shutdown();
        imguiInitialized_ = false;
    }
}

void GlfwPlatform::pollEvents(std::vector<input::InputEvent>& out) {
    glfwPollEvents();
    out.insert(out.end(), pending_.begin(), pending_.end());
    pending_.clear();
    pollGamepad(glfwGetTime());
    const size_t first = out.size();
    pad_->drain(out);
    // Keep the translator's press groups apart from the keyboard's.
    for (size_t i = first; i < out.size(); ++i) {
        if (out[i].group != 0) {
            out[i].group += 1u << 24;
        }
    }
}

void GlfwPlatform::pollGamepad(double now) {
    int jid = -1;
    for (int candidate = GLFW_JOYSTICK_1; candidate <= GLFW_JOYSTICK_LAST; ++candidate) {
        if (glfwJoystickIsGamepad(candidate)) {
            jid = candidate;
            break;
        }
    }
    GLFWgamepadstate state;
    if (jid < 0 || !glfwGetGamepadState(jid, &state)) {
        if (padWasPresent_) {
            padWasPresent_ = false;  // unplugged: nothing stays pressed
            padButtons_.fill(false);
            triggerSeenRest_.fill(false);
            pad_->releaseAll();
        }
        return;
    }
    padWasPresent_ = true;

    for (int b = 0; b < static_cast<int>(padButtons_.size()); ++b) {
        const bool down = state.buttons[b] == GLFW_PRESS;
        if (down != padButtons_[static_cast<size_t>(b)]) {
            padButtons_[static_cast<size_t>(b)] = down;
            if (capturing_) {
                // Swallowed: capture cares only about the first nameable
                // button pressed, and none of it should also act as
                // gameplay/navigation input while the Controls menu is
                // waiting for it.
                if (down) {
                    const int code = input::glfw::padCode(b);
                    if (const std::optional<std::string> name = input::glfw::nameFromCode(code)) {
                        capturedInput_ = CapturedInput{code, *name};
                        capturing_ = false;
                    }
                }
                continue;
            }
            if (down) {
                pad_->keyDown(input::glfw::padCode(b), now);
            } else {
                pad_->keyUp(input::glfw::padCode(b), now);
            }
        }
    }
    if (capturing_) {
        return;  // sticks/triggers are never capturable -- see Platform::beginInputCapture()
    }
    pad_->setAxis(input::PadAxis::LeftX, state.axes[GLFW_GAMEPAD_AXIS_LEFT_X], now);
    pad_->setAxis(input::PadAxis::LeftY, state.axes[GLFW_GAMEPAD_AXIS_LEFT_Y], now);
    pad_->setAxis(input::PadAxis::RightX, state.axes[GLFW_GAMEPAD_AXIS_RIGHT_X], now);
    pad_->setAxis(input::PadAxis::RightY, state.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y], now);
    // Triggers rest at -1 and travel to +1; PadTranslator wants 0..1. A
    // trigger that has not yet been seen at rest is ignored (some drivers
    // report 0 until it is first touched, which would read as half-pulled).
    const int triggerAxes[2] = {GLFW_GAMEPAD_AXIS_LEFT_TRIGGER, GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER};
    const input::PadAxis padTriggers[2] = {input::PadAxis::LeftTrigger, input::PadAxis::RightTrigger};
    for (int t = 0; t < 2; ++t) {
        const float raw = state.axes[triggerAxes[t]];
        if (raw < -0.9f) {
            triggerSeenRest_[static_cast<size_t>(t)] = true;
        }
        const float value = triggerSeenRest_[static_cast<size_t>(t)] ? (raw + 1.0f) * 0.5f : 0.0f;
        pad_->setAxis(padTriggers[t], value, now);
    }
    pad_->update(now);
}

void GlfwPlatform::gamepadStick(float& leftX, float& leftY, float& rightX, float& rightY) const {
    leftX = pad_->axis(input::PadAxis::LeftX);
    leftY = pad_->axis(input::PadAxis::LeftY);
    rightX = pad_->axis(input::PadAxis::RightX);
    rightY = pad_->axis(input::PadAxis::RightY);
    input::applyStickDeadzone(leftX, leftY);
    input::applyStickDeadzone(rightX, rightY);
}

std::vector<std::string> GlfwPlatform::coreDirs() const {
    std::vector<std::string> dirs;
    if (const char* env = std::getenv("PVM_CORES_DIR")) {
        dirs.push_back(env);
    }
    dirs.push_back(exeDir_ + "/cores");
#ifdef PVM_DEV_CORES_DIR
    dirs.push_back(PVM_DEV_CORES_DIR);
#endif
    return dirs;
}

std::vector<std::string> GlfwPlatform::availableDrives() const {
    std::vector<std::string> drives;
#if defined(_WIN32)
    // Fills `buf` with each drive's root path ("C:\", "D:\", ...) back to
    // back, each NUL-terminated, with one more NUL after the last one --
    // e.g. "C:\\0D:\\0\0". A drive letter with no media in it (an empty
    // CD/DVD drive) still appears here; that's fine, FileBrowser::refresh()
    // already tolerates a directory it can't list (just no entries), same
    // as any other unreadable directory.
    char buf[256];
    const DWORD len = GetLogicalDriveStringsA(sizeof(buf), buf);
    for (const char* p = buf; p < buf + len && *p != '\0'; p += std::strlen(p) + 1) {
        drives.emplace_back(p);
    }
#endif
    return drives;
}

bool GlfwPlatform::translateKeyName(std::string_view name, std::vector<input::InputEvent>& out) const {
    const std::optional<int> code = input::glfw::codeFromName(name);
    if (!code) {
        return false;
    }
    const uint32_t group = nextGroup_++;
    for (const input::Action action : keyMap_.actionsFor(*code)) {
        out.push_back(input::InputEvent{action, input::Phase::Press, 1.0f, group});
    }
    return true;
}

void GlfwPlatform::setKeepAwake(bool on) {
#ifdef __APPLE__
    // mpv normally inhibits idle sleep itself, but that lives in its own
    // video-output window; this app renders through the libmpv render API,
    // so it holds an IOKit assertion of its own (see macos_sleep_guard.h).
    macos_sleep_guard::setActive(on);
#else
    (void)on;
#endif
}

void GlfwPlatform::requestQuit() {
    if (window_) {
        glfwSetWindowShouldClose(window_, GLFW_TRUE);
    }
}

bool GlfwPlatform::quitRequested() const {
    return !window_ || glfwWindowShouldClose(window_);
}

std::vector<std::string> GlfwPlatform::displayNames() const {
    std::vector<std::string> names{"Primary"};
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    for (int i = 0; i < count; ++i) {
        const char* name = glfwGetMonitorName(monitors[i]);
        names.push_back(name ? name : ("Monitor " + std::to_string(i + 1)));
    }
    return names;
}

// Switches between fullscreen (borderless, covering the chosen monitor) and
// windowed via GLFW's monitor association -- no window or GL context
// recreation, so all GL/mpv/ImGui state stays valid.
void GlfwPlatform::setFullscreen(bool on, int displayIndex) {
    if (!window_) {
        return;
    }
    if (on) {
        if (!fullscreen_) {
            // Remember where the window was so turning fullscreen back off
            // restores it here, not at some arbitrary GLFW default spot.
            glfwGetWindowPos(window_, &windowedX_, &windowedY_);
            glfwGetWindowSize(window_, &windowedWidth_, &windowedHeight_);
        }
        GLFWmonitor* monitor = resolveMonitor(displayIndex);
        if (monitor) {
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);
            glfwSetWindowMonitor(window_, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        }
    } else {
        glfwSetWindowMonitor(window_, nullptr, windowedX_, windowedY_, windowedWidth_, windowedHeight_, 0);
    }
    fullscreen_ = on;
}

// displayIndex 0 = whatever GLFW considers primary right now; 1..N = a
// specific connected monitor. Falls back to the primary monitor if the index
// is out of range (e.g. a monitor was unplugged since it was persisted).
GLFWmonitor* GlfwPlatform::resolveMonitor(int displayIndex) const {
    if (displayIndex <= 0) {
        return glfwGetPrimaryMonitor();
    }
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    const int i = displayIndex - 1;
    if (i < 0 || i >= count) {
        return glfwGetPrimaryMonitor();
    }
    return monitors[i];
}

void GlfwPlatform::loadKeyMap() {
    keyMap_ = input::glfw::defaultKeyMap();
    const std::string path = exeDir_ + "/keys.cfg";
    std::ifstream file(path);
    if (!file) {
        return;  // optional; the defaults cover every action
    }
    for (const std::string& warning : keyMap_.loadOverrides(file, input::glfw::codeFromName, path)) {
        std::fprintf(stderr, "%s\n", warning.c_str());
    }
    std::fprintf(stdout, "Loaded key bindings from %s\n", path.c_str());
}

void GlfwPlatform::errorCallback(int error, const char* description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

void GlfwPlatform::framebufferSizeCallback(GLFWwindow* /*window*/, int width, int height) {
    glViewport(0, 0, width, height);
}

void GlfwPlatform::keyCallback(GLFWwindow* window, int key, int scancode, int action, int /*mods*/) {
    auto* self = static_cast<GlfwPlatform*>(glfwGetWindowUserPointer(window));
    if (!self) {
        return;
    }
    const std::optional<input::Phase> phase = input::glfw::phaseFromGlfwAction(action);
    if (!phase) {
        return;
    }
    if (self->capturing_) {
        // Swallowed either way: an unnameable key (no keys.cfg spelling for
        // it) just keeps capture waiting rather than acting as a normal key.
        if (*phase == input::Phase::Press) {
            const int code = input::glfw::inputCode(key, scancode);
            if (const std::optional<std::string> name = input::glfw::nameFromCode(code)) {
                self->capturedInput_ = CapturedInput{code, *name};
                self->capturing_ = false;
            }
        }
        return;
    }
    // One physical key may emit several actions (B = aspect ratio while
    // playing, blue on a teletext page); each screen reacts to at most one.
    const uint32_t group = self->nextGroup_++;
    for (const input::Action a : self->keyMap_.actionsFor(input::glfw::inputCode(key, scancode))) {
        self->pending_.push_back(input::InputEvent{a, *phase, 1.0f, group});
    }
}

bool GlfwPlatform::simulateCapturedInput(std::string_view name) {
    if (!capturing_) {
        return false;
    }
    const std::optional<int> code = input::glfw::codeFromName(name);
    if (!code) {
        return false;
    }
    const std::optional<std::string> canonicalName = input::glfw::nameFromCode(*code);
    if (!canonicalName) {
        return false;  // not nameable, same as a real press of it: capture keeps waiting
    }
    capturedInput_ = CapturedInput{*code, *canonicalName};
    capturing_ = false;
    return true;
}

std::vector<std::string> GlfwPlatform::bindingNames(input::Action action) const {
    std::vector<std::string> names;
    for (int code : keyMap_.codesFor(action)) {
        if (const std::optional<std::string> name = input::glfw::nameFromCode(code)) {
            names.push_back(*name);
        }
    }
    return names;
}

void GlfwPlatform::rebindAction(input::Action action, int code, const std::string& name) {
    keyMap_.unbindAction(action);
    keyMap_.bind(code, action);
    input::saveKeyBinding(exeDir_ + "/keys.cfg", action, {name});
}

void GlfwPlatform::resetActionBinding(input::Action action) {
    keyMap_.unbindAction(action);
    for (int code : input::glfw::defaultKeyMap().codesFor(action)) {
        keyMap_.bind(code, action);
    }
    input::clearKeyBindingOverride(exeDir_ + "/keys.cfg", action);
}
