#include "app.h"

#include <glad/gl.h>
// GLFW must be included after glad so it doesn't pull in its own GL headers.
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <vector>

#include "paths.h"
#include "settings.h"
#include "shader.h"
#include "ui/style.h"

namespace fs = std::filesystem;

namespace {

// Optional test hook: if PVM_TEST_AUTOCLOSE_FRAMES is set, the window closes
// itself after that many rendered frames. Used to verify a clean, automatic
// shutdown path (no crash/leak) without requiring a human to press a key or
// close the window, e.g. in headless/CI verification.
int autoCloseFrameCount() {
    if (const char* env = std::getenv("PVM_TEST_AUTOCLOSE_FRAMES")) {
        return std::atoi(env);
    }
    return 0;
}

// Optional test hook: if PVM_TEST_SCREENSHOT_PATH is set, the last rendered
// frame (right before auto-close) is dumped as a binary PPM for visual
// verification without a human watching the window live.
const char* screenshotPath() {
    return std::getenv("PVM_TEST_SCREENSHOT_PATH");
}

// Optional test hook: if PVM_TEST_EXERCISE_CONTROLS is set, drive
// pause/seek programmatically at fixed frames and log state to stdout, so
// play/pause/seek can be regression-tested without a physical key press.
// Only meaningful once already in the Playing screen (e.g. combined with
// PVM_TEST_SIMULATE_KEYS to first navigate there).
bool exerciseControlsRequested() {
    return std::getenv("PVM_TEST_EXERCISE_CONTROLS") != nullptr;
}

// Optional test hook: PVM_TEST_SIMULATE_KEYS is a comma-separated list of
// key names (UP, DOWN, LEFT, RIGHT, ENTER, ESCAPE, SPACE, BACKSPACE). One
// key is dispatched through the real App::handleKey() path every
// kSimulateIntervalFrames frames, so full keyboard-only navigation --
// menu -> file browser -> playback -> back to menu -- can be regression
// tested without a human at the keyboard.
constexpr int kSimulateIntervalFrames = 15;

std::vector<int> parseSimulatedKeys() {
    std::vector<int> keys;
    const char* env = std::getenv("PVM_TEST_SIMULATE_KEYS");
    if (!env) {
        return keys;
    }
    std::stringstream ss{std::string(env)};
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        if (tok == "UP") keys.push_back(GLFW_KEY_UP);
        else if (tok == "DOWN") keys.push_back(GLFW_KEY_DOWN);
        else if (tok == "LEFT") keys.push_back(GLFW_KEY_LEFT);
        else if (tok == "RIGHT") keys.push_back(GLFW_KEY_RIGHT);
        else if (tok == "ENTER") keys.push_back(GLFW_KEY_ENTER);
        else if (tok == "ESCAPE") keys.push_back(GLFW_KEY_ESCAPE);
        else if (tok == "SPACE") keys.push_back(GLFW_KEY_SPACE);
        else if (tok == "BACKSPACE") keys.push_back(GLFW_KEY_BACKSPACE);
        else if (tok == "C") keys.push_back(GLFW_KEY_C);
    }
    return keys;
}

void saveScreenshotPPM(const char* path, int width, int height) {
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 3);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    std::FILE* f = std::fopen(path, "wb");
    if (!f) {
        std::fprintf(stderr, "Failed to open screenshot path: %s\n", path);
        return;
    }
    std::fprintf(f, "P6\n%d %d\n255\n", width, height);
    // glReadPixels rows are bottom-up; PPM expects top-down.
    for (int y = height - 1; y >= 0; --y) {
        std::fwrite(pixels.data() + static_cast<size_t>(y) * width * 3, 1, width * 3, f);
    }
    std::fclose(f);
    std::fprintf(stdout, "Wrote screenshot: %s\n", path);
}

void glfwErrorCallback(int error, const char* description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

void framebufferSizeCallback(GLFWwindow* /*window*/, int width, int height) {
    glViewport(0, 0, width, height);
}

void checkGlError(const char* tag) {
    for (GLenum err = glGetError(); err != GL_NO_ERROR; err = glGetError()) {
        std::fprintf(stderr, "GL error after %s: 0x%x\n", tag, err);
    }
}

// Captures a handful of GL state bits mpv's render call may mutate, and
// restores them afterward. mpv's render API makes no guarantee about
// preserving caller GL state, and ImGui's backend renders right after this
// in the same frame -- see PLAN.md Phase 2 "Known Risks".
struct GLStateGuard {
    GLboolean blendEnabled = GL_FALSE;
    GLboolean depthEnabled = GL_FALSE;
    GLboolean scissorEnabled = GL_FALSE;
    GLint viewport[4] = {0, 0, 0, 0};
    GLint program = 0;
    GLint activeTexture = GL_TEXTURE0;
    GLint texBinding2D = 0;
    GLint vao = 0;
    GLint arrayBuffer = 0;
    GLint framebuffer = 0;

    void capture() {
        blendEnabled = glIsEnabled(GL_BLEND);
        depthEnabled = glIsEnabled(GL_DEPTH_TEST);
        scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texBinding2D);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    }

    void restore() const {
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glUseProgram(program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texBinding2D);
        glActiveTexture(static_cast<GLenum>(activeTexture));
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, arrayBuffer);
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        (blendEnabled ? glEnable : glDisable)(GL_BLEND);
        (depthEnabled ? glEnable : glDisable)(GL_DEPTH_TEST);
        (scissorEnabled ? glEnable : glDisable)(GL_SCISSOR_TEST);
    }
};

std::string formatTimestamp(double seconds) {
    if (seconds < 0.0) seconds = 0.0;
    int total = static_cast<int>(seconds);
    int mins = total / 60;
    int secs = total % 60;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", mins, secs);
    return buf;
}

