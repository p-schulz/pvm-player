#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "input/input_action.h"
#include "mpv_player.h"
#include "teletext/data_service.h"
#include "teletext/navigator.h"
#include "teletext/page.h"
#include "ui/file_browser.h"
#include "ui/menu.h"

class Platform;
struct ImVec2;
struct ImVec4;

// The player itself: screens, settings, rendering. Knows nothing about the
// window, event loop or OS it runs on -- all of that comes through Platform
// (see platform/platform.h), and the loop that calls frame() lives with the
// platform's entry point.
class App {
public:
    App();
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // Sets up GL resources, ImGui, mpv, settings and the teletext sections
    // against `platform`, whose GL context must be current. `platform` must
    // outlive this App. Returns false on failure.
    bool init(Platform& platform);

    // Directories the root menu's "Play Media" entry browses (one or more
    // configured media folders; nested subfolders, and folders above them
    // up to the filesystem root, are all navigable -- see FileBrowser).
    void setMediaRoots(std::vector<std::string> paths);

    // Runs one iteration of the main loop: applies `events` (the input that
    // arrived since the last frame), polls mpv and renders. The caller
    // presents the frame afterwards (Platform::swapBuffers()) and stops when
    // Platform::quitRequested().
    void frame(const std::vector<input::InputEvent>& events);

    // Releases GL/ImGui/mpv resources. Safe to call multiple times.
    void shutdown();

private:
    enum class Screen { RootMenu, FileBrowser, Settings, PickStartDirectory, Playing, News };
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
        // Optional: called after a Bool row's value changes (e.g.
        // Fullscreen needs to actually switch the window, not just flip a
        // flag). Most Bool rows leave this null.
        std::function<void()> onBoolChanged;

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
    void renderVolumeIndicator();
    void renderOsdMenu();
    void renderMenu();
    void renderTeletext();
    void renderSettings();
    void drawMenuRow(const std::string& label, bool selected);
    // Text with an optional outline (outlineEnabled_): drawn as offset
    // copies (offset magnitude = outlineStrength_ px) in outline{R,G,B}_
    // underneath, then the real text on top in `mainColor` -- ImGui has no
    // native glyph outline/stroke, so this is the standard cheap trick for
    // one. drawOutlinedText()/drawOutlinedTextDisabled() are the
    // normal-color/dim-color convenience wrappers used at most call
    // sites. Not used for the Highlight selection style's
    // ImGui::Selectable() text (it draws its own text internally, and its
    // reverse-video look already has strong contrast without an outline).
    void drawOutlinedTextColored(const std::string& text, const ImVec4& mainColor);
    void drawOutlinedText(const std::string& text);
    void drawOutlinedTextDisabled(const std::string& text);
    // Pushes font{R,G,B}_ into ImGui's global ImGuiCol_Text style color, so
    // every native ImGui text draw (including the Highlight selection
    // style's own ImGui::Selectable() text) and drawOutlinedText() (which
    // reads that same style color as its default `mainColor`) immediately
    // pick up the user's chosen font color. Called once after
    // ui::applyPvmStyle() at startup and again from each Font R/G/B
    // settings row's change callback.
    void applyTextColor();
    // `anchor` is the screen-space point that stays fixed while the list's
    // content is stretched by menuScaleX_/menuScaleY_ (see
    // VertexScaleScope in app.cpp) -- callers pass the same anchor used
    // for the rest of that window's content, so the whole panel reads as
    // one rigid stretch despite the list living in its own child window
    // (ImGui child windows get their own ImDrawList, so this can't be done
    // with a single scope wrapping the whole Begin/End call).
    void drawScrollableRows(int count, int selectedIndex, const std::function<std::string(int)>& labelFor,
                             ImVec2 anchor, float maxListHeight);
    void positionMenuWindow() const;
    // The screen-space point of the current ImGui window's configured
    // pivot corner (menuPositionIndex_), used as the fixed anchor for both
    // positioning (positionMenuWindow()) and menu-stretch scaling.
    ImVec2 menuPivotAnchor() const;
    // How tall drawScrollableRows()'s scrolling child may grow (its
    // `maxListHeight`) so the whole menu window -- whatever's already been
    // drawn above the list (title/header) plus a fixed footer allowance --
    // fits within the display height minus a margin on each side. Must be
    // called right before the corresponding drawScrollableRows(), after
    // that screen's header content so ImGui::GetCursorPosY() reflects it.
    float computeListHeightBudget() const;

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

    // Everything below this point reacts to actions only; turning raw
    // key/button events into actions is the platform's job.
    void handleInput(const input::InputEvent& event);
    void handleTeletextInput(const input::InputEvent& event);
    // PVM_TEST_SIMULATE_KEYS entry: an action name ("confirm",
    // "fastext_red") or a key name as in keys.cfg, the latter translated by
    // the platform's real key map.
    void injectSimulatedKey(const std::string& token);
    void openTeletext(int rootMenuIndex);
    // Enter on a teletext page: acts on activeTeletext_->nav's selected
    // RowLink -- a page link jumps there, a play link loads it into the
    // player (remembering to return to this same teletext page on stop).
    void activateTeletextSelection();
    void activateRootMenuItem(int index);
    void enterFileBrowser(std::vector<std::string> extensions);
    // Common bookkeeping for leaving Screen::Playing back to the root menu
    // (explicit stop and natural end-of-file both call this): remembers
    // the file browser's current directory as lastUsedDirectory_ and
    // persists it, so "Play Media" resumes there next time.
    void onPlaybackStopped();

    // Prints the PVM_TEST_FRAME_STATS summary, if that hook is on.
    void reportFrameStats();

