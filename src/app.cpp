#include "app.h"

#include "gl.h"

#include <imgui.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ctime>
#include <sstream>
#include <vector>

#include "platform/platform.h"
#include "settings.h"
#include "shader.h"
#include "teletext/mvw_config.h"
#include "teletext/mvw_service.h"
#include "teletext/news_config.h"
#include "teletext/news_service.h"
#include "teletext/teletext_view.h"
#include "ui/style.h"

namespace fs = std::filesystem;

namespace {

// Optional test hook: if PVM_TEST_AUTOCLOSE_FRAMES is set, the window closes
// itself after that many rendered frames. Used to verify a clean, automatic
// shutdown path (no crash/leak) without requiring a human to press a key or
// close the window, e.g. in headless/CI verification.
// Optional test hook: if PVM_TEST_FRAME_STATS is set, the main loop measures
// every frame's wall-clock duration and, at shutdown, prints the average/
// worst/99th-percentile frame time plus how many times the NEWS page
// snapshot was swapped underneath the render loop -- evidence for "background
// refresh causes no visible stutter".
bool frameStatsRequested() {
    return std::getenv("PVM_TEST_FRAME_STATS") != nullptr;
}

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
// key names as spelled in keys.cfg (UP, ENTER, ESCAPE, SPACE, F1, R, 7...) or
// action names (confirm, fastext_red, digit_7...). One is injected through
// the real input path (key names via the platform's key map) every
// kSimulateIntervalFrames frames, so full keyboard-only navigation -- menu ->
// file browser -> playback -> back to menu -- can be regression tested
// without a human at the keyboard.
constexpr int kSimulateIntervalFrames = 15;

std::vector<std::string> parseSimulatedKeys() {
    std::vector<std::string> keys;
    const char* env = std::getenv("PVM_TEST_SIMULATE_KEYS");
    if (!env) {
        return keys;
    }
    std::stringstream ss{std::string(env)};
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        if (!tok.empty()) {
            keys.push_back(tok);
        }
    }
    return keys;
}