std::string basename(const std::string& path) {
    auto pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

const std::vector<std::string> kVideoExtensions = {".mp4", ".mkv", ".avi", ".webm", ".mov", ".m4v"};
const std::vector<std::string> kAudioExtensions = {".mp3", ".flac", ".wav", ".ogg", ".m4a", ".aac"};

std::vector<std::string> mergedExtensions() {
    std::vector<std::string> merged = kVideoExtensions;
    merged.insert(merged.end(), kAudioExtensions.begin(), kAudioExtensions.end());
    return merged;
}

std::string toLowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool extensionIn(const std::string& path, const std::vector<std::string>& extensions) {
    const std::string ext = toLowerAscii(fs::path(path).extension().string());
    return std::find(extensions.begin(), extensions.end(), ext) != extensions.end();
}

// Font size: adjustable via the settings screen, applied by rebuilding the
// ImGui font atlas at the new pixel size (kept crisp, unlike a global
// render-time scale factor would be). No UI-imposed upper limit; the only
// ceiling is a generous safety net far past any usable size, purely to
// avoid an unbounded GPU texture allocation if a key is held down by
// accident.
constexpr int kFontSizeMin = 8;
constexpr int kFontSizeSafetyCeiling = 200;
constexpr int kFontSizeStep = 2;

// Menu screen position: also adjustable via the settings screen.
const std::vector<std::string> kMenuPositionNames = {"Top Left", "Top Right", "Center", "Bottom Left",
                                                       "Bottom Right"};

// Row-selection visual style: also adjustable via the settings screen; see
// App::drawMenuRow().
const std::vector<std::string> kSelectionStyleNames = {"Highlight", "Marker", "Underline"};

// Shortens a long path for display in a settings row (the stored value
// itself is never truncated) so a deeply nested directory doesn't blow up
// the auto-sized settings panel's width.
std::string truncatePathForDisplay(const std::string& path, size_t maxChars = 40) {
    if (path.size() <= maxChars) {
        return path;
    }
    return "..." + path.substr(path.size() - (maxChars - 3));
}

}  // namespace

App::App() = default;

App::~App() {
    shutdown();
}

bool App::init(int width, int height, const char* title) {
    glfwSetErrorCallback(glfwErrorCallback);

    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialize GLFW\n");
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window_) {
        std::fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);  // vsync

    if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress))) {
        std::fprintf(stderr, "Failed to initialize glad (load GL function pointers)\n");
        glfwDestroyWindow(window_);
        window_ = nullptr;
        glfwTerminate();
        return false;
    }

    glfwSetFramebufferSizeCallback(window_, framebufferSizeCallback);
    glfwSetWindowUserPointer(window_, this);
    glfwSetKeyCallback(window_, &App::keyCallback);

    std::fprintf(stdout, "OpenGL: %s / GLSL: %s / Renderer: %s\n",
                 glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION),
                 glGetString(GL_RENDERER));

    if (!mpv_.init(window_)) {
        std::fprintf(stderr, "Failed to initialize mpv player\n");
        return false;
    }

    const std::string dir = exeDir();

    fontPath_ = dir + "/assets/fonts/JetBrainsMono-Regular.ttf";
    fontsDir_ = dir + "/assets/fonts";
    scanAvailableFonts();

    // Load persisted settings before the first font bake below, so a
    // restart picks up right where the user left it. Pre-populate from
    // this App's own defaults so any field absent from the file (missing
    // file entirely on first run, or an older file written before a field
    // existed) keeps its default rather than becoming zero-initialized.
    configPath_ = dir + "/config.cfg";
    AppSettings loadedSettings;
    loadedSettings.fontSizePx = fontSizePx_;
    loadedSettings.fontFile = selectedFontFile_;
    loadedSettings.menuPositionIndex = menuPositionIndex_;
    loadedSettings.selectionStyleIndex = selectionStyleIndex_;
    loadedSettings.showHiddenFiles = showHiddenFiles_;
    loadedSettings.startDirectory = startDirectory_;
    loadedSettings.lastUsedDirectory = lastUsedDirectory_;
    loadedSettings.brightness = brightness_;
    loadedSettings.contrast = contrast_;
    loadedSettings.saturation = saturation_;
    loadedSettings.crtEnabled = crtEnabled_;
    loadedSettings.crtEffectStrength = crtEffectStrength_;
    loadedSettings.bloomStrength = bloomStrength_;
    loadedSettings.scanlineCount = scanlineCount_;
    loadedSettings.vignetteStrength = vignetteStrength_;
    loadedSettings.colorTear = colorTear_;
    loadSettings(configPath_, loadedSettings);

    fontSizePx_ = std::clamp(loadedSettings.fontSizePx, kFontSizeMin, kFontSizeSafetyCeiling);

    selectedFontChoiceIndex_ = 0;
    selectedFontFile_.clear();
    for (size_t i = 0; i < availableFontFiles_.size(); ++i) {
        if (availableFontFiles_[i] == loadedSettings.fontFile) {
            selectedFontChoiceIndex_ = static_cast<int>(i) + 1;  // +1: index 0 is "Default"
            selectedFontFile_ = availableFontFiles_[i];
            break;
        }
    }
    // A font_file naming a file that's gone missing (or empty/first run)
    // silently falls back to Default above -- not an error.

    menuPositionIndex_ = loadedSettings.menuPositionIndex % static_cast<int>(kMenuPositionNames.size());
    if (menuPositionIndex_ < 0) {
        menuPositionIndex_ += static_cast<int>(kMenuPositionNames.size());
    }
    const int selectionStyleCount = static_cast<int>(kSelectionStyleNames.size());
    selectionStyleIndex_ = ((loadedSettings.selectionStyleIndex % selectionStyleCount) + selectionStyleCount) %
                           selectionStyleCount;
    showHiddenFiles_ = loadedSettings.showHiddenFiles;
    startDirectory_ = loadedSettings.startDirectory;
    lastUsedDirectory_ = loadedSettings.lastUsedDirectory;
    brightness_ = std::clamp(loadedSettings.brightness, -0.5f, 0.5f);
    contrast_ = std::clamp(loadedSettings.contrast, 0.0f, 2.0f);
    saturation_ = std::clamp(loadedSettings.saturation, 0.0f, 2.0f);
    crtEnabled_ = loadedSettings.crtEnabled;
    crtEffectStrength_ = std::clamp(loadedSettings.crtEffectStrength, 0.0f, 2.0f);
    bloomStrength_ = std::clamp(loadedSettings.bloomStrength, 0.0f, 2.0f);
    scanlineCount_ = std::clamp(loadedSettings.scanlineCount, 60, 1080);
    vignetteStrength_ = std::clamp(loadedSettings.vignetteStrength, 0.0f, 1.0f);
    colorTear_ = std::clamp(loadedSettings.colorTear, 0.0f, 10.0f);

    blitProgram_ = loadShaderProgram(dir + "/shaders/passthrough.vert", dir + "/shaders/blit.frag");
    if (!blitProgram_) {
        std::fprintf(stderr, "Failed to load blit shader\n");
        return false;
    }

    // Fullscreen triangle needs no vertex attributes, but core profile still
    // requires a bound VAO to issue a draw call.
    glGenVertexArrays(1, &blitVao_);

    crtProgram_ = loadShaderProgram(dir + "/shaders/passthrough.vert", dir + "/shaders/crt.frag");
    if (!crtProgram_) {
        std::fprintf(stderr, "Failed to load CRT shader\n");
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // no imgui.ini for this prototype

    loadSelectedFontIntoAtlas();

    ui::applyPvmStyle();

    ImGui_ImplGlfw_InitForOpenGL(window_, /*install_callbacks=*/true);
    ImGui_ImplOpenGL3_Init("#version 330 core");
    imguiInitialized_ = true;

    rootMenu_.setItems({{"PLAY MEDIA"}, {"SETTINGS"}, {"EXIT"}});

    return true;
}

