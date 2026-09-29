#pragma once

// One running game: the core, its audio, the texture its picture is shown
// from, the pad state, pacing and the save files. App creates one per game
// and only draws the texture and reacts to input.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "retro/audio_out.h"
#include "retro/retro_core.h"

namespace retro {

class Session {
public:
    Session();
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // The frontend's own OpenGL loader (glProcAddress() on the app's
    // Platform) -- needed only by a hardware-rendered core's get_proc_
    // address(), so it's set once, up front, and reused by every open()/
    // openSubsystem() that turns out to need it.
    void setProcAddressResolver(std::function<void*(const char*)> resolver) {
        getProcAddress_ = std::move(resolver);
    }

    // Loads `core`, then the game at `romPath`, and starts audio. Files are
    // kept under `dataDir`/retro/: system/ (BIOS files) and options/<core>.cfg
    // (option overrides); battery saves and save states go under `savesDir`
    // if given (a user-configurable location, see App's "Save Directory"
    // setting), else default to `dataDir`/retro/saves. Needs the GL context
    // current. On failure returns false with `error` set and leaves the
    // session closed.
    bool open(const CoreInfo& core, const std::string& romPath, const std::string& dataDir,
             const std::string& savesDir, std::string& error);
    // Like open(), but for a subsystem (see SubsystemInfo, retro::pickSubsystem):
    // `contentPaths` is one path per subsystem.roms slot, in slot order.
    // Named and saved after contentPaths[0] -- the piece of content the
    // player actually picked (e.g. the Game Boy ROM in Super Game Boy).
    bool openSubsystem(const CoreInfo& core, const SubsystemInfo& subsystem, const std::vector<std::string>& contentPaths,
                       const std::string& dataDir, const std::string& savesDir, std::string& error);
    // Saves the battery RAM and releases everything (GL texture included: the
    // context must be current).
    void close();
    bool active() const { return core_ != nullptr; }

    // A RetroPad button (PadButton) went down or up. Held buttons are the
    // core's input on every frame until released.
    void setButton(int button, bool down);
    // The analog stick's continuous position (see Core::setAnalogStick);
    // forwarded once a frame while the game screen has focus, not through
    // the discrete button/action path. No-op with no core loaded.
    void setAnalogStick(int index, float x, float y);
    void releaseButtons();

    // Runs however many emulated frames are due at time `now` (seconds,
    // monotonic) and uploads the newest picture. While `paused` nothing runs
    // and the audio is muted. Needs the GL context current.
    void update(double now, bool paused);

    void reset();
    bool saveState(int slot);
    bool loadState(int slot);
    // Writes the battery RAM if it changed (also done every few seconds).
    void flushSaveRam();

    // The core asked to end the game.
    bool takeShutdownRequest() { return core_ && core_->takeShutdownRequest(); }

    // The picture: a texture of textureWidth() x textureHeight() *content*
    // (see textureStorageWidth()/Height() and needsFlipY() below for how to
    // actually sample it), to be shown at aspect() (width / height). 0 until
    // the first frame arrived. Serves both a software core's uploaded RGBA
    // texture and a hardware-rendered one's own FBO color attachment --
    // whichever is active -- so the caller doesn't need to know which.
    unsigned int texture() const;
    int textureWidth() const { return core_ ? core_->frame().width : 0; }
    int textureHeight() const { return core_ ? core_->frame().height : 0; }
    // The texture's actual allocated size, >= textureWidth()/Height(): equal
    // to them for a software core (tightly allocated every frame), but fixed
    // at the core's declared maximum for a hardware-rendered one (allocated
    // once; most frames use only a corner of it). UV coordinates need
    // scaling by textureWidth()/textureStorageWidth() (and the Height()
    // equivalent) to show just the used part -- see App::renderGameFrame().
    int textureStorageWidth() const;
    int textureStorageHeight() const;
    // Whether the texture's content needs a Y-flip to display right-side up:
    // always true for a software core (raw pixel rows arrive top-first);
    // for a hardware-rendered one, only if it did *not* ask for the normal
    // bottom-left GL convention (see HwRenderInfo::bottomLeftOrigin).
    bool needsFlipY() const;
    float aspect() const { return core_ ? core_->aspectRatio() : 4.0f / 3.0f; }
    void setSmooth(bool smooth);