void saveScreenshotPPM(const char* path, int width, int height) {
    // RGBA + UNSIGNED_BYTE is the one glReadPixels combination every GL and
    // GLES implementation supports; the alpha channel is dropped below.
    std::vector<unsigned char> rgba(static_cast<size_t>(width) * height * 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 3);
    for (size_t i = 0, n = static_cast<size_t>(width) * height; i < n; ++i) {
        pixels[i * 3 + 0] = rgba[i * 4 + 0];
        pixels[i * 3 + 1] = rgba[i * 4 + 1];
        pixels[i * 3 + 2] = rgba[i * 4 + 2];
    }

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

// ImGui has no built-in anisotropic (independent X/Y) scale for text or
// windows -- io.FontGlobalScale and window scale are both single scalars.
// This gets independent X/Y stretch by directly rewriting the positions of
// whatever vertices get added to the *current* window's ImDrawList during
// this scope's lifetime, anchored at a fixed screen-space point so that
// point doesn't move while everything else grows/shrinks around it.
//
// Construct it right before the ImGui call(s) to stretch (Text, Selectable,
// a whole window's content, ...) and let it go out of scope right after.
// Nesting is fine and compounds correctly: an inner scope's already-moved
// vertices just get moved again by an outer scope's transform, which is
// exactly how e.g. "menu scale" (the whole panel) and "text scale" (each
// row's glyphs individually) are meant to combine.
//
// One real limitation: ImGui child windows (BeginChild/EndChild, used for
// the scrollable row list) get their own separate ImDrawList, so a scope
// opened in the parent won't see vertices drawn inside a child -- callers
// need a separate scope inside the child (see drawScrollableRows()).
class VertexScaleScope {
public:
    VertexScaleScope(float scaleX, float scaleY, ImVec2 anchor)
        : scaleX_(scaleX), scaleY_(scaleY), anchor_(anchor), drawList_(ImGui::GetWindowDrawList()),
          startVtx_(drawList_->VtxBuffer.Size) {}

    // Convenience: anchor at the current cursor position, for a single
    // piece of text/UI that should grow from where it's about to be drawn.
    VertexScaleScope(float scaleX, float scaleY)
        : VertexScaleScope(scaleX, scaleY, ImGui::GetCursorScreenPos()) {}

    ~VertexScaleScope() {
        if (scaleX_ == 1.0f && scaleY_ == 1.0f) {
            return;
        }
        for (int i = startVtx_; i < drawList_->VtxBuffer.Size; ++i) {
            ImDrawVert& v = drawList_->VtxBuffer[i];
            v.pos.x = anchor_.x + (v.pos.x - anchor_.x) * scaleX_;
            v.pos.y = anchor_.y + (v.pos.y - anchor_.y) * scaleY_;
        }
    }

    VertexScaleScope(const VertexScaleScope&) = delete;
    VertexScaleScope& operator=(const VertexScaleScope&) = delete;

private:
    float scaleX_;
    float scaleY_;
    ImVec2 anchor_;
    ImDrawList* drawList_;
    int startVtx_;
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

// Aspect-ratio override cycled with 'B' during playback. Names are for
// display; values are what's forwarded to mpv's video-aspect-override
// ("no" = default/unchanged, i.e. the file's own aspect).
const std::vector<std::string> kAspectRatioNames = {"Default", "4:3", "5:4", "16:9", "16:10"};
const std::vector<std::string> kAspectRatioValues = {"no", "4:3", "5:4", "16:9", "16:10"};

// Video scale mode cycled with 'V' during playback -- FIT (mpv's normal
// letterbox/pillarbox), FILL (stretch to fill, distorting), CROP (uniform
// zoom to fill, cropping overflow, no distortion). See App::renderFrame().
const std::vector<std::string> kVideoScaleModeNames = {"FIT", "FILL", "CROP"};

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

bool App::init(Platform& platform) {
    platform_ = &platform;

    if (!mpv_.init(platform)) {
        std::fprintf(stderr, "Failed to initialize mpv player\n");
        return false;
    }

    // Shipped read-only files (shaders, fonts, *.default.cfg) vs. files the
    // user edits and the app writes (config.cfg, *.cfg overrides, caches).
    const std::string assetDir = platform.assetDir();
    const std::string dataDir = platform.dataDir();

    if (std::vector<std::string> roots = platform.defaultMediaRoots(); !roots.empty()) {
        mediaRoots_ = std::move(roots);
    }
    monitorChoiceNames_ = platform.displayNames();

    autoCloseFrames_ = autoCloseFrameCount();
    exerciseControls_ = exerciseControlsRequested();
    simulatedKeys_ = parseSimulatedKeys();
    frameStats_ = frameStatsRequested();
    lastFrameTime_ = platform.now();

    fontPath_ = assetDir + "/assets/fonts/JetBrainsMono-Regular.ttf";
    fontsDir_ = assetDir + "/assets/fonts";
    scanAvailableFonts();

    // Load persisted settings before the first font bake below, so a
    // restart picks up right where the user left it. Pre-populate from
    // this App's own defaults so any field absent from the file (missing
    // file entirely on first run, or an older file written before a field
    // existed) keeps its default rather than becoming zero-initialized.
    configPath_ = dataDir + "/config.cfg";
    AppSettings loadedSettings;
    loadedSettings.fontSizePx = fontSizePx_;
    loadedSettings.fontFile = selectedFontFile_;
    loadedSettings.outlineEnabled = outlineEnabled_;
    loadedSettings.outlineR = outlineR_;
    loadedSettings.outlineG = outlineG_;
    loadedSettings.outlineB = outlineB_;
    loadedSettings.outlineStrength = outlineStrength_;
    loadedSettings.fontR = fontR_;
    loadedSettings.fontG = fontG_;
    loadedSettings.fontB = fontB_;
    loadedSettings.menuPositionIndex = menuPositionIndex_;
    loadedSettings.selectionStyleIndex = selectionStyleIndex_;
    loadedSettings.menuScaleX = menuScaleX_;
    loadedSettings.menuScaleY = menuScaleY_;
    loadedSettings.textScaleX = textScaleX_;
    loadedSettings.textScaleY = textScaleY_;
    loadedSettings.teletextMenuScaleX = teletextMenuScaleX_;
    loadedSettings.teletextMenuScaleY = teletextMenuScaleY_;
    loadedSettings.teletextTextScaleX = teletextTextScaleX_;
    loadedSettings.teletextTextScaleY = teletextTextScaleY_;
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
    loadedSettings.volume = volume_;
    loadedSettings.videoScaleModeIndex = videoScaleModeIndex_;
    loadedSettings.aspectOverrideIndex = aspectOverrideIndex_;
    loadedSettings.fullscreen = fullscreen_;
    loadedSettings.monitorIndex = monitorIndex_;
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

    outlineEnabled_ = loadedSettings.outlineEnabled;
    outlineR_ = std::clamp(loadedSettings.outlineR, 0, 255);
    outlineG_ = std::clamp(loadedSettings.outlineG, 0, 255);
    outlineB_ = std::clamp(loadedSettings.outlineB, 0, 255);
    outlineStrength_ = std::clamp(loadedSettings.outlineStrength, 1, 6);
    fontR_ = std::clamp(loadedSettings.fontR, 0, 255);
    fontG_ = std::clamp(loadedSettings.fontG, 0, 255);
    fontB_ = std::clamp(loadedSettings.fontB, 0, 255);

    menuPositionIndex_ = loadedSettings.menuPositionIndex % static_cast<int>(kMenuPositionNames.size());
    if (menuPositionIndex_ < 0) {
        menuPositionIndex_ += static_cast<int>(kMenuPositionNames.size());
    }
    const int selectionStyleCount = static_cast<int>(kSelectionStyleNames.size());
    selectionStyleIndex_ = ((loadedSettings.selectionStyleIndex % selectionStyleCount) + selectionStyleCount) %
                           selectionStyleCount;
    menuScaleX_ = std::clamp(loadedSettings.menuScaleX, 0.3f, 3.0f);
    menuScaleY_ = std::clamp(loadedSettings.menuScaleY, 0.3f, 3.0f);
    textScaleX_ = std::clamp(loadedSettings.textScaleX, 0.3f, 3.0f);
    textScaleY_ = std::clamp(loadedSettings.textScaleY, 0.3f, 3.0f);
    teletextMenuScaleX_ = std::clamp(loadedSettings.teletextMenuScaleX, 0.3f, 3.0f);
    teletextMenuScaleY_ = std::clamp(loadedSettings.teletextMenuScaleY, 0.3f, 3.0f);
    teletextTextScaleX_ = std::clamp(loadedSettings.teletextTextScaleX, 0.3f, 3.0f);
    teletextTextScaleY_ = std::clamp(loadedSettings.teletextTextScaleY, 0.3f, 3.0f);
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
    volume_ = std::clamp(loadedSettings.volume, 0, 100);
    mpv_.setVolume(volume_);
    const int videoScaleModeCount = static_cast<int>(kVideoScaleModeNames.size());
    videoScaleModeIndex_ = ((loadedSettings.videoScaleModeIndex % videoScaleModeCount) + videoScaleModeCount) %
                           videoScaleModeCount;
    const int aspectRatioCount = static_cast<int>(kAspectRatioNames.size());
    aspectOverrideIndex_ =
        ((loadedSettings.aspectOverrideIndex % aspectRatioCount) + aspectRatioCount) % aspectRatioCount;
    mpv_.setAspectOverride(kAspectRatioValues[static_cast<size_t>(aspectOverrideIndex_)]);
    // Fullscreen/Monitor are loaded verbatim even where the platform has no
    // window modes (they're hidden there but written back unchanged, so
    // config.cfg stays shareable across platforms); they only act where it does.
    fullscreen_ = loadedSettings.fullscreen;
    if (platform.supportsWindowModes()) {
        const int monitorChoiceCount = static_cast<int>(monitorChoiceNames_.size());
        monitorIndex_ = std::clamp(loadedSettings.monitorIndex, 0, monitorChoiceCount - 1);
        if (fullscreen_) {
            platform.setFullscreen(true, monitorIndex_);
        }
    } else {
        monitorIndex_ = std::max(loadedSettings.monitorIndex, 0);
    }

    blitProgram_ = loadShaderProgram(assetDir + "/shaders/passthrough.vert", assetDir + "/shaders/blit.frag");
    if (!blitProgram_) {
        std::fprintf(stderr, "Failed to load blit shader\n");
        return false;
    }

    // Fullscreen triangle needs no vertex attributes, but core profile still
    // requires a bound VAO to issue a draw call.
    glGenVertexArrays(1, &blitVao_);

    crtProgram_ = loadShaderProgram(assetDir + "/shaders/passthrough.vert", assetDir + "/shaders/crt.frag");
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
    applyTextColor();

    if (!platform.imguiInit()) {
        std::fprintf(stderr, "Failed to initialize the ImGui platform backend\n");
        return false;
    }
    ImGui_ImplOpenGL3_Init(platform.glslVersion());
    imguiInitialized_ = true;

    rootMenu_.setItems({{"PLAY MEDIA"}, {"NEWS"}, {"TAGESSCHAU"}, {"ARD"}, {"ZDF"}, {"SETTINGS"}, {"EXIT"}});
    initTeletextSection(newsSection_, dataDir, assetDir, "news", "PVM_NEWS_CONFIG");
    initTeletextSection(tagesschauSection_, dataDir, assetDir, "tagesschau", "PVM_TAGESSCHAU_CONFIG");
    initMvwSection(ardSection_, dataDir, assetDir, "ard", "PVM_ARD_CONFIG");
    initMvwSection(zdfSection_, dataDir, assetDir, "zdf", "PVM_ZDF_CONFIG");

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
    // AddFontFromFileTTF() asserts (aborting debug builds) on a file that
    // isn't there, so only hand it paths that exist.
    auto addFont = [&](const std::string& path) {
        std::error_code ec;
        return fs::is_regular_file(path, ec) &&
               io.Fonts->AddFontFromFileTTF(path.c_str(), static_cast<float>(fontSizePx_)) != nullptr;
    };
    const std::string path = resolveFontPath();
    if (addFont(path)) {
        return;
    }
    std::fprintf(stderr, "Failed to load font %s, falling back to bundled default\n", path.c_str());
    if (path != fontPath_ && addFont(fontPath_)) {
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
    // handleInput(), i.e. while applying the frame's input events at the top of the frame,
    // before this frame's ImGui::NewFrame()/Render().
    ImGui_ImplOpenGL3_DestroyDeviceObjects();
    ImGui_ImplOpenGL3_CreateDeviceObjects();
}

void App::saveCurrentSettings() const {
    AppSettings settings;
    settings.fontSizePx = fontSizePx_;
    settings.fontFile = selectedFontFile_;
    settings.outlineEnabled = outlineEnabled_;
    settings.outlineR = outlineR_;
    settings.outlineG = outlineG_;
    settings.outlineB = outlineB_;
    settings.outlineStrength = outlineStrength_;
    settings.fontR = fontR_;
    settings.fontG = fontG_;
    settings.fontB = fontB_;
    settings.menuPositionIndex = menuPositionIndex_;
    settings.selectionStyleIndex = selectionStyleIndex_;
    settings.menuScaleX = menuScaleX_;
    settings.menuScaleY = menuScaleY_;
    settings.textScaleX = textScaleX_;
    settings.textScaleY = textScaleY_;
    settings.teletextMenuScaleX = teletextMenuScaleX_;
    settings.teletextMenuScaleY = teletextMenuScaleY_;
    settings.teletextTextScaleX = teletextTextScaleX_;
    settings.teletextTextScaleY = teletextTextScaleY_;
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
    settings.volume = volume_;
    settings.videoScaleModeIndex = videoScaleModeIndex_;
    settings.aspectOverrideIndex = aspectOverrideIndex_;
    settings.fullscreen = fullscreen_;
    settings.monitorIndex = monitorIndex_;
    saveSettings(configPath_, settings);
}

bool App::loadMedia(const std::string& path) {
    return mpv_.loadFile(path);
}

void App::frame(const std::vector<input::InputEvent>& events) {
    if (frameStats_) {
        const double now = platform_->now();
        if (frameCount_ > 5) {  // skip window/GL warm-up
            frameMs_.push_back((now - lastFrameTime_) * 1000.0);
            if (frameMs_.back() > 100.0) {
                std::fprintf(stdout, "[stats] slow frame %d: %.0f ms (screen=%d)\n", frameCount_, frameMs_.back(),
                             static_cast<int>(screen_));
            }
        }
        lastFrameTime_ = now;
        const std::shared_ptr<const teletext::PageStore> current = newsSection_.service->snapshot();
        if (lastSnapshot_ && current.get() != lastSnapshot_) {
            ++snapshotSwaps_;
            std::fprintf(stdout, "[stats] news snapshot swapped (frame %d, %zu pages)\n", frameCount_,
                         current->size());
        }
        lastSnapshot_ = current.get();
    }
    for (const input::InputEvent& event : events) {
        handleInput(event);
    }
    mpv_.pollEvents();

    if (screen_ == Screen::Playing && mpv_.consumeEndOfFile()) {
        onPlaybackStopped();
    }
    // Keep the display awake while actually playing (not while paused, or on
    // any other screen).
    platform_->setKeepAwake(screen_ == Screen::Playing && !mpv_.isPaused());

    ImGui_ImplOpenGL3_NewFrame();
    platform_->imguiNewFrame();
    ImGui::NewFrame();

    int fbWidth, fbHeight;
    platform_->framebufferSize(fbWidth, fbHeight);
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

    if (exerciseControls_ && screen_ == Screen::Playing) {
        if (frameCount_ == 20) {
            std::fprintf(stdout, "[test] t=%.2f paused=%d -> togglePause()\n",
                         mpv_.timePositionSeconds(), mpv_.isPaused());
            mpv_.togglePause();
        } else if (frameCount_ == 40) {
            std::fprintf(stdout, "[test] t=%.2f paused=%d -> togglePause()\n",
                         mpv_.timePositionSeconds(), mpv_.isPaused());
            mpv_.togglePause();
        } else if (frameCount_ == 60) {
            std::fprintf(stdout, "[test] t=%.2f paused=%d -> seekRelative(+5)\n",
                         mpv_.timePositionSeconds(), mpv_.isPaused());
            mpv_.seekRelative(5.0);
        } else if (frameCount_ == 80) {
            std::fprintf(stdout, "[test] t=%.2f paused=%d (final)\n",
                         mpv_.timePositionSeconds(), mpv_.isPaused());
        }
    }

    if (!simulatedKeys_.empty() && frameCount_ > 0 && frameCount_ % kSimulateIntervalFrames == 0) {
        const size_t idx = static_cast<size_t>(frameCount_ / kSimulateIntervalFrames) - 1;
        if (idx < simulatedKeys_.size()) {
            std::fprintf(stdout, "[test] simulate key #%zu = %s (screen=%d)\n", idx, simulatedKeys_[idx].c_str(),
                         static_cast<int>(screen_));
            injectSimulatedKey(simulatedKeys_[idx]);
        }
    }

    ++frameCount_;
    if (autoCloseFrames_ > 0 && frameCount_ >= autoCloseFrames_) {
        // Read before the caller swaps buffers, while the back buffer still
        // holds this frame.
        if (const char* path = screenshotPath()) {
            saveScreenshotPPM(path, fbWidth, fbHeight);
        }
        platform_->requestQuit();
    }
}

void App::injectSimulatedKey(const std::string& token) {
    std::vector<input::InputEvent> events;
    if (!platform_->translateKeyName(token, events)) {
        const std::optional<input::Action> action = input::actionFromName(token);
        if (!action) {
            std::fprintf(stderr, "[test] unknown simulated key '%s'\n", token.c_str());
            return;
        }
        events.push_back(input::InputEvent{*action, input::Phase::Press});
    }
    for (const input::InputEvent& event : events) {
        handleInput(event);
    }
}

void App::reportFrameStats() {
    if (frameStats_ && !frameMs_.empty()) {
        std::vector<double> sorted = frameMs_;
        std::sort(sorted.begin(), sorted.end());
        double sum = 0.0;
        for (double ms : frameMs_) sum += ms;
        std::fprintf(stdout,
                     "[stats] %zu frames: avg %.2f ms, p99 %.2f ms, worst %.2f ms, %d news snapshot swaps\n",
                     frameMs_.size(), sum / static_cast<double>(frameMs_.size()),
                     sorted[static_cast<size_t>(static_cast<double>(sorted.size() - 1) * 0.99)], sorted.back(),
                     snapshotSwaps_);
    }
    frameMs_.clear();
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
    platform_->framebufferSize(width, height);
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

        // Video scale mode ('V'): FIT (identity -- mpv's own letterbox/
        // pillarbox stands as rendered) is the default; FILL and CROP both
        // sample a cropped sub-rect of the texture instead of the whole
        // thing, computed from the video's effective aspect (post any 'B'
        // override) vs. the window's.
        //
        // contentFracX/Y is the fraction of the FBO each axis is actually
        // covered by picture in FIT mode (1.0 on whichever axis already
        // matches the window; <1.0 on the axis with bars).
        float uvScaleX = 1.0f, uvScaleY = 1.0f, uvOffsetX = 0.0f, uvOffsetY = 0.0f;
        int videoW = 0, videoH = 0;
        if (videoScaleModeIndex_ != 0 && width > 0 && height > 0 &&
            mpv_.videoDisplaySize(videoW, videoH) && videoW > 0 && videoH > 0) {
            const float videoAspect = static_cast<float>(videoW) / static_cast<float>(videoH);
            const float windowAspect = static_cast<float>(width) / static_cast<float>(height);
            const float contentFracX = (videoAspect > windowAspect) ? 1.0f : videoAspect / windowAspect;
            const float contentFracY = (videoAspect > windowAspect) ? windowAspect / videoAspect : 1.0f;

            if (videoScaleModeIndex_ == 1) {
                // FILL: crop only the bars' axis down to just its content,
                // leaving the other axis untouched -- stretches the result
                // to fill the screen, distorting the picture (cheapest
                // possible "no bars" change: a UV crop, nothing else).
                uvScaleX = contentFracX;
                uvOffsetX = (1.0f - uvScaleX) * 0.5f;
                uvScaleY = contentFracY;
                uvOffsetY = (1.0f - uvScaleY) * 0.5f;
            } else {
                // CROP: uniform zoom by 1/min(contentFracX, contentFracY)
                // applied to *both* axes equally, so the sampled rect keeps
                // the video's exact aspect ratio (no distortion) while
                // still covering the whole screen -- the overflow this
                // creates on the axis that had no bars is what gets
                // cropped off (e.g. left/right for a 16:9 video letterboxed
                // top/bottom in a 4:3 window).
                const float cropFrac = std::min(contentFracX, contentFracY);
                uvScaleX = cropFrac;
                uvScaleY = cropFrac;
                uvOffsetX = (1.0f - cropFrac) * 0.5f;
                uvOffsetY = (1.0f - cropFrac) * 0.5f;
            }
        }

        glUseProgram(blitProgram_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, videoTexture);
        glUniform1i(glGetUniformLocation(blitProgram_, "uTexture"), 0);
        glUniform2f(glGetUniformLocation(blitProgram_, "uUvScale"), uvScaleX, uvScaleY);
        glUniform2f(glGetUniformLocation(blitProgram_, "uUvOffset"), uvOffsetX, uvOffsetY);

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
        case Screen::News:
            renderTeletext();
            break;
        case Screen::RootMenu:
        case Screen::FileBrowser:
        case Screen::PickStartDirectory:
            renderMenu();
            break;
    }
}

void App::renderPlaybackHud() {
    if (mediaInfoVisible_) {
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;

        ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f));
        ImGui::SetNextWindowBgAlpha(0.35f);
        ImGui::Begin("HUD", nullptr, flags);

        {
            // No "menu scale" here -- the playback HUD isn't a menu screen --
            // but text scale still applies everywhere text is drawn.
            VertexScaleScope textScope(textScaleX_, textScaleY_);
            drawOutlinedText(mpv_.filename().empty()          ? "(no media loaded)"
                             : !playbackTitleOverride_.empty() ? playbackTitleOverride_
                                                               : basename(mpv_.filename()));

            char timeLine[64];
            std::snprintf(timeLine, sizeof(timeLine), "%s / %s", formatTimestamp(mpv_.timePositionSeconds()).c_str(),
                          formatTimestamp(mpv_.durationSeconds()).c_str());
            drawOutlinedText(timeLine);
            drawOutlinedText(mpv_.isPaused() ? "PAUSED" : "PLAYING");
        }

        ImGui::End();
    }

    if (currentMediaKind_ == MediaKind::Audio) {
        renderAudioIndicator();
        renderAudioProgressBar();
    }

    if (platform_->now() < volumeIndicatorHideAtTime_) {
        renderVolumeIndicator();
    }

    if (osdMenuVisible_) {
        renderOsdMenu();
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// No audio visualization (explicitly out of scope, see PLAN.md) -- just a
// static, type-aware indicator so an audio-only file doesn't just look like
// a stuck/blank video screen.
void App::renderAudioIndicator() {
    // ImGui positioning is in its own logical coordinate space (ImGuiIO::
    // DisplaySize), which is the platform's logical window size, not the raw GL
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
    {
        VertexScaleScope textScope(textScaleX_, textScaleY_);
        drawOutlinedText("[ AUDIO ]");
    }
    ImGui::SetWindowFontScale(1.0f);
    ImGui::End();
}

// A minimal playback-position bar for audio, drawn along the bottom edge
// of the screen: an unfilled white border (the track) with a small gap
// inside it, then a filled white rectangle that grows left-to-right as
// mpv's time-pos advances toward duration. Uses the foreground draw list
// directly (screen-space, no ImGui window needed) since it's not tied to
// any particular window's content area -- deliberately not hooked up to
// menu/text scale, matching the "just a simple bar" ask.
void App::renderAudioProgressBar() {
    const ImVec2 display = ImGui::GetIO().DisplaySize;

    constexpr float kMargin = 16.0f;      // inset from the screen edges
    constexpr float kBarHeight = 20.0f;
    constexpr float kBorderThickness = 2.0f;
    constexpr float kInnerPad = 4.0f;     // gap between border and fill

    const ImVec2 outerMin(kMargin, display.y - kMargin - kBarHeight);
    const ImVec2 outerMax(display.x - kMargin, display.y - kMargin);

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    drawList->AddRect(outerMin, outerMax, IM_COL32(255, 255, 255, 255), 0.0f, 0, kBorderThickness);

    const double duration = mpv_.durationSeconds();
    const double position = mpv_.timePositionSeconds();
    float fraction = 0.0f;
    if (duration > 0.0) {
        fraction = static_cast<float>(std::clamp(position / duration, 0.0, 1.0));
    }

    const ImVec2 innerMin(outerMin.x + kInnerPad, outerMin.y + kInnerPad);
    const ImVec2 innerMaxFull(outerMax.x - kInnerPad, outerMax.y - kInnerPad);
    if (fraction > 0.0f) {
        const ImVec2 innerMax(innerMin.x + (innerMaxFull.x - innerMin.x) * fraction, innerMaxFull.y);
        drawList->AddRectFilled(innerMin, innerMax, IM_COL32(255, 255, 255, 255));
    }
}

// "VOL" plus a 25-bar level meter (each bar = 4% of volume_), shown for a
// couple seconds after a volume change then auto-hidden -- see
// volumeIndicatorHideAtTime_. Lit bars (volume_ so far) are drawn taller
// than the unlit remainder, filling left to right, all bottom-aligned to
// the "VOL" text's baseline.
void App::renderVolumeIndicator() {
    const ImVec2 display = ImGui::GetIO().DisplaySize;

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                    ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground;

    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y - 70.0f), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    ImGui::Begin("VolumeIndicator", nullptr, flags);

    {
        VertexScaleScope textScope(textScaleX_, textScaleY_);
        drawOutlinedText("VOL");
    }
    ImGui::SameLine();

    constexpr int kBarCount = 25;      // each bar = 4% of volume (25 * 4 = 100)
    constexpr float kBarWidth = 8.0f;
    constexpr float kBarGap = 3.0f;
    constexpr float kBarHeightInactive = 10.0f;
    constexpr float kBarHeightActive = 16.0f;

    const int activeBars = std::clamp((volume_ + 2) / 4, 0, kBarCount);  // +2: round to nearest bar
    const float textHeight = ImGui::GetTextLineHeight();
    const float baselineY = ImGui::GetCursorScreenPos().y + textHeight;
    const float originX = ImGui::GetCursorScreenPos().x;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    for (int i = 0; i < kBarCount; ++i) {
        const bool active = i < activeBars;
        const float barHeight = active ? kBarHeightActive : kBarHeightInactive;
        const float x0 = originX + static_cast<float>(i) * (kBarWidth + kBarGap);
        const float x1 = x0 + kBarWidth;
        const float y1 = baselineY;
        const float y0 = y1 - barHeight;
        const ImU32 color = active ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 255, 255, 70);
        drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), color);
    }
    ImGui::Dummy(ImVec2(static_cast<float>(kBarCount) * (kBarWidth + kBarGap) - kBarGap, textHeight));

    ImGui::End();
}