void App::setMediaRoots(std::vector<std::string> paths) {
    if (!paths.empty()) {
        mediaRoots_ = std::move(paths);
    }
}

void App::scanAvailableFonts() {
    availableFontFiles_.clear();
    const std::string defaultBasename = fs::path(fontPath_).filename().string();

    std::error_code ec;
    fs::directory_iterator it(fontsDir_, ec);
    if (!ec) {
        for (const auto& de : it) {
            std::error_code typeEc;
            if (!de.is_regular_file(typeEc) || typeEc) {
                continue;
            }
            const std::string name = de.path().filename().string();
            if (name == defaultBasename) {
                continue;  // the bundled default is offered as "Default", not listed again by filename
            }
            const std::string ext = toLowerAscii(de.path().extension().string());
            if (ext == ".ttf" || ext == ".otf") {
                availableFontFiles_.push_back(name);
            }
        }
    }
    std::sort(availableFontFiles_.begin(), availableFontFiles_.end(),
             [](const std::string& a, const std::string& b) { return toLowerAscii(a) < toLowerAscii(b); });

    fontChoiceNames_.clear();
    fontChoiceNames_.push_back("Default");
    fontChoiceNames_.insert(fontChoiceNames_.end(), availableFontFiles_.begin(), availableFontFiles_.end());
}

std::string App::resolveFontPath() const {
    return selectedFontFile_.empty() ? fontPath_ : (fontsDir_ + "/" + selectedFontFile_);
}

void App::loadSelectedFontIntoAtlas() {
    ImGuiIO& io = ImGui::GetIO();
    const std::string path = resolveFontPath();
    if (io.Fonts->AddFontFromFileTTF(path.c_str(), static_cast<float>(fontSizePx_))) {
        return;
    }
    std::fprintf(stderr, "Failed to load font %s, falling back to bundled default\n", path.c_str());
    if (path != fontPath_ &&
        io.Fonts->AddFontFromFileTTF(fontPath_.c_str(), static_cast<float>(fontSizePx_))) {
        selectedFontChoiceIndex_ = 0;
        selectedFontFile_.clear();
        return;
    }
    io.Fonts->AddFontDefault();
}

void App::applyFont() {
    ImGui::GetIO().Fonts->Clear();
    loadSelectedFontIntoAtlas();
    // Rebuild the backend's GL device objects (including the font atlas
    // texture) from the new bake. Safe here: always called from
    // handleKey(), i.e. during glfwPollEvents() at the top of the frame,
    // before this frame's ImGui::NewFrame()/Render().
    ImGui_ImplOpenGL3_DestroyDeviceObjects();
    ImGui_ImplOpenGL3_CreateDeviceObjects();
}

void App::saveCurrentSettings() const {
    AppSettings settings;
    settings.fontSizePx = fontSizePx_;
    settings.fontFile = selectedFontFile_;
    settings.menuPositionIndex = menuPositionIndex_;
    settings.selectionStyleIndex = selectionStyleIndex_;
    settings.showHiddenFiles = showHiddenFiles_;
    settings.startDirectory = startDirectory_;
    settings.lastUsedDirectory = lastUsedDirectory_;
    settings.brightness = brightness_;
    settings.contrast = contrast_;
    settings.saturation = saturation_;
    settings.crtEnabled = crtEnabled_;
    settings.crtEffectStrength = crtEffectStrength_;
    settings.bloomStrength = bloomStrength_;
    settings.scanlineCount = scanlineCount_;
    settings.vignetteStrength = vignetteStrength_;
    settings.colorTear = colorTear_;
    saveSettings(configPath_, settings);
}

bool App::loadMedia(const std::string& path) {
    return mpv_.loadFile(path);
}