    const std::string& coreName() const { return coreName_; }
    const std::string& gameName() const { return gameName_; }
    const std::string& romPath() const { return romPath_; }
    // Empty unless opened with openSubsystem(): the subsystem's own name
    // ("Super Game Boy") and, for restorePlayback() after a lost GL context,
    // what it would take to relaunch the same way.
    const std::string& subsystemDesc() const { return subsystemDesc_; }
    bool isSubsystem() const { return subsystemGameType_ != 0; }
    unsigned subsystemGameType() const { return subsystemGameType_; }
    const std::vector<std::string>& subsystemContentPaths() const { return subsystemContentPaths_; }
    // A state slot of its own for carrying a game across a lost GL context
    // (the app is rebuilt, the game is not); never shown in the menu.
    static constexpr int kRecoverySlot = -1;
    bool hasAudio() const { return audio_.running(); }

private:
    std::string statePath(int slot) const;
    void uploadFrame();
    // Shared tail of open()/openSubsystem(): loads the battery save, wires
    // audio, resets pacing and, if the core asked for one, sets up hardware
    // rendering. Takes ownership of `core` (already past load() +
    // loadGame()/loadGameSpecial()); releases it and returns false (with
    // `error` set) if hardware-render setup fails.
    bool finishOpen(std::unique_ptr<Core> core, const std::string& savePath, std::string& error);

    // Allocates the FBO a hardware-rendered core draws into (sized to its
    // declared maximum) and calls its context_reset(). Needs the GL context
    // current, same as open()/openSubsystem() themselves.
    bool setupHwRender(std::string& error);
    // The reverse: context_destroy() (if the context is still valid -- see
    // close()'s own comment) and releasing the FBO/textures/renderbuffer.
    void destroyHwRender();

    std::unique_ptr<Core> core_;
    std::function<void*(const char*)> getProcAddress_;
    AudioOut audio_;
    std::string coreName_;
    std::string gameName_;
    std::string romPath_;
    std::string subsystemDesc_;
    unsigned subsystemGameType_ = 0;
    std::vector<std::string> subsystemContentPaths_;
    std::string savePath_;   // .srm
    std::string stateBase_;  // .state, .state1, ...

    uint32_t buttons_ = 0;

    // Software path: the uploaded RGBA texture (see uploadFrame()).
    // textureWidth_/Height_ track what's currently allocated, purely so
    // uploadFrame() knows whether it can glTexSubImage2D in place or needs
    // to reallocate -- textureWidth()/Height() above read the core's own
    // frame() instead, which is equally current for either path.
    unsigned int texture_ = 0;
    int textureWidth_ = 0;
    int textureHeight_ = 0;
    uint64_t uploadedSerial_ = 0;
    bool smooth_ = false;

    // Hardware-render path: the FBO a core with an HwRenderInfo draws into
    // directly, and its color attachment (what texture() returns instead of
    // texture_ above). Sized to the core's declared maximum once at open()
    // and never resized after -- see setupHwRender().
    unsigned int hwFbo_ = 0;
    unsigned int hwColorTexture_ = 0;
    unsigned int hwDepthStencilRbo_ = 0;  // 0 if the core asked for neither
    int hwFboWidth_ = 0;
    int hwFboHeight_ = 0;

    // Pacing: when the display refreshes at about the core's rate, one core
    // frame per displayed frame (audio rate control absorbs the small
    // difference); otherwise frames are run against the clock.
    bool started_ = false;
    bool wasPaused_ = false;
    double lastNow_ = 0.0;
    double averageFrameSeconds_ = 1.0 / 60.0;
    double owed_ = 0.0;
    double lastSaveRamCheck_ = 0.0;
    double lastSampleRate_ = 0.0;
};

}  // namespace retro