    Platform* platform_ = nullptr;
    MpvPlayer mpv_;

    // PVM_TEST_* hook state (see the helpers at the top of app.cpp), read
    // once in init() and advanced by frame().
    int frameCount_ = 0;
    int autoCloseFrames_ = 0;
    bool exerciseControls_ = false;
    std::vector<std::string> simulatedKeys_;
    bool frameStats_ = false;
    std::vector<double> frameMs_;
    double lastFrameTime_ = 0.0;
    const teletext::PageStore* lastSnapshot_ = nullptr;
    int snapshotSwaps_ = 0;

    // Fullscreen ("Fullscreen" settings row, persisted -- also what makes
    // it launch fullscreen by default once turned on) and which display it
    // uses ("Monitor" settings row, persisted; 0 = primary, 1..N = a
    // specific one, named by monitorChoiceNames_). Both rows only exist
    // where Platform::supportsWindowModes(); the values are kept in
    // config.cfg either way so the file stays shareable between platforms.
    bool fullscreen_ = false;
    int monitorIndex_ = 0;
    std::vector<std::string> monitorChoiceNames_;

    // Text outline ("Outline"/"Outline R/G/B"/"Outline Strength" settings
    // rows, persisted) -- see drawOutlinedTextColored(). Color components
    // are 0-255; strength is the offset-copy radius in pixels.
    bool outlineEnabled_ = false;
    int outlineR_ = 0;
    int outlineG_ = 0;
    int outlineB_ = 0;
    int outlineStrength_ = 1;

    // Main text color ("Font R/G/B" settings rows, persisted), 0-255 each;
    // applied to ImGui's global text style by applyTextColor(). Defaults
    // to white, matching the previous hardcoded look.
    int fontR_ = 255;
    int fontG_ = 255;
    int fontB_ = 255;

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

    // The same four knobs for the teletext screens (NEWS/TAGESSCHAU), which
    // are laid out on their own auto-fitted grid and so ignore the menu
    // ones above: menu scale resizes the whole grid about the screen
    // centre, text scale stretches the glyphs within their cells. Settings
    // rows "Teletext Menu/Text X/Y"; see teletext::TeletextScale.
    float teletextMenuScaleX_ = 1.0f;
    float teletextMenuScaleY_ = 1.0f;
    float teletextTextScaleX_ = 1.0f;
    float teletextTextScaleY_ = 1.0f;

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

    // Filename/time/play-state block in renderPlaybackHud(), toggled with
    // '0'. Not persisted -- like osdMenuVisible_, it's a transient
    // during-playback display state, not a saved preference.
    bool mediaInfoVisible_ = true;

    // "VOL" + bar-graph indicator (renderVolumeIndicator()), shown for 2
    // seconds after any volume change (OSD VOLUME row or the ';'/':'
    // hotkeys) then auto-hidden. Platform::now()-based rather than a frame
    // counter so the 2 seconds is wall-clock, independent of frame rate.
    double volumeIndicatorHideAtTime_ = 0.0;

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

    // Teletext-style page readers: the root menu's NEWS, TAGESSCHAU, ARD and
    // ZDF entries. Each section owns its data (a TeletextDataService --
    // either a NewsService for RSS/Atom, or an MvwService for the
    // MediathekViewWeb API -- that publishes immutable page snapshots, see
    // teletext/page.h) and its own current page + link-selection +
    // digit-entry state; activeTeletext_ is the one on screen while
    // screen_ == Screen::News.
    struct TeletextSection {
        std::unique_ptr<teletext::TeletextDataService> service;
        teletext::Navigator nav;
    };
    // Loads <baseName>.cfg from dataDir (else <baseName>.default.cfg from
    // assetDir, else `envVar`'s path if that environment variable is set)
    // and starts the NEWS/TAGESSCHAU section's background refresh, caching
    // under dataDir; cached pages are available at once.
    void initTeletextSection(TeletextSection& section, const std::string& dataDir, const std::string& assetDir,
                             const std::string& baseName, const char* envVar);
    // Same idea for a MediathekViewWeb-backed section (ARD, ZDF, ...), which
    // has its own config shape (a channel, favorites and the generated A-Z
    // window) -- see teletext/mvw_config.h. `baseName` picks the config file
    // (e.g. "ard" -> ard.cfg/ard.default.cfg) and the cache subdirectory;
    // `envVar` is the config-path override environment variable.
    void initMvwSection(TeletextSection& section, const std::string& dataDir, const std::string& assetDir,
                        const std::string& baseName, const char* envVar);
    TeletextSection newsSection_;
    TeletextSection tagesschauSection_;
    TeletextSection ardSection_;
    TeletextSection zdfSection_;
    TeletextSection* activeTeletext_ = &newsSection_;

    // Set right before switching to Screen::Playing from a teletext play
    // link, so onPlaybackStopped() returns to that same teletext page (and
    // selected row) instead of the root menu -- activeTeletext_/its nav are
    // never touched by Playing, so "return" is just switching screen_ back.
    bool cameFromTeletext_ = false;

    // The playback HUD's title line: normally basename(mpv_.filename()), but
    // a teletext play link's URL has no meaningful filename (an opaque CDN
    // name, e.g. "409_16651_sendeton_....mp4"), so activateTeletextSelection()
    // sets this to the RowLink's own playTitle instead. Cleared whenever
    // Play Media starts a local file, so that keeps showing its real name.
    std::string playbackTitleOverride_;

    Screen screen_ = Screen::RootMenu;
    Menu rootMenu_;
    FileBrowser fileBrowser_;
    std::vector<std::string> mediaRoots_ = {"."};
};
