#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "input/input_action.h"
#include "mpv_player.h"
#include "retro/session.h"
#include "teletext/data_service.h"
#include "teletext/navigator.h"
#include "teletext/page.h"
#include "ui/file_browser.h"
#include "ui/menu.h"

// What a root-menu entry does (their order is kRootItems in app.cpp).
enum class RootItem { PlayMedia, Tv, Tagesschau, News, Games, Ard, Zdf, Settings, Quit };

class Platform;
struct CapturedInput;
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

    // The app going to the background (true) and coming back (false): pauses
    // playback while away and resumes it afterwards, unless the user had
    // paused it themselves.
    void setSuspended(bool suspended);

    // What is playing, enough to carry it over to a fresh App after the GL
    // context was lost (see the platform's recovery): the file, where in it,
    // and whether it was paused. `valid` is false when nothing is playing.
    struct PlaybackSnapshot {
        bool valid = false;
        std::string path;
        double positionSeconds = 0.0;
        bool paused = false;
        std::string title;
        bool fromTeletext = false;
        bool fromTv = false;
        int mediaKind = 0;
        // A running game instead (path is its ROM): resumed from a save state
        // taken for the occasion.
        bool game = false;
        // Set only for a subsystem game (retro::Session::openSubsystem(),
        // e.g. Super Game Boy): what it takes to relaunch the same way,
        // since `path` alone (slot 0's content) isn't enough. 0 = not one.
        unsigned subsystemGameType = 0;
        std::string subsystemCoreName;
        std::vector<std::string> subsystemContentPaths;
    };
    PlaybackSnapshot snapshotPlayback();
    void restorePlayback(const PlaybackSnapshot& snapshot);