void App::run() {
    if (!window_) {
        return;
    }

    const int autoCloseFrames = autoCloseFrameCount();
    const bool exerciseControls = exerciseControlsRequested();
    const std::vector<int> simulatedKeys = parseSimulatedKeys();
    int frame = 0;

    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();
        mpv_.pollEvents();

        if (screen_ == Screen::Playing && mpv_.consumeEndOfFile()) {
            onPlaybackStopped();
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        int fbWidth, fbHeight;
        glfwGetFramebufferSize(window_, &fbWidth, &fbHeight);
        ensureSceneFbo(fbWidth, fbHeight);

        // Video + ImGui composite into one offscreen scene FBO; the CRT
        // pass (or a plain passthrough when disabled) then draws that
        // scene to the real backbuffer as a final fullscreen-quad pass.
        glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo_);
        renderFrame();
        checkGlError("renderFrame");
        renderHud();
        checkGlError("renderHud");

        renderPostProcess(fbWidth, fbHeight);
        checkGlError("renderPostProcess");

        glfwSwapBuffers(window_);

        if (exerciseControls && screen_ == Screen::Playing) {
            if (frame == 20) {
                std::fprintf(stdout, "[test] t=%.2f paused=%d -> togglePause()\n",
                             mpv_.timePositionSeconds(), mpv_.isPaused());
                mpv_.togglePause();
            } else if (frame == 40) {
                std::fprintf(stdout, "[test] t=%.2f paused=%d -> togglePause()\n",
                             mpv_.timePositionSeconds(), mpv_.isPaused());
                mpv_.togglePause();
            } else if (frame == 60) {
                std::fprintf(stdout, "[test] t=%.2f paused=%d -> seekRelative(+5)\n",
                             mpv_.timePositionSeconds(), mpv_.isPaused());
                mpv_.seekRelative(5.0);
            } else if (frame == 80) {
                std::fprintf(stdout, "[test] t=%.2f paused=%d (final)\n",
                             mpv_.timePositionSeconds(), mpv_.isPaused());
            }
        }

        if (!simulatedKeys.empty() && frame > 0 && frame % kSimulateIntervalFrames == 0) {
            size_t idx = static_cast<size_t>(frame / kSimulateIntervalFrames) - 1;
            if (idx < simulatedKeys.size()) {
                std::fprintf(stdout, "[test] simulate key #%zu = %d (screen=%d)\n", idx,
                             simulatedKeys[idx], static_cast<int>(screen_));
                handleKey(simulatedKeys[idx], GLFW_PRESS);
            }
        }

        ++frame;
        if (autoCloseFrames > 0 && frame >= autoCloseFrames) {
            if (const char* path = screenshotPath()) {
                int width, height;
                glfwGetFramebufferSize(window_, &width, &height);
                saveScreenshotPPM(path, width, height);
            }
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
        }
    }
}

void App::ensureSceneFbo(int width, int height) {
    if (width == sceneWidth_ && height == sceneHeight_ && sceneFbo_ != 0) {
        return;
    }
    destroySceneFbo();

    glGenTextures(1, &sceneTexture_);
    glBindTexture(GL_TEXTURE_2D, sceneTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &sceneFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneTexture_, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "Scene FBO incomplete: 0x%x\n", status);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    sceneWidth_ = width;
    sceneHeight_ = height;
}

void App::destroySceneFbo() {
    if (sceneFbo_) {
        glDeleteFramebuffers(1, &sceneFbo_);
        sceneFbo_ = 0;
    }
    if (sceneTexture_) {
        glDeleteTextures(1, &sceneTexture_);
        sceneTexture_ = 0;
    }
    sceneWidth_ = 0;
    sceneHeight_ = 0;
}

void App::renderPostProcess(int width, int height) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);

    // Always run the full shader: screen appearance (brightness/contrast/
    // saturation) applies unconditionally, while uCrtEnabled/
    // uCrtEffectStrength gate the CRT/bloom stylization -- see crt.frag.
    glUseProgram(crtProgram_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneTexture_);
    glUniform1i(glGetUniformLocation(crtProgram_, "uTexture"), 0);
    glUniform2f(glGetUniformLocation(crtProgram_, "uResolution"), static_cast<float>(width),
                static_cast<float>(height));
    glUniform1f(glGetUniformLocation(crtProgram_, "uBrightness"), brightness_);
    glUniform1f(glGetUniformLocation(crtProgram_, "uContrast"), contrast_);
    glUniform1f(glGetUniformLocation(crtProgram_, "uSaturation"), saturation_);
    glUniform1f(glGetUniformLocation(crtProgram_, "uCrtEnabled"), crtEnabled_ ? 1.0f : 0.0f);
    glUniform1f(glGetUniformLocation(crtProgram_, "uCrtEffectStrength"), crtEffectStrength_);
    glUniform1f(glGetUniformLocation(crtProgram_, "uBloomStrength"), bloomStrength_);
    glUniform1f(glGetUniformLocation(crtProgram_, "uScanlineCount"), static_cast<float>(scanlineCount_));
    glUniform1f(glGetUniformLocation(crtProgram_, "uVignetteStrength"), vignetteStrength_);
    glUniform1f(glGetUniformLocation(crtProgram_, "uColorTear"), colorTear_);

    glBindVertexArray(blitVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);
}

void App::renderFrame() {
    int width, height;
    glfwGetFramebufferSize(window_, &width, &height);
    glViewport(0, 0, width, height);

    // PVM idle screen: flat black. Also the backdrop for the menu screens.
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (screen_ != Screen::Playing) {
        return;
    }

    GLStateGuard preMpvState;
    preMpvState.capture();
    unsigned int videoTexture = mpv_.render(width, height);
    preMpvState.restore();

    if (videoTexture != 0) {
        // Belt-and-suspenders: explicitly (re-)establish the state our own
        // blit draw needs rather than assuming the restored baseline above
        // already matches it.
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_SCISSOR_TEST);
        glViewport(0, 0, width, height);

        glUseProgram(blitProgram_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, videoTexture);
        glUniform1i(glGetUniformLocation(blitProgram_, "uTexture"), 0);

        glBindVertexArray(blitVao_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        glUseProgram(0);
    }
}

void App::renderHud() {
    switch (screen_) {
        case Screen::Playing:
            renderPlaybackHud();
            break;
        case Screen::Settings:
            renderSettings();
            break;
        case Screen::RootMenu:
        case Screen::FileBrowser:
        case Screen::PickStartDirectory:
            renderMenu();
            break;
    }
}

