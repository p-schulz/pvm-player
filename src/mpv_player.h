#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

struct mpv_handle;
struct mpv_render_context;
class Platform;

// Thin wrapper around libmpv's client + render (OpenGL) APIs.
//
// Rendering model (deliberate Phase-1 simplification, see PLAN.md): no
// dedicated render/event thread. `pollEvents()` and `render()` are both
// called once per frame from the main loop. mpv's wakeup callback just
// flags that something happened; we don't block on it.
class MpvPlayer {
public:
    MpvPlayer();
    ~MpvPlayer();

    MpvPlayer(const MpvPlayer&) = delete;
    MpvPlayer& operator=(const MpvPlayer&) = delete;

    // Creates the mpv core and the OpenGL render context. The platform's GL
    // context must be current on the calling thread; `platform` resolves the
    // GL function pointers mpv asks for and must outlive this player.
    bool init(const Platform& platform);

    void shutdown();

    // Starts playback of a file path or URL, optionally from `startSeconds`
    // (used to pick up where playback was after the GL context had to be
    // rebuilt); the seek happens once the file has loaded.
    bool loadFile(const std::string& path, double startSeconds = 0.0);

    // Drains pending mpv core events (log messages, property changes, etc).
    // Cheap no-op when nothing happened; call once per frame.
    void pollEvents();

    // Renders the current video frame into an FBO-backed texture of the
    // given pixel size, (re-)allocating the texture if the size changed.
    // Returns the GL texture name (0 if nothing has been decoded yet).
    unsigned int render(int width, int height);

    // Playback controls (Phase 1: hardcoded keybinds call these directly).
    void togglePause();
    void setPaused(bool paused);

    // Hardware decoding on or off (the platform's kind: MediaCodec on
    // Android, mpv's "auto-safe" elsewhere). Applies from the next file.
    void setHardwareDecoding(bool enabled);

    // Call once per frame while playing. A hardware decoder that accepts the
    // stream but never delivers a frame wedges mpv's core without any error,
    // and a wedged core answers nothing again, so it cannot be told to switch
    // decoders. If nothing has progressed for a few seconds (not paused, not
    // buffering), this therefore abandons that mpv instance -- leaked on
    // purpose, since destroying it would wait on the stuck decoder -- starts a
    // fresh one with software decoding, and reloads the file at the same
    // position. See consumeHardwareDecodingFallback().
    void watchForStall();
    // True once, after watchForStall() replaced the player, so the owner can
    // re-apply what it had set on the old one (volume, aspect) and remember
    // not to use hardware decoding again.
    bool consumeHardwareDecodingFallback();

    // One line of mpv's playback health counters (dropped frames, effective
    // video fps...) for the frame-stats test hook. Queries the core
    // synchronously, so it is for diagnostics only.
    std::string statsLine() const;
    void seekRelative(double seconds);
    void stop();
    void setVolume(double volumePercent);  // 0-100 (mpv's normal softvol range)

    // Forces mpv to letterbox/pillarbox as if the video had this aspect
    // ratio (e.g. "4:3", "16:9") instead of its own; "no" restores the
    // file's real aspect. Maps directly to mpv's video-aspect-override.
    void setAspectOverride(const std::string& ratio);

    // The video's current effective display size in pixels -- i.e. after
    // any aspect-ratio override and pixel-aspect correction, so it's the
    // right thing to compare against the window size for crop math (see
    // App's video fill mode). Returns false (leaving width/height
    // untouched) if unavailable yet (e.g. nothing decoded).
    bool videoDisplaySize(int& width, int& height) const;

    // True once, the first time this is called after mpv reached natural
    // end-of-file (not a user-initiated stop() or an error). Used by the
    // Phase 3 menu to return to the root menu when playback finishes on
    // its own, not just on an explicit "back" key press.
    bool consumeEndOfFile();

    // The decoder mpv settled on for the current file: "mediacodec-copy" for
    // Android hardware decoding, "no" for software, "" before a video decoder
    // exists. Shown in the media info overlay, since a silent fallback to
    // software decoding is the classic way for playback to stutter.
    const std::string& hwdecCurrent() const { return hwdecCurrent_; }

    bool isPaused() const { return paused_; }
    double timePositionSeconds() const { return timePos_; }
    double durationSeconds() const { return duration_; }
    const std::string& filename() const { return filename_; }
    bool hasVideo() const { return textureWidth_ > 0 && textureHeight_ > 0; }

private:
    // Creates the mpv core and its render context (what init() does, and
    // what replacing a wedged player repeats).
    bool createCore();
    // Lets go of the current core and render context without destroying them.
    void abandonCore();

    const Platform* platform_ = nullptr;

    void ensureFbo(int width, int height);
    void destroyFbo();

    mpv_handle* mpv_ = nullptr;
    mpv_render_context* renderCtx_ = nullptr;

    unsigned int fbo_ = 0;
    unsigned int texture_ = 0;
    int textureWidth_ = 0;
    int textureHeight_ = 0;

    bool paused_ = false;
    bool endOfFileFlag_ = false;
    double timePos_ = 0.0;
    double duration_ = 0.0;
    std::string filename_;
    std::string hwdecCurrent_;
    double pendingStartSeconds_ = 0.0;

    // Observed properties, cached so the main loop never has to ask the core.
    int displayWidth_ = 0;
    int displayHeight_ = 0;
    bool pausedForCache_ = false;

    bool hwdecEnabled_ = false;
    bool hwdecFallbackPending_ = false;
    double lastProgressPos_ = -1.0;
    std::chrono::steady_clock::time_point lastProgressTime_{};

    // Set from mpv's render-update callback (may fire off the main thread);
    // only used as a hint, so relaxed atomics are sufficient here.
    std::atomic<bool> redrawFlag_{false};
    static void onRenderUpdate(void* ctx);
};
