#pragma once

// One running game: the core, its audio, the texture its picture is shown
// from, the pad state, pacing and the save files. App creates one per game
// and only draws the texture and reacts to input.

#include <cstdint>
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

    // Loads `core`, then the game at `romPath`, and starts audio. Files are
    // kept under `dataDir`/retro/: system/ (BIOS files), saves/ (battery
    // saves and states), options/<core>.cfg (option overrides). Needs the GL
    // context current. On failure returns false with `error` set and leaves
    // the session closed.
    bool open(const CoreInfo& core, const std::string& romPath, const std::string& dataDir, std::string& error);
    // Saves the battery RAM and releases everything (GL texture included: the
    // context must be current).
    void close();
    bool active() const { return core_ != nullptr; }

    // A RetroPad button (PadButton) went down or up. Held buttons are the
    // core's input on every frame until released.
    void setButton(int button, bool down);
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

    // The picture: an RGBA texture of textureWidth() x textureHeight(), to be
    // shown at aspect() (width / height). 0 until the first frame arrived.
    unsigned int texture() const { return texture_; }
    int textureWidth() const { return textureWidth_; }
    int textureHeight() const { return textureHeight_; }
    float aspect() const { return core_ ? core_->aspectRatio() : 4.0f / 3.0f; }
    void setSmooth(bool smooth);

    const std::string& coreName() const { return coreName_; }
    const std::string& gameName() const { return gameName_; }
    const std::string& romPath() const { return romPath_; }
    // A state slot of its own for carrying a game across a lost GL context
    // (the app is rebuilt, the game is not); never shown in the menu.
    static constexpr int kRecoverySlot = -1;
    bool hasAudio() const { return audio_.running(); }

private:
    std::string statePath(int slot) const;
    void uploadFrame();

    std::unique_ptr<Core> core_;
    AudioOut audio_;
    std::string coreName_;
    std::string gameName_;
    std::string romPath_;
    std::string savePath_;   // .srm
    std::string stateBase_;  // .state, .state1, ...

    uint32_t buttons_ = 0;

    unsigned int texture_ = 0;
    int textureWidth_ = 0;
    int textureHeight_ = 0;
    uint64_t uploadedSerial_ = 0;
    bool smooth_ = false;

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