// A quick-access OSD opened with 'M' during playback, for adjusting a
// handful of settings without leaving the video/audio playing behind it.
// Deliberately plain per the request: just text (a ">" prefix marks the
// selected row instead of a drawn highlight/marker/underline), no
// background window fill, so it never blocks the picture.
void App::renderOsdMenu() {
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                    ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground;

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::Begin("OSD Menu", nullptr, flags);

    {
        VertexScaleScope textScope(textScaleX_, textScaleY_);
        drawOutlinedText("MENU");
    }

    const std::vector<SettingsRowDesc> rows = buildOsdRows();
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const bool selected = (i == osdSelectedRow_);
        const std::string line = (selected ? "> " : "  ") + formatOsdRow(rows[static_cast<size_t>(i)]);
        VertexScaleScope textScope(textScaleX_, textScaleY_);
        drawOutlinedText(line);
    }

    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// Draws `text` at the current cursor position with an outline (copies
// offset in a (2*outlineStrength_+1)^2-1 square around the origin, in
// outline{R,G,B}_, drawn first) if outlineEnabled_, then the real text on
// top in `mainColor` -- ImGui's font atlas has no native glyph
// outline/stroke, so this is the standard cheap multi-draw trick for one.
// The offset copies use SetCursorScreenPos() to land exactly on top of
// each other rather than stacking as separate lines; only the final
// (main-color) draw is left to advance the layout cursor normally, so
// callers can treat this exactly like a plain TextUnformatted() otherwise.
void App::drawOutlinedTextColored(const std::string& text, const ImVec4& mainColor) {
    if (text.empty()) {
        return;
    }
    if (outlineEnabled_) {
        const ImVec4 outlineColor(outlineR_ / 255.0f, outlineG_ / 255.0f, outlineB_ / 255.0f, 1.0f);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const int radius = std::clamp(outlineStrength_, 1, 6);
        ImGui::PushStyleColor(ImGuiCol_Text, outlineColor);
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (dx == 0 && dy == 0) {
                    continue;  // that's the main-color draw below
                }
                ImGui::SetCursorScreenPos(ImVec2(origin.x + static_cast<float>(dx), origin.y + static_cast<float>(dy)));
                ImGui::TextUnformatted(text.c_str());
            }
        }
        ImGui::PopStyleColor();
        ImGui::SetCursorScreenPos(origin);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, mainColor);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();
}

