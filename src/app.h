#pragma once

#include <functional>
#include <string>
#include <vector>

#include "mpv_player.h"
#include "ui/file_browser.h"
#include "ui/menu.h"

struct GLFWwindow;
struct ImVec2;

// Owns the GLFW window + OpenGL context and drives the main loop.
class App {
public:
    App();
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // Creates the window/GL context. Returns false on failure.
    bool init(int width, int height, const char* title);

    // Directories the root menu's "Play Media" entry browses (one or more
    // configured media folders; nested subfolders, and folders above them
    // up to the filesystem root, are all navigable -- see FileBrowser).
    void setMediaRoots(std::vector<std::string> paths);

    // Runs the main loop until the window is closed. Blocks until exit.
    void run();

    // Releases GLFW/GL/mpv resources. Safe to call multiple times.
    void shutdown();

private:
    enum class Screen { RootMenu, FileBrowser, Settings, PickStartDirectory, Playing };
    enum class MediaKind { Unknown, Video, Audio };

    // A single adjustable settings-screen row, described generically so the
    // (now fairly long) settings list doesn't need a hand-written
    // switch/case per field. Built fresh by buildSettingsRows() each time
    // it's needed; the pointers reference App's own members, so the
    // vector's lifetime only needs to outlive one render/input call.
    enum class SettingsRowType { Int, Float, Bool, Enum, Action };
    struct SettingsRowDesc {
        SettingsRowType type = SettingsRowType::Int;
        std::string label;

        int* intPtr = nullptr;
        int intMin = 0;
        int intMax = 0;
        int intStep = 1;
        std::string intSuffix;
        // Optional override for how an Int row applies a new value (e.g.
        // font size needs an atlas rebuild, not just a plain assignment).
        std::function<void(int)> onIntChanged;

        float* floatPtr = nullptr;
        float floatMin = 0.0f;
        float floatMax = 0.0f;
        float floatStep = 0.05f;

        bool* boolPtr = nullptr;
        std::string onLabel = "ON";
        std::string offLabel = "OFF";

        int* enumPtr = nullptr;
        const std::vector<std::string>* enumNames = nullptr;
        // Optional: called after an Enum row's index changes (e.g. the
        // Font row needs to rebuild the font atlas, not just store an
        // index). Most Enum rows (menu position, selection style) leave
        // this null -- storing the new index is enough on its own.
        std::function<void()> onEnumChanged;

        // Action rows aren't adjustable via LEFT/RIGHT; ENTER invokes
        // onActivate instead (e.g. "Start Directory" opens a folder
        // picker). actionValue is the current value shown for display.
        std::function<void()> onActivate;
        std::string actionValue;
    };

    bool loadMedia(const std::string& path);

    void renderFrame();
    void renderHud();
    void renderPlaybackHud();
    void renderAudioIndicator();
    void renderAudioProgressBar();
    void renderOsdMenu();
    void renderMenu();
    void renderSettings();
    void drawMenuRow(const std::string& label, bool selected);
    // `anchor` is the screen-space point that stays fixed while the list's
    // content is stretched by menuScaleX_/menuScaleY_ (see
    // VertexScaleScope in app.cpp) -- callers pass the same anchor used
    // for the rest of that window's content, so the whole panel reads as
    // one rigid stretch despite the list living in its own child window
    // (ImGui child windows get their own ImDrawList, so this can't be done
    // with a single scope wrapping the whole Begin/End call).
    void drawScrollableRows(int count, int selectedIndex, const std::function<std::string(int)>& labelFor,
                             ImVec2 anchor);
    void positionMenuWindow() const;
    // The screen-space point of the current ImGui window's configured
    // pivot corner (menuPositionIndex_), used as the fixed anchor for both
    // positioning (positionMenuWindow()) and menu-stretch scaling.
    ImVec2 menuPivotAnchor() const;

    std::vector<SettingsRowDesc> buildSettingsRows();
    static std::string formatSettingsRow(const SettingsRowDesc& row);
    void adjustSettingsRow(SettingsRowDesc& row, int direction);

    // The in-playback quick OSD (opened with 'M'): a small fixed subset of
    // settings (brightness/contrast/chroma/volume) reusing the same
    // SettingsRowDesc plumbing as the full settings screen, but with its
    // own plain "LABEL : value" text formatting and no background.
    std::vector<SettingsRowDesc> buildOsdRows();
    static std::string formatOsdRow(const SettingsRowDesc& row);

    void ensureSceneFbo(int width, int height);
    void destroySceneFbo();
    void renderPostProcess(int width, int height);

    void scanAvailableFonts();          // populates availableFontFiles_/fontChoiceNames_ from fontsDir_
    std::string resolveFontPath() const;  // fontPath_ (default) or fontsDir_/selectedFontFile_
    void loadSelectedFontIntoAtlas();   // AddFontFromFileTTF with a fallback chain; no atlas Clear()
    void applyFont();  // rebuilds the ImGui font atlas from fontSizePx_ + selectedFontFile_
    void saveCurrentSettings() const;

    void handleKey(int key, int action);
    void activateRootMenuItem(int index);
    void enterFileBrowser(std::vector<std::string> extensions);
    // Common bookkeeping for leaving Screen::Playing back to the root menu
    // (explicit stop and natural end-of-file both call this): remembers
    // the file browser's current directory as lastUsedDirectory_ and
    // persists it, so "Play Media" resumes there next time.
    void onPlaybackStopped();

    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

