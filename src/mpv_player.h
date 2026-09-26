#pragma once

#include <atomic>
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

    // Starts playback of a local file path.
    bool loadFile(const std::string& path);

    // Drains pending mpv core events (log messages, property changes, etc).
    // Cheap no-op when nothing happened; call once per frame.
    void pollEvents();

    // Renders the current video frame into an FBO-backed texture of the
    // given pixel size, (re-)allocating the texture if the size changed.
    // Returns the GL texture name (0 if nothing has been decoded yet).
    unsigned int render(int width, int height);

    // Playback controls (Phase 1: hardcoded keybinds call these directly).
    void togglePause();
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

    bool isPaused() const { return paused_; }
    double timePositionSeconds() const { return timePos_; }
    double durationSeconds() const { return duration_; }
    const std::string& filename() const { return filename_; }
    bool hasVideo() const { return textureWidth_ > 0 && textureHeight_ > 0; }

private:
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

    // Set from mpv's render-update callback (may fire off the main thread);
    // only used as a hint, so relaxed atomics are sufficient here.
    std::atomic<bool> redrawFlag_{false};
    static void onRenderUpdate(void* ctx);
};