private:
    enum class Screen {
    RootMenu,
    FileBrowser,
    Settings,
    PickStartDirectory,
    Playing,
    News,
    Game,
    TvMenu,
    GamesMenu,      // GAMES root-menu entry: browse ROMs, or Favorites
    GameFavorites,  // favorited ROMs, added from the browser/in-game menu (ToggleFavorite)
    PickSubsystemContent,  // picking the 2nd (3rd, ...) file of a subsystem game (PlaySubsystem)
    ControlMapping,  // Settings > Controls: rebinding keys/buttons live
};
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

    bool loadMedia(const std::string& path, double startSeconds = 0.0);

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
    // Applies a batch of events, treating the events of one press (same
    // InputEvent::group) as one: once the first of them has changed what the
    // input means (screen, OSD, page spinner), the rest are dropped, so a
    // button bound to "confirm" and "play/pause" doesn't also pause the video
    // its confirm just started.
    void dispatchEvents(const std::vector<input::InputEvent>& events);
    int inputContext() const;
    void handleTeletextInput(const input::InputEvent& event);
    // PVM_TEST_SIMULATE_KEYS entry: an action name ("confirm",
    // "fastext_red") or a key name as in keys.cfg, the latter translated by
    // the platform's real key map.
    void injectSimulatedKey(const std::string& token);
    void openTeletext(RootItem section);
    // The teletext section after the current/last one, in menu order, wrapping.
    RootItem nextSection() const;
    // Changes the playback volume by `points` (fractions accumulate across
    // calls, so analog input slower than one point per event still moves it).
    void adjustVolume(float points);
    // The page-number spinner (see PageEntry): opens on the current page,
    // and handles a teletext input event while open.
    void openPageEntry();
    void handlePageEntryInput(const input::InputEvent& event);
    void renderPageEntry();
    // Enter on a teletext page: acts on activeTeletext_->nav's selected
    // RowLink -- a page link jumps there, a play link loads it into the
    // player (remembering to return to this same teletext page on stop).
    void activateTeletextSelection();
    void activateRootMenuItem(int index);
    // Plays TV channel `index` (kTvChannels) and returns to the TV menu when it stops.
    void playTvChannel(int index);
    // GAMES root-menu entry: index 0 browses ROMs, 1 opens Favorites.
    void activateGamesMenuItem(int index);
    bool isGameFavorite(const std::string& path) const;
    // Adds `path` to favorites, or removes it if already there; persists the
    // change, refreshes the Favorites screen if it's on screen, and toasts.
    void toggleGameFavorite(const std::string& path);
    // Rebuilds gameFavoritesMenu_'s labels from gameFavorites_ (paths' basenames).
    void refreshGameFavoritesMenu();
    void loadGameFavorites(const std::string& path);
    void saveGameFavorites() const;
    void enterFileBrowser(std::vector<std::string> extensions, bool games = false);

    // Subsystem games (see retro::SubsystemInfo, e.g. bsnes's Super Game Boy):
    // a core loading more than one piece of content together. `match` names
    // the core + subsystem, `primaryPath` is the file the player already
    // picked (the ROM browser's or Favorites' selection) -- slot 0. Prompts
    // for the remaining slots one at a time (Screen::PickSubsystemContent),
    // then launches.
    void beginSubsystemPlay(const retro::SubsystemMatch& match, const std::string& primaryPath);
    // Opens the file browser (filtered to the next slot's extensions) for the
    // subsystem content still needed; called once per remaining slot.
    void promptNextSubsystemContent();
    void launchPendingSubsystem();
    void cancelPendingSubsystem();

    // Settings > Controls: rebinding keys/buttons without a restart. See
    // Platform::beginInputCapture(). handleInput()'s ControlMapping case
    // drives all of this; App::frame() polls takeCapturedInput() while
    // waiting (see controlMappingWaiting_) since it can complete on any
    // frame, not just in response to an event.
    void renderControlMapping();
    void beginControlRebind(input::Action action);
    void finishControlRebind(const CapturedInput& captured);
    void cancelControlRebind();

    // Games (libretro cores). startGame() picks the core for the ROM and
    // switches to Screen::Game; the game screen passes the pad to the core,
    // and its own menu (chord Start+Select, Escape/M on a keyboard) holds
    // save states, reset and the picture options.
    void startGame(const std::string& romPath);
    void closeGame();
    void handleGameInput(const input::InputEvent& event);
    void openGameMenu();
    std::vector<SettingsRowDesc> buildGameMenuRows();
    void renderGameFrame(int width, int height);
    void renderGameHud();
    void renderGameMenu();
    // A short message at the bottom of the screen (errors, "state saved").
    void showToast(const std::string& text);
    void renderToast();
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
    // Persisted only; acted on by the platform's launcher (see
    // Platform::hasLaunchDisplaySetting()).
    bool launchOnTopScreen_ = true;
    bool hardwareDecoding_ = false;  // the platform's default until settings are loaded
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

    // Games: the cores found at startup, the running game (retro_.active()
    // while Screen::Game), and its menu and picture options (persisted).
    std::vector<retro::CoreInfo> retroCores_;
    bool anySubsystemCores_ = false;  // any scanned core declares a subsystem (Super Game Boy, ...)

    // Settings > Controls. controlMappingRow_ indexes kControlMappingActions
    // (app.cpp); while controlMappingWaiting_, the platform is listening
    // for the next press to bind to controlMappingPendingAction_, and
    // controlMappingWaitDeadline_ (platform_->now()-based) auto-cancels it
    // if nothing arrives -- there is no button that means "cancel" here,
    // since literally any press is what's being waited for.
    int controlMappingRow_ = 0;
    bool controlMappingWaiting_ = false;
    input::Action controlMappingPendingAction_ = input::Action::Confirm;
    double controlMappingWaitDeadline_ = 0.0;
    retro::Session retro_;
    bool browsingGames_ = false;  // the file browser is listing ROMs
    bool gameMenuVisible_ = false;
    int gameMenuRow_ = 0;
    int gameSlot_ = 0;
    bool retroStartDown_ = false;
    bool retroSelectDown_ = false;
    bool suspended_ = false;
    int retroScaleIndex_ = 0;
    int retroAspectIndex_ = 0;
    bool retroSmooth_ = true;
    std::string retroLastDirectory_;
    std::string retroStartDirectory_;
    // Where save states and battery saves/memory cards go; empty means the
    // default, platform_->dataDir() + "/retro/saves" -- see retroSavesDir().
    std::string retroSavesDirectory_;
    // Which setting Screen::PickStartDirectory's folder picker is filling in.
    enum class DirectoryPickTarget { MediaStart, RomStart, SavesDirectory };
    DirectoryPickTarget directoryPickTarget_ = DirectoryPickTarget::MediaStart;
    // The resolved saves directory: retroSavesDirectory_ if set, else the default.
    std::string retroSavesDir() const;
    std::string toastText_;
    double toastHideAtTime_ = 0.0;

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
    // under cacheDir/<baseName>; cached pages are available at once.
    void initTeletextSection(TeletextSection& section, const std::string& dataDir, const std::string& assetDir,
                             const std::string& cacheDir, const std::string& baseName, const char* envVar);
    // Same idea for a MediathekViewWeb-backed section (ARD, ZDF, ...), which
    // has its own config shape (a channel, favorites and the generated A-Z
    // window) -- see teletext/mvw_config.h. `baseName` picks the config file
    // (e.g. "ard" -> ard.cfg/ard.default.cfg) and the cache subdirectory;
    // `envVar` is the config-path override environment variable.
    void initMvwSection(TeletextSection& section, const std::string& dataDir, const std::string& assetDir,
                        const std::string& cacheDir, const std::string& baseName, const char* envVar);
    TeletextSection newsSection_;
    TeletextSection tagesschauSection_;
    TeletextSection ardSection_;
    TeletextSection zdfSection_;
    TeletextSection* activeTeletext_ = &newsSection_;
    bool sectionOpened_ = false;  // a teletext section has been opened; lastSection_ is valid
    RootItem lastSection_ = RootItem::Tagesschau;

    // Direct page entry without digit keys (gamepad): three digits, one of
    // which is selected. Up/Down changes it, Left/Right moves, Confirm goes
    // to the page, Back cancels. Opened by the PageEntry action (long-press Y).
    struct PageEntry {
        bool active = false;
        int digits[3] = {1, 0, 0};
        int position = 0;
    };
    PageEntry pageEntry_;

    // True while playback is paused only because the app is in the background.
    bool pausedBySuspend_ = false;

    // Fractions carried between analog events (see InputEvent::value).
    float volumeAccum_ = 0.0f;
    float scrollAccum_ = 0.0f;

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
    Menu tvMenu_;
    bool cameFromTv_ = false;  // playback was started from the TV menu
    Menu gamesMenu_;
    // Favorited ROM paths (absolute), persisted to dataDir()/game_favorites.cfg,
    // one per line -- see loadGameFavorites()/saveGameFavorites(). gameFavoritesMenu_
    // mirrors it 1:1 (basenames) for drawScrollableRows()/selection; kept in sync
    // by refreshGameFavoritesMenu() whenever the list changes.
    std::vector<std::string> gameFavorites_;
    Menu gameFavoritesMenu_;
    std::string gameFavoritesPath_;  // dataDir()/game_favorites.cfg
    // Backs the in-game menu's FAVORITE row (see buildGameMenuRows()): the
    // SettingsRowDesc bool machinery needs an addressable bool, not a computed one.
    bool gameMenuFavoriteFlag_ = false;

    // Subsystem games (Screen::PickSubsystemContent, PlaySubsystem): the
    // core+subsystem being launched, the content paths gathered so far
    // (index 0 is always the ROM the player originally picked), and which
    // slot promptNextSubsystemContent() is currently asking for. Raw
    // pointers into retroCores_, which is never resized after init().
    const retro::CoreInfo* pendingSubsystemCore_ = nullptr;
    const retro::SubsystemInfo* pendingSubsystemInfo_ = nullptr;
    std::vector<std::string> pendingSubsystemPaths_;
    size_t pendingSubsystemSlot_ = 0;
    int tvChannelIndex_ = 0;   // the channel playing (valid while cameFromTv_)
    bool tvTriedFallback_ = false;
    FileBrowser fileBrowser_;
    std::vector<std::string> mediaRoots_ = {"."};
    // True when the roots were given on the command line (a deliberate
    // multi-root setup), false when they are the platform's defaults.
    bool mediaRootsExplicit_ = false;
};