    GLFWwindow* window_ = nullptr;
    MpvPlayer mpv_;

    unsigned int blitProgram_ = 0;
    unsigned int blitVao_ = 0;

    // Offscreen target video + ImGui composite into, so the CRT/color-grade
    // pass has one texture to post-process.
    unsigned int sceneFbo_ = 0;
    unsigned int sceneTexture_ = 0;
    int sceneWidth_ = 0;
    int sceneHeight_ = 0;

    unsigned int crtProgram_ = 0;

    // Screen appearance (always applied) and CRT/bloom stylization
    // (gated by crtEnabled_) -- all adjustable via the settings screen and
    // persisted to config.cfg. Defaults reproduce the pre-Settings-screen
    // look exactly (no change unless the user adjusts something).
    bool crtEnabled_ = true;
    float brightness_ = 0.0f;   // additive, -0.5..0.5
    float contrast_ = 1.0f;     // multiplier around the mid-gray pivot
    float saturation_ = 1.0f;   // 0 = grayscale, 1 = normal
    float crtEffectStrength_ = 1.0f;    // overall multiplier on the CRT delta
    float bloomStrength_ = 1.0f;
    int scanlineCount_ = 480;
    float vignetteStrength_ = 0.35f;
    float colorTear_ = 0.0f;    // channel-shift amount, in pixels

    bool imguiInitialized_ = false;
    std::string fontPath_;    // bundled default (JetBrains Mono), always a valid fallback
    std::string fontsDir_;    // assets/fonts -- scanned at startup for selectable fonts
    std::string configPath_;
    int fontSizePx_ = 22;

    // Selectable fonts found in fontsDir_ at startup (filenames, excluding
    // the bundled default's own file), plus a leading "Default" choice --
    // see buildSettingsRows()'s "Font" row. selectedFontFile_ is "" for
    // Default, else a filename within fontsDir_.
    std::vector<std::string> availableFontFiles_;
    std::vector<std::string> fontChoiceNames_;
    int selectedFontChoiceIndex_ = 0;
    std::string selectedFontFile_;

    // Index into a fixed list of screen anchors (top-left/top-right/center/
    // bottom-left/bottom-right) the menu-family screens (root menu, file
    // browser, settings) are positioned at -- adjustable via the settings
    // screen.
    int menuPositionIndex_ = 0;
    int settingsSelectedRow_ = 0;

    // Independent X/Y stretch, adjustable via the settings screen (see
    // app.cpp's VertexScaleScope). menuScale* stretches the whole
    // menu-family panel (root menu/file browser/settings/directory
    // picker) as one rigid unit, anchored at its configured screen
    // position; textScale* stretches just the glyphs of every piece of
    // text drawn anywhere (menu rows, HUD, audio indicator), each
    // anchored at that text's own position -- so it compounds with
    // menuScale* for menu-screen text but also applies on its own to the
    // always-on playback HUD, which has no "menu" to stretch.
    float menuScaleX_ = 1.0f;
    float menuScaleY_ = 1.0f;
    float textScaleX_ = 1.0f;
    float textScaleY_ = 1.0f;

    // Index into a fixed list of row-selection visual styles (reverse-video
    // highlight / marker rect / underline) -- adjustable via the settings
    // screen; see drawMenuRow().
    int selectionStyleIndex_ = 0;

    bool showHiddenFiles_ = false;

    // Directory the file browser starts at (user-configurable via the
    // "Start Directory" settings row's folder picker) and the directory it
    // was last in when playback stopped (auto-tracked, takes priority over
    // startDirectory_ when set) -- see enterFileBrowser(). Both persisted.
    std::string startDirectory_;
    std::string lastUsedDirectory_;

    // In-playback quick OSD (see buildOsdRows()/renderOsdMenu()), toggled
    // with 'M'. Volume is persisted like the other settings; brightness/
    // contrast/saturation are the OSD's "chroma" row and already exist
    // above as screen-appearance settings -- the OSD just exposes them
    // under different (quick-access) labels, same backing variables.
    bool osdMenuVisible_ = false;
    int osdSelectedRow_ = 0;
    int volume_ = 100;

    // Video scaling, both cycled with a key during playback ('V'/'B') and
    // persisted:
    //   videoScaleModeIndex_ -- indexes kVideoScaleModeNames:
    //     0 FIT  (default) -- mpv's normal letterbox/pillarbox, unchanged.
    //     1 FILL -- stretches the letterboxed *content* rectangle to fill
    //       the screen on the bars' axis only, so the picture is distorted
    //       (non-uniform scale) but fills completely with the cheapest
    //       possible change (a UV crop on the existing texture, not a
    //       different render size or mpv re-scale).
    //     2 CROP -- uniform zoom-in (same factor both axes, so no
    //       distortion) until the picture covers the window, cropping the
    //       overflow off symmetrically -- see blit.frag's uUvScale/
    //       uUvOffset and renderFrame()'s comment for the derivation.
    //   aspectOverrideIndex_ -- indexes kAspectRatioNames/kAspectRatioValues
    //     (Default/4:3/5:4/16:9/16:10), forwarded to mpv's own
    //     video-aspect-override so mpv does the actual letterbox math.
    int videoScaleModeIndex_ = 0;
    int aspectOverrideIndex_ = 0;

    MediaKind currentMediaKind_ = MediaKind::Unknown;

    Screen screen_ = Screen::RootMenu;
    Menu rootMenu_;
    FileBrowser fileBrowser_;
    std::vector<std::string> mediaRoots_ = {"."};
};