void App::drawOutlinedText(const std::string& text) {
    drawOutlinedTextColored(text, ImGui::GetStyle().Colors[ImGuiCol_Text]);
}

void App::drawOutlinedTextDisabled(const std::string& text) {
    drawOutlinedTextColored(text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
}

void App::applyTextColor() {
    ImGui::GetStyle().Colors[ImGuiCol_Text] = ImVec4(fontR_ / 255.0f, fontG_ / 255.0f, fontB_ / 255.0f, 1.0f);
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
    // Scales this whole row (marker/underline + its text, or the
    // highlight bar + its text) as one unit from the row's own top-left,
    // so a marker/underline stays attached to the text it's marking.
    VertexScaleScope textScope(textScaleX_, textScaleY_);

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
            drawOutlinedText(label);
            ImGui::Unindent(indent);
            break;
        }
        case 2: {  // Underline
            const ImVec2 textStart = ImGui::GetCursorScreenPos();
            drawOutlinedText(label);
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

// The screen-space point of the configured menuPositionIndex_ anchor
// corner. This is exactly the `pos` positionMenuWindow() passes to
// SetNextWindowPos() -- by construction, that's the one point of the
// window that ImGui pins in place regardless of the window's size, which
// makes it the natural, lag-free anchor for menu-stretch scaling too (no
// need to wait a frame for GetWindowPos()/GetWindowSize() to settle).
ImVec2 App::menuPivotAnchor() const {
    // ImGui positioning is in its own logical coordinate space (ImGuiIO::
    // DisplaySize -- the platform's logical window size), not the raw GL framebuffer
    // pixel size; those differ by the display scale factor on HiDPI/Retina
    // screens. Mixing the two silently mispositions anything but a (0,0)
    // pivot (verified: "Center" landed in the bottom-right corner on a 2x
    // display before this fix).
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    constexpr float kMargin = 24.0f;
    switch (menuPositionIndex_) {
        case 0:  // Top Left
            return ImVec2(kMargin, kMargin);
        case 1:  // Top Right
            return ImVec2(display.x - kMargin, kMargin);
        case 2:  // Center
            return ImVec2(display.x * 0.5f, display.y * 0.5f);
        case 3:  // Bottom Left
            return ImVec2(kMargin, display.y - kMargin);
        case 4:  // Bottom Right
        default:
            return ImVec2(display.x - kMargin, display.y - kMargin);
    }
}

// Positions the next ImGui window per the configurable menuPositionIndex_
// setting (a screen anchor + pivot), instead of a fixed top-left/fullscreen
// placement. Shared by all menu-family screens (root menu, file browser,
// settings) so the "menu screen position" setting affects them uniformly.
void App::positionMenuWindow() const {
    const ImVec2 pos = menuPivotAnchor();

    ImVec2 pivot;
    switch (menuPositionIndex_) {
        case 0:  // Top Left
            pivot = ImVec2(0.0f, 0.0f);
            break;
        case 1:  // Top Right
            pivot = ImVec2(1.0f, 0.0f);
            break;
        case 2:  // Center
            pivot = ImVec2(0.5f, 0.5f);
            break;
        case 3:  // Bottom Left
            pivot = ImVec2(0.0f, 1.0f);
            break;
        case 4:  // Bottom Right
        default:
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
    const ImVec2 anchor = menuPivotAnchor();
    ImGui::Begin("PVM Menu", nullptr, flags);
    // ImGui auto-sizes the window from *unscaled* content, then clips
    // drawing to that window rect; without this, a stretched (menu/text
    // scale > 1) panel gets its overflow silently cut off at what would
    // have been its unstretched edge. Widening the clip rect to the whole
    // display sidesteps that -- there's nothing else on these screens to
    // accidentally draw over.
    ImGui::PushClipRect(ImVec2(0.0f, 0.0f), ImGui::GetIO().DisplaySize, false);

    {
        VertexScaleScope menuScope(menuScaleX_, menuScaleY_, anchor);
        {
            VertexScaleScope textScope(textScaleX_, textScaleY_);
            drawOutlinedText("MENU");
        }
        ImGui::Separator();
        ImGui::Spacing();

        if (screen_ != Screen::RootMenu) {
            const bool picking = screen_ == Screen::PickStartDirectory;
            {
                VertexScaleScope textScope(textScaleX_, textScaleY_);
                char header[512];
                std::snprintf(header, sizeof(header), "%s  (%s)", picking ? "SELECT FOLDER" : "SELECT FILE",
                              fileBrowser_.currentPathLabel().c_str());
                drawOutlinedText(header);
            }
            ImGui::Spacing();
        }
    }

    if (screen_ == Screen::RootMenu) {
        drawScrollableRows(static_cast<int>(rootMenu_.items().size()), rootMenu_.selectedIndex(),
                            [&](int i) { return rootMenu_.items()[i].label; }, anchor,
                            computeListHeightBudget());
    } else if (fileBrowser_.empty()) {
        VertexScaleScope menuScope(menuScaleX_, menuScaleY_, anchor);
        VertexScaleScope textScope(textScaleX_, textScaleY_);
        drawOutlinedTextDisabled("(no matching files found)");
    } else {
        const auto& entries = fileBrowser_.entries();
        drawScrollableRows(
            static_cast<int>(entries.size()), fileBrowser_.selectedIndex(),
            [&](int i) { return entries[i].isDirectory ? entries[i].name + "/" : entries[i].name; }, anchor,
            computeListHeightBudget());
    }

    {
        VertexScaleScope menuScope(menuScaleX_, menuScaleY_, anchor);
        ImGui::Spacing();
        ImGui::Separator();
        {
            VertexScaleScope textScope(textScaleX_, textScaleY_);
            drawOutlinedTextDisabled(screen_ == Screen::RootMenu ? "UP/DOWN: Move   ENTER: Select   ESC: Exit"
                                                                  : "UP/DOWN: Move   ENTER: Open   ESC: Back");
        }
    }

    ImGui::PopClipRect();
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// See the declaration in app.h. kMargin matches menuPivotAnchor()'s own
// screen-edge margin, so the panel's positioning and its height budget
// agree on how much breathing room to leave around it. The footer (an
// explicit Spacing() + Separator() + one text line, drawn by the caller
// right after drawScrollableRows() returns) isn't on screen yet when this
// runs, so its height is estimated from style metrics rather than
// measured -- close enough for a hint line, and it only affects how much
// of the display the list claims, not correctness.
float App::computeListHeightBudget() const {
    constexpr float kMargin = 24.0f;
    const float itemHeight = ImGui::GetTextLineHeightWithSpacing();
    const float footerReserve = 2.0f * ImGui::GetStyle().ItemSpacing.y + 1.0f + itemHeight;
    const float windowPadY = ImGui::GetStyle().WindowPadding.y;
    const float contentBudget = ImGui::GetIO().DisplaySize.y - 2.0f * kMargin - 2.0f * windowPadY;
    return std::max(itemHeight, contentBudget - ImGui::GetCursorPosY() - footerReserve);
}

// Long listings (a big directory, a home folder with dozens of dotfiles, or
// the settings screen's dozen-odd rows) get a fixed-height scrolling
// region -- capped at `maxListHeight` (see computeListHeightBudget()) --
// instead of growing the window past the screen; the selected row is kept
// in view as it moves. Shared by renderMenu() and renderSettings().
void App::drawScrollableRows(int count, int selectedIndex, const std::function<std::string(int)>& labelFor,
                              ImVec2 anchor, float maxListHeight) {
    const float itemHeight = ImGui::GetTextLineHeightWithSpacing();
    const float listHeight = std::min(static_cast<float>(count) * itemHeight, maxListHeight);
    ImGui::BeginChild("MenuList", ImVec2(0.0f, listHeight));
    {
        // Widen the child's clip rect horizontally only (keep its own
        // vertical extent, so the intentional vertical clip/scroll for a
        // long list still works) -- otherwise a horizontally-stretched row
        // (menu or text scale X > 1) gets cut off at what would have been
        // its unstretched width. See the matching PushClipRect in
        // renderMenu()/renderSettings() for the same issue at the window
        // level.
        const ImVec2 clipMin = ImGui::GetWindowDrawList()->GetClipRectMin();
        const ImVec2 clipMax = ImGui::GetWindowDrawList()->GetClipRectMax();
        ImGui::PushClipRect(ImVec2(0.0f, clipMin.y), ImVec2(ImGui::GetIO().DisplaySize.x, clipMax.y), false);

        // ImGui child windows get their own ImDrawList (see VertexScaleScope's
        // comment), so menu-scale needs its own scope here rather than being
        // covered by the one in renderMenu()/renderSettings(). SetScrollHereY()
        // below still reasons about unscaled positions -- scroll-to-selection
        // stays correct, but very large vertical stretch can still clip
        // against the child's fixed-height viewport rather than growing it.
        VertexScaleScope menuScope(menuScaleX_, menuScaleY_, anchor);
        for (int i = 0; i < count; ++i) {
            const bool selected = (i == selectedIndex);
            drawMenuRow(labelFor(i), selected);
            if (selected) {
                ImGui::SetScrollHereY(0.5f);
            }
        }
        ImGui::PopClipRect();
    }
    ImGui::EndChild();
}

void App::renderSettings() {
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus;

    positionMenuWindow();
    const ImVec2 anchor = menuPivotAnchor();
    ImGui::Begin("PVM Settings", nullptr, flags);
    ImGui::PushClipRect(ImVec2(0.0f, 0.0f), ImGui::GetIO().DisplaySize, false);

    {
        VertexScaleScope menuScope(menuScaleX_, menuScaleY_, anchor);
        {
            VertexScaleScope textScope(textScaleX_, textScaleY_);
            drawOutlinedText("SETTINGS");
        }
        ImGui::Separator();
        ImGui::Spacing();
    }

    const std::vector<SettingsRowDesc> rows = buildSettingsRows();
    drawScrollableRows(
        static_cast<int>(rows.size()), settingsSelectedRow_,
        [&](int i) { return formatSettingsRow(rows[static_cast<size_t>(i)]); }, anchor,
        computeListHeightBudget());

    {
        VertexScaleScope menuScope(menuScaleX_, menuScaleY_, anchor);
        ImGui::Spacing();
        ImGui::Separator();
        {
            VertexScaleScope textScope(textScaleX_, textScaleY_);
            drawOutlinedTextDisabled("UP/DOWN: Move   LEFT/RIGHT: Change   ESC: Back");
        }
    }

    ImGui::PopClipRect();
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

    // Window modes are a desktop concept; on a platform with one fixed
    // display these rows would do nothing, so they aren't offered.
    if (platform_->supportsWindowModes()) {
        SettingsRowDesc fullscreenRow = boolRow("Fullscreen", &fullscreen_, "ON", "OFF");
        fullscreenRow.onBoolChanged = [this]() { platform_->setFullscreen(fullscreen_, monitorIndex_); };
        rows.push_back(fullscreenRow);

        SettingsRowDesc monitorRow;
        monitorRow.type = SettingsRowType::Enum;
        monitorRow.label = "Monitor";
        monitorRow.enumPtr = &monitorIndex_;
        monitorRow.enumNames = &monitorChoiceNames_;
        monitorRow.onEnumChanged = [this]() {
            if (fullscreen_) {
                platform_->setFullscreen(true, monitorIndex_);  // move the fullscreen window to the newly selected monitor right away
            }
        };
        rows.push_back(monitorRow);
    }

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

    rows.push_back(boolRow("Outline", &outlineEnabled_, "ON", "OFF"));
    rows.push_back(intRow("Outline R", &outlineR_, 0, 255, 5));
    rows.push_back(intRow("Outline G", &outlineG_, 0, 255, 5));
    rows.push_back(intRow("Outline B", &outlineB_, 0, 255, 5));
    rows.push_back(intRow("Outline Strength", &outlineStrength_, 1, 6, 1, " px"));

    // Font R/G/B each need to re-push the resulting color into ImGui's
    // global style the moment any component changes, via applyTextColor()
    // -- this shared lambda avoids repeating that callback three times.
    auto fontColorRow = [this, &intRow](std::string label, int* ptr) {
        SettingsRowDesc r = intRow(std::move(label), ptr, 0, 255, 5);
        r.onIntChanged = [this, ptr](int v) {
            *ptr = std::clamp(v, 0, 255);
            applyTextColor();
        };
        return r;
    };
    rows.push_back(fontColorRow("Font R", &fontR_));
    rows.push_back(fontColorRow("Font G", &fontG_));
    rows.push_back(fontColorRow("Font B", &fontB_));

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

    // Independent X/Y stretch. 1.00 is "no change"; the ranges below (up
    // to 3x, down to 0.3x) are chosen generously since "stretch" implies
    // going well past subtle -- there's no other correctness reason to
    // cap them there.
    rows.push_back(floatRow("Menu Scale X", &menuScaleX_, 0.3f, 3.0f, 0.05f));
    rows.push_back(floatRow("Menu Scale Y", &menuScaleY_, 0.3f, 3.0f, 0.05f));
    rows.push_back(floatRow("Text Scale X", &textScaleX_, 0.3f, 3.0f, 0.05f));
    rows.push_back(floatRow("Text Scale Y", &textScaleY_, 0.3f, 3.0f, 0.05f));

    // The same for the teletext screens (grid resize / glyph stretch).
    rows.push_back(floatRow("Teletext Menu X", &teletextMenuScaleX_, 0.3f, 3.0f, 0.05f));
    rows.push_back(floatRow("Teletext Menu Y", &teletextMenuScaleY_, 0.3f, 3.0f, 0.05f));
    rows.push_back(floatRow("Teletext Text X", &teletextTextScaleX_, 0.3f, 3.0f, 0.05f));
    rows.push_back(floatRow("Teletext Text Y", &teletextTextScaleY_, 0.3f, 3.0f, 0.05f));

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

// A small fixed set of quick-access rows for the in-playback OSD ('M'):
// brightness/contrast/chroma (screen appearance -- the same backing
// variables as the full settings screen's rows, just under quick-OSD
// labels) plus volume (new here) and an EXIT action to close the OSD.
std::vector<App::SettingsRowDesc> App::buildOsdRows() {
    std::vector<SettingsRowDesc> rows;

    SettingsRowDesc brightnessRow;
    brightnessRow.type = SettingsRowType::Float;
    brightnessRow.label = "BRIGHTNESS";
    brightnessRow.floatPtr = &brightness_;
    brightnessRow.floatMin = -0.5f;
    brightnessRow.floatMax = 0.5f;
    brightnessRow.floatStep = 0.05f;
    rows.push_back(brightnessRow);

    SettingsRowDesc contrastRow;
    contrastRow.type = SettingsRowType::Float;
    contrastRow.label = "CONTRAST";
    contrastRow.floatPtr = &contrast_;
    contrastRow.floatMin = 0.0f;
    contrastRow.floatMax = 2.0f;
    contrastRow.floatStep = 0.05f;
    rows.push_back(contrastRow);

    SettingsRowDesc chromaRow;
    chromaRow.type = SettingsRowType::Float;
    chromaRow.label = "CHROMA";
    chromaRow.floatPtr = &saturation_;  // "chroma" is the classic monitor-OSD term for saturation
    chromaRow.floatMin = 0.0f;
    chromaRow.floatMax = 2.0f;
    chromaRow.floatStep = 0.05f;
    rows.push_back(chromaRow);

    SettingsRowDesc volumeRow;
    volumeRow.type = SettingsRowType::Int;
    volumeRow.label = "VOLUME";
    volumeRow.intPtr = &volume_;
    volumeRow.intMin = 0;
    volumeRow.intMax = 100;
    volumeRow.intStep = 5;
    volumeRow.onIntChanged = [this](int v) {
        volume_ = std::clamp(v, 0, 100);
        mpv_.setVolume(volume_);
        volumeIndicatorHideAtTime_ = platform_->now() + 2.0;
    };
    rows.push_back(volumeRow);

    SettingsRowDesc scaleRow;
    scaleRow.type = SettingsRowType::Enum;
    scaleRow.label = "SCALE";
    scaleRow.enumPtr = &videoScaleModeIndex_;
    scaleRow.enumNames = &kVideoScaleModeNames;
    rows.push_back(scaleRow);

    SettingsRowDesc aspectRow;
    aspectRow.type = SettingsRowType::Enum;
    aspectRow.label = "ASPECT";
    aspectRow.enumPtr = &aspectOverrideIndex_;
    aspectRow.enumNames = &kAspectRatioNames;
    aspectRow.onEnumChanged = [this]() {
        mpv_.setAspectOverride(kAspectRatioValues[static_cast<size_t>(aspectOverrideIndex_)]);
    };
    rows.push_back(aspectRow);

    SettingsRowDesc exitRow;
    exitRow.type = SettingsRowType::Action;
    exitRow.label = "EXIT";
    exitRow.onActivate = [this]() { osdMenuVisible_ = false; };
    rows.push_back(exitRow);

    return rows;
}

// "LABEL : value" -- deliberately plainer than formatSettingsRow()'s
// padded/justified layout, matching what was asked for this OSD
// specifically (e.g. "VOLUME : 100"). EXIT has no value to show.
std::string App::formatOsdRow(const SettingsRowDesc& row) {
    char buf[48];
    switch (row.type) {
        case SettingsRowType::Float:
            std::snprintf(buf, sizeof(buf), "%s : %.2f", row.label.c_str(), *row.floatPtr);
            break;
        case SettingsRowType::Int:
            std::snprintf(buf, sizeof(buf), "%s : %d%s", row.label.c_str(), *row.intPtr,
                          row.intSuffix.c_str());
            break;
        case SettingsRowType::Bool:
            std::snprintf(buf, sizeof(buf), "%s : %s", row.label.c_str(),
                          *row.boolPtr ? row.onLabel.c_str() : row.offLabel.c_str());
            break;
        case SettingsRowType::Enum:
            std::snprintf(buf, sizeof(buf), "%s : %s", row.label.c_str(),
                          (*row.enumNames)[static_cast<size_t>(*row.enumPtr)].c_str());
            break;
        default:
            std::snprintf(buf, sizeof(buf), "%s", row.label.c_str());
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
            if (row.onBoolChanged) {
                row.onBoolChanged();
            }
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
            break;  // not adjustable via LEFT/RIGHT; Confirm invokes onActivate (see handleInput)
    }
}

void App::activateRootMenuItem(int index) {
    switch (index) {
        case 0:  // Play Media
            enterFileBrowser(mergedExtensions());
            break;
        case 1:  // News
        case 2:  // Tagesschau
        case 3:  // ARD
        case 4:  // ZDF
            openTeletext(index);
            break;
        case 5:  // Settings
            settingsSelectedRow_ = 0;
            screen_ = Screen::Settings;
            break;
        case 6:  // Exit
            platform_->requestQuit();
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

void App::handleInput(const input::InputEvent& event) {
    using input::Action;

    if (event.phase == input::Phase::Release) {
        return;
    }
    const Action action = event.action;
    // One-shot actions (activate, leave, toggles) fire on the initial press
    // only; movement, seeking, volume and value adjustment also auto-repeat.
    const bool pressed = event.phase == input::Phase::Press;

    // Global debug toggle, available on every screen: flips the CRT
    // post-process pass on/off without a restart.
    if (action == Action::ToggleCrt) {
        if (pressed) {
            crtEnabled_ = !crtEnabled_;
            std::fprintf(stdout, "CRT effect: %s\n", crtEnabled_ ? "on" : "off");
            saveCurrentSettings();
        }
        return;
    }

    switch (screen_) {
        case Screen::RootMenu:
            switch (action) {
                case Action::Up:
                    rootMenu_.moveUp();
                    break;
                case Action::Down:
                    rootMenu_.moveDown();
                    break;
                case Action::Confirm:
                    if (pressed) {
                        activateRootMenuItem(rootMenu_.selectedIndex());
                    }
                    break;
                case Action::Back:
                    if (pressed) {
                        platform_->requestQuit();
                    }
                    break;
                default:
                    break;
            }
            break;

        case Screen::FileBrowser:
            switch (action) {
                case Action::Up:
                    fileBrowser_.moveUp();
                    break;
                case Action::Down:
                    fileBrowser_.moveDown();
                    break;
                case Action::Confirm:
                    if (pressed && !fileBrowser_.empty()) {
                        if (fileBrowser_.selectedIsDirectory()) {
                            fileBrowser_.enterSelectedDirectory();
                        } else {
                            const std::string path = fileBrowser_.selectedFilePath();
                            if (!path.empty() && loadMedia(path)) {
                                currentMediaKind_ = extensionIn(path, kVideoExtensions)   ? MediaKind::Video
                                                     : extensionIn(path, kAudioExtensions) ? MediaKind::Audio
                                                                                            : MediaKind::Unknown;
                                playbackTitleOverride_.clear();  // a local file's own basename is a real title
                                screen_ = Screen::Playing;
                            }
                        }
                    }
                    break;
                case Action::Back:
                case Action::BackSoft:
                    if (pressed && !fileBrowser_.goBack()) {
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
            switch (action) {
                case Action::Up:
                    settingsSelectedRow_ = std::max(0, settingsSelectedRow_ - 1);
                    break;
                case Action::Down:
                    settingsSelectedRow_ = std::min(rowCount - 1, settingsSelectedRow_ + 1);
                    break;
                case Action::Left:
                case Action::Right:
                    if (settingsSelectedRow_ >= 0 && settingsSelectedRow_ < rowCount) {
                        adjustSettingsRow(rows[static_cast<size_t>(settingsSelectedRow_)],
                                          action == Action::Left ? -1 : 1);
                        saveCurrentSettings();
                    }
                    break;
                case Action::Confirm:
                    if (pressed && settingsSelectedRow_ >= 0 && settingsSelectedRow_ < rowCount) {
                        const SettingsRowDesc& row = rows[static_cast<size_t>(settingsSelectedRow_)];
                        if (row.type == SettingsRowType::Action && row.onActivate) {
                            row.onActivate();
                        }
                    }
                    break;
                case Action::Back:
                case Action::BackSoft:
                    if (pressed) {
                        screen_ = Screen::RootMenu;
                    }
                    break;
                default:
                    break;
            }
            break;
        }

        case Screen::PickStartDirectory:
            switch (action) {
                case Action::Up:
                    fileBrowser_.moveUp();
                    break;
                case Action::Down:
                    fileBrowser_.moveDown();
                    break;
                case Action::Confirm:
                    if (pressed && !fileBrowser_.empty()) {
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
                case Action::Back:
                case Action::BackSoft:
                    if (pressed && !fileBrowser_.goBack()) {
                        screen_ = Screen::Settings;
                    }
                    break;
                default:
                    break;
            }
            break;

        case Screen::News:
            handleTeletextInput(event);
            break;

        case Screen::Playing:
            if (action == Action::ToggleOsd) {
                if (pressed) {
                    osdMenuVisible_ = !osdMenuVisible_;
                    osdSelectedRow_ = 0;
                }
                break;
            }

            if (osdMenuVisible_) {
                // The OSD captures navigation while open -- normal
                // seek/pause/stop below don't run. Back closes the OSD
                // rather than stopping playback, so "back" backs out of the
                // overlay first.
                std::vector<SettingsRowDesc> osdRows = buildOsdRows();
                const int osdRowCount = static_cast<int>(osdRows.size());
                switch (action) {
                    case Action::Up:
                        osdSelectedRow_ = std::max(0, osdSelectedRow_ - 1);
                        break;
                    case Action::Down:
                        osdSelectedRow_ = std::min(osdRowCount - 1, osdSelectedRow_ + 1);
                        break;
                    case Action::Left:
                    case Action::Right:
                        if (osdSelectedRow_ >= 0 && osdSelectedRow_ < osdRowCount) {
                            adjustSettingsRow(osdRows[static_cast<size_t>(osdSelectedRow_)],
                                              action == Action::Left ? -1 : 1);
                            saveCurrentSettings();
                        }
                        break;
                    case Action::Confirm:
                        if (pressed && osdSelectedRow_ >= 0 && osdSelectedRow_ < osdRowCount) {
                            const SettingsRowDesc& row = osdRows[static_cast<size_t>(osdSelectedRow_)];
                            if (row.type == SettingsRowType::Action && row.onActivate) {
                                row.onActivate();
                            }
                        }
                        break;
                    case Action::Back:
                        if (pressed) {
                            osdMenuVisible_ = false;
                        }
                        break;
                    default:
                        break;
                }
                break;
            }

            switch (action) {
                case Action::PlayPause:
                    if (pressed) {
                        mpv_.togglePause();
                    }
                    break;
                case Action::Left:
                case Action::SeekBack:
                    mpv_.seekRelative(-5.0);
                    break;
                case Action::Right:
                case Action::SeekFwd:
                    mpv_.seekRelative(5.0);
                    break;
                case Action::VideoScale:
                    if (pressed) {
                        const int count = static_cast<int>(kVideoScaleModeNames.size());
                        videoScaleModeIndex_ = (videoScaleModeIndex_ + 1) % count;
                        std::fprintf(stdout, "Video scale: %s\n",
                                     kVideoScaleModeNames[static_cast<size_t>(videoScaleModeIndex_)].c_str());
                        saveCurrentSettings();
                    }
                    break;
                case Action::AspectRatio:
                    if (pressed) {
                        const int count = static_cast<int>(kAspectRatioNames.size());
                        aspectOverrideIndex_ = (aspectOverrideIndex_ + 1) % count;
                        mpv_.setAspectOverride(kAspectRatioValues[static_cast<size_t>(aspectOverrideIndex_)]);
                        std::fprintf(stdout, "Aspect ratio: %s\n",
                                     kAspectRatioNames[static_cast<size_t>(aspectOverrideIndex_)].c_str());
                        saveCurrentSettings();
                    }
                    break;
                case Action::MediaInfo:
                    if (pressed) {
                        mediaInfoVisible_ = !mediaInfoVisible_;
                    }
                    break;
                case Action::VolumeDown:
                case Action::VolumeUp:
                    volume_ = std::clamp(volume_ + (action == Action::VolumeUp ? 5 : -5), 0, 100);
                    mpv_.setVolume(volume_);
                    volumeIndicatorHideAtTime_ = platform_->now() + 2.0;
                    saveCurrentSettings();
                    break;
                case Action::Back:
                    if (pressed) {
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

// Up/Down = move the highlighted selection among the current page's links
// (a headline, a category, a show, an episode); Confirm activates it -- a
// page link jumps there, a play link (ARD only) loads it into the player.
// Left/Right = previous/next page number. The colour keys of the bottom bar
// (F1-F4 or R/G/Y/B on a keyboard): red "-" previous page, green "+" next
// page (same as Left/Right), yellow "News" (or M) the index page 100, blue
// "Refresh" re-fetches this section right now. Digits (main row or keypad
// -- the Xbox 360 remote sends the latter) = direct page entry; Back/
// BackSoft = back: abandon a half-typed number, else climb a level, else
// leave the section.
void App::handleTeletextInput(const input::InputEvent& event) {
    using input::Action;

    teletext::Navigator& nav = activeTeletext_->nav;
    teletext::TeletextDataService& service = *activeTeletext_->service;
    const std::shared_ptr<const teletext::PageStore> snapshot = service.snapshot();
    const teletext::PageStore& store = *snapshot;
    const teletext::TeletextPage* page = store.find(nav.currentPage(), nav.currentSubPage());
    const int linkCount = page ? static_cast<int>(page->links.size()) : 0;
    const bool pressed = event.phase == input::Phase::Press;

    if (const int digit = input::digitOf(event.action); digit >= 0) {
        if (pressed) {
            nav.digit(digit, platform_->now());
        }
        return;
    }

    switch (event.action) {
        case Action::Up:
            nav.selectLink(-1, linkCount);
            break;
        case Action::Down:
            nav.selectLink(1, linkCount);
            break;
        case Action::Left:
        case Action::FastextRed:
            nav.stepPage(store, -1);
            break;
        case Action::Right:
        case Action::FastextGreen:
            nav.stepPage(store, 1);
            break;
        case Action::FastextYellow:
            if (pressed) nav.goTo(teletext::kIndexPage);
            break;
        case Action::FastextBlue:
            if (pressed) service.requestRefresh();
            break;
        case Action::Confirm:
            if (pressed) activateTeletextSelection();
            break;
        case Action::Back:
        case Action::BackSoft:
            if (pressed && !nav.back(store)) {
                screen_ = Screen::RootMenu;
            }
            break;
        default:
            break;
    }
}

void App::activateTeletextSelection() {
    teletext::Navigator& nav = activeTeletext_->nav;
    const std::shared_ptr<const teletext::PageStore> snapshot = activeTeletext_->service->snapshot();
    const teletext::TeletextPage* page = snapshot->find(nav.currentPage(), nav.currentSubPage());
    const int index = nav.selectedLink();
    if (!page || index < 0 || index >= static_cast<int>(page->links.size())) {
        return;
    }
    const teletext::RowLink& link = page->links[static_cast<size_t>(index)];
    if (link.gotoPage > 0) {
        nav.goTo(link.gotoPage);
    } else if (!link.playUrl.empty() && loadMedia(link.playUrl)) {
        currentMediaKind_ = MediaKind::Video;
        cameFromTeletext_ = true;
        playbackTitleOverride_ = link.playTitle;
        screen_ = Screen::Playing;
    }
}

// Loads the section's config (<baseName>.cfg from dataDir, else the shipped
// <baseName>.default.cfg from assetDir; the environment variable `envVar`
// overrides both) and starts its service, caching under dataDir/cache. With no usable config the section still works:
// page 100 says no sources are configured.
void App::initTeletextSection(TeletextSection& section, const std::string& dataDir, const std::string& assetDir,
                              const std::string& baseName, const char* envVar) {
    teletext::NewsConfig config;
    std::vector<std::string> warnings;

    std::string path;
    if (const char* env = std::getenv(envVar)) {
        path = env;
    } else if (fs::exists(dataDir + "/" + baseName + ".cfg")) {
        path = dataDir + "/" + baseName + ".cfg";
    } else {
        path = assetDir + "/" + baseName + ".default.cfg";
    }
    if (!teletext::loadNewsConfig(path, config, &warnings)) {
        std::fprintf(stderr, "[%s] no config at %s -- this section will have no sources\n", baseName.c_str(),
                     path.c_str());
    }
    for (const std::string& w : warnings) {
        std::fprintf(stderr, "[%s] %s\n", baseName.c_str(), w.c_str());
    }

    // Cached pages are loaded (and published) right here in the constructor;
    // the live refresh then runs on the service's own thread.
    auto service = std::make_unique<teletext::NewsService>(std::move(config), dataDir + "/cache/" + baseName);
    if (const char* env = std::getenv("PVM_TEST_NEWS_REFRESH_SECONDS")) {
        service->setRefreshIntervalSecondsForTesting(std::atoi(env));
    }
    service->start();
    section.service = std::move(service);
}

// Same idea as initTeletextSection(), but for a MediathekViewWeb-backed
// section's own config shape (a channel, favorites and the generated A-Z
// window, see teletext/mvw_config.h) and service (teletext/mvw_service.h).
void App::initMvwSection(TeletextSection& section, const std::string& dataDir, const std::string& assetDir,
                         const std::string& baseName, const char* envVar) {
    teletext::MvwConfig config;
    std::vector<std::string> warnings;

    std::string path;
    if (const char* env = std::getenv(envVar)) {
        path = env;
    } else if (fs::exists(dataDir + "/" + baseName + ".cfg")) {
        path = dataDir + "/" + baseName + ".cfg";
    } else {
        path = assetDir + "/" + baseName + ".default.cfg";
    }
    if (!teletext::loadMvwConfig(path, config, &warnings)) {
        std::fprintf(stderr, "[%s] no config at %s -- this section will have no favorites\n", baseName.c_str(),
                     path.c_str());
    }
    for (const std::string& w : warnings) {
        std::fprintf(stderr, "[%s] %s\n", baseName.c_str(), w.c_str());
    }

    auto service = std::make_unique<teletext::MvwService>(std::move(config), dataDir + "/cache/" + baseName);
    if (const char* env = std::getenv("PVM_TEST_NEWS_REFRESH_SECONDS")) {
        service->setRefreshIntervalSecondsForTesting(std::atoi(env));
    }
    service->start();
    section.service = std::move(service);
}

void App::openTeletext(int rootMenuIndex) {
    switch (rootMenuIndex) {
        case 1:
            activeTeletext_ = &newsSection_;
            break;
        case 2:
            activeTeletext_ = &tagesschauSection_;
            break;
        case 3:
            activeTeletext_ = &ardSection_;
            break;
        case 4:
            activeTeletext_ = &zdfSection_;
            break;
        default:
            activeTeletext_ = &newsSection_;
            break;
    }
    activeTeletext_->nav.goTo(teletext::kIndexPage);
    screen_ = Screen::News;
}

void App::renderTeletext() {
    teletext::Navigator& nav = activeTeletext_->nav;
    const teletext::TeletextDataService& service = *activeTeletext_->service;
    nav.update(platform_->now());

    const std::shared_ptr<const teletext::PageStore> snapshot = service.snapshot();
    teletext::TeletextPage placeholder;
    const teletext::TeletextPage* page = snapshot->find(nav.currentPage(), nav.currentSubPage());
    if (!page) {
        placeholder = teletext::makeNotFoundPage(nav.currentPage(), service.serviceName());
        page = &placeholder;
    }

    const std::time_t now = std::time(nullptr);
    const int selectedLink = page->links.empty() ? -1 : std::clamp(nav.selectedLink(), 0,
                                                                    static_cast<int>(page->links.size()) - 1);
    teletext::drawPage(*page, nav.targetLabel(), teletext::formatLocalTime(now, "%d.%m."),
                       teletext::formatLocalTime(now, "%H:%M:%S"),
                       {teletextMenuScaleX_, teletextMenuScaleY_, teletextTextScaleX_, teletextTextScaleY_},
                       selectedLink);

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void App::onPlaybackStopped() {
    currentMediaKind_ = MediaKind::Unknown;
    osdMenuVisible_ = false;
    playbackTitleOverride_.clear();

    // A play link from a teletext page (ARD): return to that exact page --
    // activeTeletext_ and its Navigator (current page + selected row) were
    // never touched while Playing, so this is the whole of "returning".
    if (cameFromTeletext_) {
        cameFromTeletext_ = false;
        screen_ = Screen::News;
        return;
    }

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

void App::shutdown() {
    reportFrameStats();
    // Joins the refresh threads (aborting any transfer in flight).
    newsSection_.service.reset();
    tagesschauSection_.service.reset();
    ardSection_.service.reset();
    zdfSection_.service.reset();
    if (imguiInitialized_) {
        ImGui_ImplOpenGL3_Shutdown();
        platform_->imguiShutdown();
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
}