void App::renderPlaybackHud() {
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                    ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;

    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f));
    ImGui::SetNextWindowBgAlpha(0.35f);
    ImGui::Begin("HUD", nullptr, flags);

    if (!mpv_.filename().empty()) {
        ImGui::Text("%s", basename(mpv_.filename()).c_str());
    } else {
        ImGui::Text("(no media loaded)");
    }

    ImGui::Text("%s / %s", formatTimestamp(mpv_.timePositionSeconds()).c_str(),
                formatTimestamp(mpv_.durationSeconds()).c_str());
    ImGui::Text("%s", mpv_.isPaused() ? "PAUSED" : "PLAYING");

    ImGui::End();

    if (currentMediaKind_ == MediaKind::Audio) {
        renderAudioIndicator();
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// No audio visualization (explicitly out of scope, see PLAN.md) -- just a
// static, type-aware indicator so an audio-only file doesn't just look like
// a stuck/blank video screen.
void App::renderAudioIndicator() {
    // ImGui positioning is in its own logical coordinate space (ImGuiIO::
    // DisplaySize), which is the GLFW *window* size, not the raw GL
    // framebuffer pixel size -- those differ by the display scale factor
    // on HiDPI/Retina screens. Mixing the two silently mispositions
    // anything but a (0,0) pivot.
    const ImVec2 display = ImGui::GetIO().DisplaySize;

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                    ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground;

    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::Begin("AudioIndicator", nullptr, flags);
    ImGui::SetWindowFontScale(2.0f);
    ImGui::TextUnformatted("[ AUDIO ]");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::End();
}

// Row-selection visual style, adjustable via the settings screen
// (selectionStyleIndex_ / kSelectionStyleNames):
//   0 Highlight  -- reverse-video: white row background, black text (the
//                   original/default look; uses ImGui::Selectable's own
//                   highlight, so it keeps mouse click-to-select too).
//   1 Marker     -- a small filled white square before the row; row text
//                   stays normal white-on-black.
//   2 Underline  -- a white line drawn under the row's text; row text
//                   stays normal white-on-black.
// Styles 1/2 draw plain text rather than a Selectable, since the point is
// to *not* show Selectable's own highlight -- this project is
// keyboard-only by design (see PLAN.md), so losing incidental mouse
// click-to-select on those two styles is an acceptable tradeoff.
void App::drawMenuRow(const std::string& label, bool selected) {
    switch (selectionStyleIndex_) {
        case 1: {  // Marker
            const float lineHeight = ImGui::GetTextLineHeight();
            const float indent = lineHeight * 1.4f;
            const ImVec2 rowStart = ImGui::GetCursorScreenPos();
            if (selected) {
                const float boxSize = lineHeight * 0.5f;
                const float boxPad = (lineHeight - boxSize) * 0.5f;
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImVec2(rowStart.x, rowStart.y + boxPad),
                    ImVec2(rowStart.x + boxSize, rowStart.y + boxPad + boxSize), IM_COL32(255, 255, 255, 255));
            }
            ImGui::Indent(indent);
            ImGui::TextUnformatted(label.c_str());
            ImGui::Unindent(indent);
            break;
        }
        case 2: {  // Underline
            const ImVec2 textStart = ImGui::GetCursorScreenPos();
            ImGui::TextUnformatted(label.c_str());
            if (selected) {
                const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
                const float y = textStart.y + textSize.y - 1.0f;
                ImGui::GetWindowDrawList()->AddLine(ImVec2(textStart.x, y), ImVec2(textStart.x + textSize.x, y),
                                                    IM_COL32(255, 255, 255, 255), 2.0f);
            }
            break;
        }
        case 0:  // Highlight
        default:
            if (selected) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
            }
            ImGui::Selectable(label.c_str(), selected);
            if (selected) {
                ImGui::PopStyleColor();
            }
            break;
    }
}

// Positions the next ImGui window per the configurable menuPositionIndex_
// setting (a screen anchor + pivot), instead of a fixed top-left/fullscreen
// placement. Shared by all menu-family screens (root menu, file browser,
// settings) so the "menu screen position" setting affects them uniformly.
void App::positionMenuWindow() const {
    // ImGui positioning is in its own logical coordinate space (ImGuiIO::
    // DisplaySize -- the GLFW *window* size), not the raw GL framebuffer
    // pixel size; those differ by the display scale factor on HiDPI/Retina
    // screens. Mixing the two silently mispositions anything but a (0,0)
    // pivot (verified: "Center" landed in the bottom-right corner on a 2x
    // display before this fix).
    const ImVec2 display = ImGui::GetIO().DisplaySize;

    constexpr float kMargin = 24.0f;
    ImVec2 pos;
    ImVec2 pivot;
    switch (menuPositionIndex_) {
        case 0:  // Top Left
            pos = ImVec2(kMargin, kMargin);
            pivot = ImVec2(0.0f, 0.0f);
            break;
        case 1:  // Top Right
            pos = ImVec2(display.x - kMargin, kMargin);
            pivot = ImVec2(1.0f, 0.0f);
            break;
        case 2:  // Center
            pos = ImVec2(display.x * 0.5f, display.y * 0.5f);
            pivot = ImVec2(0.5f, 0.5f);
            break;
        case 3:  // Bottom Left
            pos = ImVec2(kMargin, display.y - kMargin);
            pivot = ImVec2(0.0f, 1.0f);
            break;
        case 4:  // Bottom Right
        default:
            pos = ImVec2(display.x - kMargin, display.y - kMargin);
            pivot = ImVec2(1.0f, 1.0f);
            break;
    }
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, pivot);
    ImGui::SetNextWindowSizeConstraints(ImVec2(360.0f, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
}

void App::renderMenu() {
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus;

    positionMenuWindow();
    ImGui::Begin("PVM Menu", nullptr, flags);

    ImGui::TextUnformatted("MENU");
    ImGui::Separator();
    ImGui::Spacing();

    if (screen_ == Screen::RootMenu) {
        drawScrollableRows(static_cast<int>(rootMenu_.items().size()), rootMenu_.selectedIndex(),
                            [&](int i) { return rootMenu_.items()[i].label; });
    } else {  // Screen::FileBrowser or Screen::PickStartDirectory
        const bool picking = screen_ == Screen::PickStartDirectory;
        ImGui::Text("%s  (%s)", picking ? "SELECT FOLDER" : "SELECT FILE",
                    fileBrowser_.currentPathLabel().c_str());
        ImGui::Spacing();
        if (fileBrowser_.empty()) {
            ImGui::TextDisabled("(no matching files found)");
        } else {
            const auto& entries = fileBrowser_.entries();
            drawScrollableRows(static_cast<int>(entries.size()), fileBrowser_.selectedIndex(), [&](int i) {
                return entries[i].isDirectory ? entries[i].name + "/" : entries[i].name;
            });
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("%s", screen_ == Screen::RootMenu ? "UP/DOWN: Move   ENTER: Select   ESC: Exit"
                                                            : "UP/DOWN: Move   ENTER: Open   ESC: Back");

    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// Long listings (a big directory, a home folder with dozens of dotfiles, or
// the settings screen's dozen-odd rows) get a fixed-height scrolling
// region instead of growing the window past the screen; the selected row
// is kept in view as it moves. Shared by renderMenu() and renderSettings().
void App::drawScrollableRows(int count, int selectedIndex, const std::function<std::string(int)>& labelFor) {
    const float itemHeight = ImGui::GetTextLineHeightWithSpacing();
    const float maxListHeight = ImGui::GetIO().DisplaySize.y * 0.5f;
    const float listHeight = std::min(static_cast<float>(count) * itemHeight, maxListHeight);
    ImGui::BeginChild("MenuList", ImVec2(0.0f, listHeight));
    for (int i = 0; i < count; ++i) {
        const bool selected = (i == selectedIndex);
        drawMenuRow(labelFor(i), selected);
        if (selected) {
            ImGui::SetScrollHereY(0.5f);
        }
    }
    ImGui::EndChild();
}

void App::renderSettings() {
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus;

    positionMenuWindow();
    ImGui::Begin("PVM Settings", nullptr, flags);

    ImGui::TextUnformatted("SETTINGS");
    ImGui::Separator();
    ImGui::Spacing();

    const std::vector<SettingsRowDesc> rows = buildSettingsRows();
    drawScrollableRows(static_cast<int>(rows.size()), settingsSelectedRow_,
                        [&](int i) { return formatSettingsRow(rows[static_cast<size_t>(i)]); });

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("UP/DOWN: Move   LEFT/RIGHT: Change   ESC: Back");

    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

std::vector<App::SettingsRowDesc> App::buildSettingsRows() {
    auto intRow = [](std::string label, int* ptr, int mn, int mx, int step, std::string suffix = "") {
        SettingsRowDesc r;
        r.type = SettingsRowType::Int;
        r.label = std::move(label);
        r.intPtr = ptr;
        r.intMin = mn;
        r.intMax = mx;
        r.intStep = step;
        r.intSuffix = std::move(suffix);
        return r;
    };
    auto floatRow = [](std::string label, float* ptr, float mn, float mx, float step) {
        SettingsRowDesc r;
        r.type = SettingsRowType::Float;
        r.label = std::move(label);
        r.floatPtr = ptr;
        r.floatMin = mn;
        r.floatMax = mx;
        r.floatStep = step;
        return r;
    };
    auto boolRow = [](std::string label, bool* ptr, std::string onLabel, std::string offLabel) {
        SettingsRowDesc r;
        r.type = SettingsRowType::Bool;
        r.label = std::move(label);
        r.boolPtr = ptr;
        r.onLabel = std::move(onLabel);
        r.offLabel = std::move(offLabel);
        return r;
    };

    std::vector<SettingsRowDesc> rows;

    SettingsRowDesc fontSizeRow =
        intRow("Font Size", &fontSizePx_, kFontSizeMin, kFontSizeSafetyCeiling, kFontSizeStep, " px");
    fontSizeRow.onIntChanged = [this](int v) {
        fontSizePx_ = std::clamp(v, kFontSizeMin, kFontSizeSafetyCeiling);
        applyFont();
    };
    rows.push_back(fontSizeRow);

    SettingsRowDesc fontRow;
    fontRow.type = SettingsRowType::Enum;
    fontRow.label = "Font";
    fontRow.enumPtr = &selectedFontChoiceIndex_;
    fontRow.enumNames = &fontChoiceNames_;
    fontRow.onEnumChanged = [this]() {
        selectedFontFile_ = selectedFontChoiceIndex_ == 0
                                ? ""
                                : availableFontFiles_[static_cast<size_t>(selectedFontChoiceIndex_ - 1)];
        applyFont();
    };
    rows.push_back(fontRow);

    SettingsRowDesc positionRow;
    positionRow.type = SettingsRowType::Enum;
    positionRow.label = "Menu Position";
    positionRow.enumPtr = &menuPositionIndex_;
    positionRow.enumNames = &kMenuPositionNames;
    rows.push_back(positionRow);

    SettingsRowDesc selectionStyleRow;
    selectionStyleRow.type = SettingsRowType::Enum;
    selectionStyleRow.label = "Selection Style";
    selectionStyleRow.enumPtr = &selectionStyleIndex_;
    selectionStyleRow.enumNames = &kSelectionStyleNames;
    rows.push_back(selectionStyleRow);

    rows.push_back(boolRow("Hidden Files", &showHiddenFiles_, "Show", "Hide"));

    SettingsRowDesc startDirRow;
    startDirRow.type = SettingsRowType::Action;
    startDirRow.label = "Start Directory";
    startDirRow.actionValue =
        truncatePathForDisplay(!startDirectory_.empty() ? startDirectory_
                                : !mediaRoots_.empty()   ? mediaRoots_[0]
                                                          : std::string("."));
    startDirRow.onActivate = [this]() {
        const std::string startAt =
            !startDirectory_.empty() ? startDirectory_ : (!mediaRoots_.empty() ? mediaRoots_[0] : ".");
        fileBrowser_.openPicker(startAt, showHiddenFiles_);
        screen_ = Screen::PickStartDirectory;
    };
    rows.push_back(startDirRow);

    rows.push_back(floatRow("Brightness", &brightness_, -0.5f, 0.5f, 0.05f));
    rows.push_back(floatRow("Contrast", &contrast_, 0.0f, 2.0f, 0.05f));
    rows.push_back(floatRow("Saturation", &saturation_, 0.0f, 2.0f, 0.05f));

    rows.push_back(boolRow("CRT Effect", &crtEnabled_, "ON", "OFF"));
    rows.push_back(floatRow("Effect Strength", &crtEffectStrength_, 0.0f, 2.0f, 0.1f));
    rows.push_back(floatRow("Bloom Strength", &bloomStrength_, 0.0f, 2.0f, 0.1f));
    rows.push_back(intRow("Scanline Count", &scanlineCount_, 60, 1080, 20));
    rows.push_back(floatRow("Vignette Strength", &vignetteStrength_, 0.0f, 1.0f, 0.05f));
    rows.push_back(floatRow("Color Tear", &colorTear_, 0.0f, 10.0f, 0.5f));

    return rows;
}

std::string App::formatSettingsRow(const SettingsRowDesc& row) {
    char buf[80];
    const std::string labelColon = row.label + ":";
    switch (row.type) {
        case SettingsRowType::Int:
            std::snprintf(buf, sizeof(buf), "%-18s%d%s", labelColon.c_str(), *row.intPtr,
                          row.intSuffix.c_str());
            break;
        case SettingsRowType::Float:
            std::snprintf(buf, sizeof(buf), "%-18s%.2f", labelColon.c_str(), *row.floatPtr);
            break;
        case SettingsRowType::Bool:
            std::snprintf(buf, sizeof(buf), "%-18s%s", labelColon.c_str(),
                          *row.boolPtr ? row.onLabel.c_str() : row.offLabel.c_str());
            break;
        case SettingsRowType::Enum:
            std::snprintf(buf, sizeof(buf), "%-18s%s", labelColon.c_str(),
                          (*row.enumNames)[static_cast<size_t>(*row.enumPtr)].c_str());
            break;
        case SettingsRowType::Action:
            std::snprintf(buf, sizeof(buf), "%-18s%s", labelColon.c_str(), row.actionValue.c_str());
            break;
    }
    return buf;
}

void App::adjustSettingsRow(SettingsRowDesc& row, int direction) {
    switch (row.type) {
        case SettingsRowType::Int: {
            const int v = std::clamp(*row.intPtr + direction * row.intStep, row.intMin, row.intMax);
            if (row.onIntChanged) {
                row.onIntChanged(v);
            } else {
                *row.intPtr = v;
            }
            break;
        }
        case SettingsRowType::Float:
            *row.floatPtr = std::clamp(*row.floatPtr + static_cast<float>(direction) * row.floatStep,
                                       row.floatMin, row.floatMax);
            break;
        case SettingsRowType::Bool:
            *row.boolPtr = !*row.boolPtr;
            break;
        case SettingsRowType::Enum: {
            const int n = static_cast<int>(row.enumNames->size());
            *row.enumPtr = ((*row.enumPtr + direction) % n + n) % n;
            if (row.onEnumChanged) {
                row.onEnumChanged();
            }
            break;
        }
        case SettingsRowType::Action:
            break;  // not adjustable via LEFT/RIGHT; ENTER invokes onActivate (see handleKey)
    }
}

void App::activateRootMenuItem(int index) {
    switch (index) {
        case 0:  // Play Media
            enterFileBrowser(mergedExtensions());
            break;
        case 1:  // Settings
            settingsSelectedRow_ = 0;
            screen_ = Screen::Settings;
            break;
        case 2:  // Exit
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
            break;
        default:
            break;
    }
}

void App::enterFileBrowser(std::vector<std::string> extensions) {
    // The configurable start/last-used directory only makes sense for the
    // common single-root case; an explicit multi-root launch (argv) is a
    // deliberate dev/CLI configuration and keeps showing its roots picker
    // unchanged. lastUsedDirectory_ (where playback last stopped) takes
    // priority over startDirectory_ (the configured default) so "Play
    // Media" resumes where the user left off.
    std::vector<std::string> roots = mediaRoots_;
    if (mediaRoots_.size() <= 1) {
        if (!lastUsedDirectory_.empty()) {
            roots = {lastUsedDirectory_};
        } else if (!startDirectory_.empty()) {
            roots = {startDirectory_};
        }
    }
    fileBrowser_.open(roots, std::move(extensions), showHiddenFiles_);
    screen_ = Screen::FileBrowser;
}

void App::handleKey(int key, int action) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) {
        return;
    }

    // Global debug toggle, available on every screen: flips the CRT
    // post-process pass on/off without a restart.
    if (key == GLFW_KEY_C && action == GLFW_PRESS) {
        crtEnabled_ = !crtEnabled_;
        std::fprintf(stdout, "CRT effect: %s\n", crtEnabled_ ? "on" : "off");
        saveCurrentSettings();
        return;
    }

    switch (screen_) {
        case Screen::RootMenu:
            switch (key) {
                case GLFW_KEY_UP:
                    rootMenu_.moveUp();
                    break;
                case GLFW_KEY_DOWN:
                    rootMenu_.moveDown();
                    break;
                case GLFW_KEY_ENTER:
                case GLFW_KEY_KP_ENTER:
                    if (action == GLFW_PRESS) {
                        activateRootMenuItem(rootMenu_.selectedIndex());
                    }
                    break;
                case GLFW_KEY_ESCAPE:
                    if (action == GLFW_PRESS) {
                        glfwSetWindowShouldClose(window_, GLFW_TRUE);
                    }
                    break;
                default:
                    break;
            }
            break;

        case Screen::FileBrowser:
            switch (key) {
                case GLFW_KEY_UP:
                    fileBrowser_.moveUp();
                    break;
                case GLFW_KEY_DOWN:
                    fileBrowser_.moveDown();
                    break;
                case GLFW_KEY_ENTER:
                case GLFW_KEY_KP_ENTER:
                    if (action == GLFW_PRESS && !fileBrowser_.empty()) {
                        if (fileBrowser_.selectedIsDirectory()) {
                            fileBrowser_.enterSelectedDirectory();
                        } else {
                            const std::string path = fileBrowser_.selectedFilePath();
                            if (!path.empty() && loadMedia(path)) {
                                currentMediaKind_ = extensionIn(path, kVideoExtensions)   ? MediaKind::Video
                                                     : extensionIn(path, kAudioExtensions) ? MediaKind::Audio
                                                                                            : MediaKind::Unknown;
                                screen_ = Screen::Playing;
                            }
                        }
                    }
                    break;
                case GLFW_KEY_ESCAPE:
                case GLFW_KEY_BACKSPACE:
                    if (action == GLFW_PRESS && !fileBrowser_.goBack()) {
                        screen_ = Screen::RootMenu;
                    }
                    break;
                default:
                    break;
            }
            break;

        case Screen::Settings: {
            std::vector<SettingsRowDesc> rows = buildSettingsRows();
            const int rowCount = static_cast<int>(rows.size());
            switch (key) {
                case GLFW_KEY_UP:
                    settingsSelectedRow_ = std::max(0, settingsSelectedRow_ - 1);
                    break;
                case GLFW_KEY_DOWN:
                    settingsSelectedRow_ = std::min(rowCount - 1, settingsSelectedRow_ + 1);
                    break;
                case GLFW_KEY_LEFT:
                    if (settingsSelectedRow_ >= 0 && settingsSelectedRow_ < rowCount) {
                        adjustSettingsRow(rows[static_cast<size_t>(settingsSelectedRow_)], -1);
                        saveCurrentSettings();
                    }
                    break;
                case GLFW_KEY_RIGHT:
                    if (settingsSelectedRow_ >= 0 && settingsSelectedRow_ < rowCount) {
                        adjustSettingsRow(rows[static_cast<size_t>(settingsSelectedRow_)], 1);
                        saveCurrentSettings();
                    }
                    break;
                case GLFW_KEY_ENTER:
                case GLFW_KEY_KP_ENTER:
                    if (action == GLFW_PRESS && settingsSelectedRow_ >= 0 && settingsSelectedRow_ < rowCount) {
                        const SettingsRowDesc& row = rows[static_cast<size_t>(settingsSelectedRow_)];
                        if (row.type == SettingsRowType::Action && row.onActivate) {
                            row.onActivate();
                        }
                    }
                    break;
                case GLFW_KEY_ESCAPE:
                case GLFW_KEY_BACKSPACE:
                    if (action == GLFW_PRESS) {
                        screen_ = Screen::RootMenu;
                    }
                    break;
                default:
                    break;
            }
            break;
        }

        case Screen::PickStartDirectory:
            switch (key) {
                case GLFW_KEY_UP:
                    fileBrowser_.moveUp();
                    break;
                case GLFW_KEY_DOWN:
                    fileBrowser_.moveDown();
                    break;
                case GLFW_KEY_ENTER:
                case GLFW_KEY_KP_ENTER:
                    if (action == GLFW_PRESS && !fileBrowser_.empty()) {
                        if (fileBrowser_.selectedIsPickHere()) {
                            startDirectory_ = fileBrowser_.currentPathLabel();
                            lastUsedDirectory_.clear();  // an explicit new start dir takes priority
                            saveCurrentSettings();
                            screen_ = Screen::Settings;
                        } else if (fileBrowser_.selectedIsDirectory()) {
                            fileBrowser_.enterSelectedDirectory();
                        }
                    }
                    break;
                case GLFW_KEY_ESCAPE:
                case GLFW_KEY_BACKSPACE:
                    if (action == GLFW_PRESS && !fileBrowser_.goBack()) {
                        screen_ = Screen::Settings;
                    }
                    break;
                default:
                    break;
            }
            break;

        case Screen::Playing:
            switch (key) {
                case GLFW_KEY_SPACE:
                    if (action == GLFW_PRESS) {
                        mpv_.togglePause();
                    }
                    break;
                case GLFW_KEY_LEFT:
                    mpv_.seekRelative(-5.0);
                    break;
                case GLFW_KEY_RIGHT:
                    mpv_.seekRelative(5.0);
                    break;
                case GLFW_KEY_ESCAPE:
                    if (action == GLFW_PRESS) {
                        mpv_.stop();
                        onPlaybackStopped();
                    }
                    break;
                default:
                    break;
            }
            break;
    }
}

void App::onPlaybackStopped() {
    currentMediaKind_ = MediaKind::Unknown;
    screen_ = Screen::RootMenu;

    // Remember where we were browsing so "Play Media" resumes here next
    // time, instead of always restarting at the configured start
    // directory. currentPathLabel() is only the "Select Folder" root-
    // picker placeholder when pathStack_ is empty, which can't be the
    // case here -- reaching Playing requires having descended into (and
    // selected a file from) a real directory first.
    const std::string dir = fileBrowser_.currentPathLabel();
    if (!dir.empty() && dir != "Select Folder") {
        lastUsedDirectory_ = dir;
        saveCurrentSettings();
    }
}

void App::keyCallback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(window));
    if (app) {
        app->handleKey(key, action);
    }
}

void App::shutdown() {
    if (imguiInitialized_) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        imguiInitialized_ = false;
    }
    destroySceneFbo();
    if (crtProgram_) {
        glDeleteProgram(crtProgram_);
        crtProgram_ = 0;
    }
    if (blitVao_) {
        glDeleteVertexArrays(1, &blitVao_);
        blitVao_ = 0;
    }
    if (blitProgram_) {
        glDeleteProgram(blitProgram_);
        blitProgram_ = 0;
    }
    mpv_.shutdown();
    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
        glfwTerminate();
    }
}
