#include "mpv_player.h"

#include "gl.h"

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <cstdio>
#include <cstdlib>

#include "platform/platform.h"

namespace {

void* getProcAddressMpv(void* ctx, const char* name) {
    return static_cast<const Platform*>(ctx)->glProcAddress(name);
}

// Everything the main loop asks of mpv is asynchronous (commands and property
// sets return at once; failures come back as reply events), and what it needs
// to read every frame is observed and cached. That way an mpv core that is
// stuck -- e.g. behind a hardware decoder that stopped delivering frames --
// cannot freeze the app's own loop: the user can still back out and change
// the setting.
constexpr uint64_t kCommandReply = 1;
constexpr uint64_t kSetPropertyReply = 2;

void checkMpvError(int status, const char* what) {
    if (status < 0) {
        std::fprintf(stderr, "mpv error (%s): %s\n", what, mpv_error_string(status));
    }
}

}  // namespace

MpvPlayer::MpvPlayer() = default;

MpvPlayer::~MpvPlayer() {
    shutdown();
}

void MpvPlayer::onRenderUpdate(void* ctx) {
    auto* self = static_cast<MpvPlayer*>(ctx);
    self->redrawFlag_.store(true, std::memory_order_relaxed);
}

bool MpvPlayer::init(const Platform& platform) {
    platform_ = &platform;
    return createCore();
}

bool MpvPlayer::createCore() {
    const Platform& platform = *platform_;
    mpv_ = mpv_create();
    if (!mpv_) {
        std::fprintf(stderr, "Failed to create mpv instance\n");
        return false;
    }

    // Quiet by default; still surface real errors via checkMpvError().
    mpv_set_option_string(mpv_, "terminal", "no");
    mpv_set_option_string(mpv_, "vo", "libmpv");

#if defined(__ANDROID__)
    // MediaCodec decodes in hardware and hands the frames back to be
    // uploaded like software ones. The zero-copy "mediacodec" mode renders to
    // its own surface, which would bypass the CRT pass.
    mpv_set_option_string(mpv_, "hwdec", "mediacodec-copy");
    // This libmpv build's audio outputs are OpenSL ES (no AAudio); fall back
    // to silence rather than failing playback if the device refuses it.
    mpv_set_option_string(mpv_, "ao", "opensles");
    mpv_set_option_string(mpv_, "audio-fallback-to-null", "yes");
    // mbedTLS has no system trust store on Android: use the bundled CA list
    // (unpacked from the APK to the asset directory) for https streams.
    {
        const std::string caFile = platform.assetDir() + "/cacert.pem";
        if (std::FILE* f = std::fopen(caFile.c_str(), "rb")) {
            std::fclose(f);
            mpv_set_option_string(mpv_, "tls-ca-file", caFile.c_str());
        } else {
            std::fprintf(stderr, "No CA bundle at %s; https streams will not verify\n", caFile.c_str());
        }
    }
#endif

    // Test hook: PVM_MPV_OPTIONS="key=value;key=value" sets mpv options before
    // initialisation, to try decoder/output options without a rebuild.
    if (const char* extra = std::getenv("PVM_MPV_OPTIONS")) {
        std::string list = extra;
        size_t start = 0;
        while (start < list.size()) {
            size_t end = list.find(';', start);
            if (end == std::string::npos) end = list.size();
            const std::string item = list.substr(start, end - start);
            const size_t eq = item.find('=');
            if (eq != std::string::npos) {
                checkMpvError(mpv_set_option_string(mpv_, item.substr(0, eq).c_str(), item.substr(eq + 1).c_str()),
                              item.c_str());
            }
            start = end + 1;
        }
    }

    if (mpv_initialize(mpv_) < 0) {
        std::fprintf(stderr, "Failed to initialize mpv\n");
        mpv_terminate_destroy(mpv_);
        mpv_ = nullptr;
        return false;
    }

    mpv_observe_property(mpv_, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv_, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv_, 0, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(mpv_, 0, "hwdec-current", MPV_FORMAT_STRING);
    mpv_observe_property(mpv_, 0, "dwidth", MPV_FORMAT_INT64);
    mpv_observe_property(mpv_, 0, "dheight", MPV_FORMAT_INT64);
    mpv_observe_property(mpv_, 0, "paused-for-cache", MPV_FORMAT_FLAG);

    // mpv's own log, at the level PVM_MPV_LOG names (fatal|error|warn|info|
    // v|debug); by default only Android asks, and only for warnings, since
    // there the terminal output is otherwise invisible.
    if (const char* level = std::getenv("PVM_MPV_LOG")) {
        mpv_request_log_messages(mpv_, level);
    }
#if defined(__ANDROID__)
    else {
        mpv_request_log_messages(mpv_, "warn");
    }
#endif

    mpv_opengl_init_params glInitParams{};
    glInitParams.get_proc_address = getProcAddressMpv;
    glInitParams.get_proc_address_ctx = const_cast<Platform*>(&platform);

    int advancedControl = 0;  // Simple polling model (see class comment).
    mpv_render_param renderParams[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInitParams},
        {MPV_RENDER_PARAM_ADVANCED_CONTROL, &advancedControl},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };

    if (mpv_render_context_create(&renderCtx_, mpv_, renderParams) < 0) {
        std::fprintf(stderr, "Failed to create mpv render context\n");
        mpv_terminate_destroy(mpv_);
        mpv_ = nullptr;
        return false;
    }

    mpv_render_context_set_update_callback(renderCtx_, onRenderUpdate, this);

    return true;
}

bool MpvPlayer::loadFile(const std::string& path, double startSeconds) {
    if (!mpv_) {
        return false;
    }
    const char* cmd[] = {"loadfile", path.c_str(), nullptr};
    int status = mpv_command_async(mpv_, kCommandReply, cmd);
    checkMpvError(status, "loadfile");
    if (status >= 0) {
        filename_ = path;
        paused_ = false;
        hwdecCurrent_.clear();
        pendingStartSeconds_ = startSeconds;
        timePos_ = 0.0;
        duration_ = 0.0;
        lastProgressPos_ = -1.0;
        lastProgressTime_ = std::chrono::steady_clock::now();
    }
    return status >= 0;
}

void MpvPlayer::pollEvents() {
    if (!mpv_) {
        return;
    }

    while (true) {
        mpv_event* event = mpv_wait_event(mpv_, 0.0);
        if (event->event_id == MPV_EVENT_NONE) {
            break;
        }

        switch (event->event_id) {
            case MPV_EVENT_PROPERTY_CHANGE: {
                auto* prop = static_cast<mpv_event_property*>(event->data);
                if (prop->format == MPV_FORMAT_DOUBLE && prop->data) {
                    double value = *static_cast<double*>(prop->data);
                    if (std::string(prop->name) == "time-pos") {
                        timePos_ = value;
                    } else if (std::string(prop->name) == "duration") {
                        duration_ = value;
                    }
                } else if (prop->format == MPV_FORMAT_INT64) {
                    // Unavailable (no video yet) arrives as MPV_FORMAT_NONE, i.e. 0 here.
                    const int64_t value = prop->data ? *static_cast<int64_t*>(prop->data) : 0;
                    if (std::string(prop->name) == "dwidth") {
                        displayWidth_ = static_cast<int>(value);
                    } else if (std::string(prop->name) == "dheight") {
                        displayHeight_ = static_cast<int>(value);
                    }
                } else if (prop->format == MPV_FORMAT_FLAG && prop->data) {
                    int flag = *static_cast<int*>(prop->data);
                    if (std::string(prop->name) == "pause") {
                        paused_ = flag != 0;
                    } else if (std::string(prop->name) == "paused-for-cache") {
                        pausedForCache_ = flag != 0;
                    }
                } else if (prop->format == MPV_FORMAT_STRING && prop->data &&
                           std::string(prop->name) == "hwdec-current") {
                    const char* value = *static_cast<char**>(prop->data);
                    hwdecCurrent_ = value ? value : "";
                    if (!hwdecCurrent_.empty()) {
                        std::fprintf(stdout, "[mpv] decoder: %s\n", hwdecCurrent_.c_str());
                    }
                }
                break;
            }
            case MPV_EVENT_FILE_LOADED: {
                if (pendingStartSeconds_ > 0.0) {
                    char buf[32];
                    std::snprintf(buf, sizeof(buf), "%.3f", pendingStartSeconds_);
                    const char* cmd[] = {"seek", buf, "absolute", nullptr};
                    checkMpvError(mpv_command_async(mpv_, kCommandReply, cmd), "restore position");
                    pendingStartSeconds_ = 0.0;
                }
                break;
            }
            case MPV_EVENT_COMMAND_REPLY:
            case MPV_EVENT_SET_PROPERTY_REPLY:
                if (event->error < 0) {
                    std::fprintf(stderr, "mpv %s failed: %s\n",
                                 event->event_id == MPV_EVENT_COMMAND_REPLY ? "command" : "property change",
                                 mpv_error_string(event->error));
                }
                break;
            case MPV_EVENT_LOG_MESSAGE: {
                auto* msg = static_cast<mpv_event_log_message*>(event->data);
                std::fprintf(stderr, "[mpv] %s", msg->text);
                break;
            }
            case MPV_EVENT_END_FILE: {
                auto* ef = static_cast<mpv_event_end_file*>(event->data);
                if (ef->reason == MPV_END_FILE_REASON_ERROR) {
                    std::fprintf(stderr, "mpv playback error: %s\n",
                                 mpv_error_string(ef->error));
                    // A file that cannot be played ends playback like a
                    // finished one does, instead of leaving a black screen.
                    endOfFileFlag_ = true;
                } else if (ef->reason == MPV_END_FILE_REASON_EOF) {
                    endOfFileFlag_ = true;
                }
                break;
            }
            default:
                break;
        }
    }
}

void MpvPlayer::ensureFbo(int width, int height) {
    if (width == textureWidth_ && height == textureHeight_ && fbo_ != 0) {
        return;
    }
    destroyFbo();

    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "mpv render FBO incomplete: 0x%x\n", status);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    textureWidth_ = width;
    textureHeight_ = height;
}

void MpvPlayer::destroyFbo() {
    if (fbo_) {
        glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
    }
    if (texture_) {
        glDeleteTextures(1, &texture_);
        texture_ = 0;
    }
    textureWidth_ = 0;
    textureHeight_ = 0;
}

unsigned int MpvPlayer::render(int width, int height) {
    if (!renderCtx_ || width <= 0 || height <= 0) {
        return 0;
    }

    ensureFbo(width, height);
    redrawFlag_.store(false, std::memory_order_relaxed);

    mpv_opengl_fbo mpfbo{};
    mpfbo.fbo = static_cast<int>(fbo_);
    mpfbo.w = width;
    mpfbo.h = height;
    mpfbo.internal_format = 0;

    int flipY = 1;
    mpv_render_param renderParams[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &mpfbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flipY},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };

    mpv_render_context_render(renderCtx_, renderParams);

    return texture_;
}

void MpvPlayer::togglePause() {
    if (!mpv_) {
        return;
    }
    const char* cmd[] = {"cycle", "pause", nullptr};
    checkMpvError(mpv_command_async(mpv_, kCommandReply, cmd), "cycle pause");
}

void MpvPlayer::setHardwareDecoding(bool enabled) {
    if (!mpv_) {
        return;
    }
#if defined(__ANDROID__)
    const char* mode = enabled ? "mediacodec-copy" : "no";
#else
    const char* mode = enabled ? "auto-safe" : "no";
#endif
    checkMpvError(mpv_set_property_async(mpv_, kSetPropertyReply, "hwdec", MPV_FORMAT_STRING, &mode), "set hwdec");
    hwdecEnabled_ = enabled;
}

void MpvPlayer::watchForStall() {
    if (!mpv_ || filename_.empty()) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();

    if (!hwdecEnabled_) {
        return;
    }

    // A stream may sit waiting on the network, and that is not a stall. (For
    // a local file "paused for cache" is just what a decoder that delivers
    // nothing looks like, so it must not count there.)
    const bool isUrl = filename_.find("://") != std::string::npos;
    const bool buffering = isUrl && pausedForCache_;

    if (paused_ || buffering || timePos_ != lastProgressPos_) {
        lastProgressPos_ = timePos_;
        lastProgressTime_ = now;
        return;
    }
    const double limitSeconds = isUrl ? 10.0 : 4.0;
    if (std::chrono::duration<double>(now - lastProgressTime_).count() < limitSeconds) {
        return;
    }

    std::fprintf(stderr, "Hardware decoding made no progress for %.0f s; restarting the player with software decoding\n",
                 limitSeconds);
    const double resumeAt = timePos_;
    const std::string path = filename_;
    abandonCore();
    if (!createCore()) {
        std::fprintf(stderr, "Could not restart the player\n");
        return;
    }
    setHardwareDecoding(false);
    hwdecFallbackPending_ = true;
    loadFile(path, resumeAt);
}

void MpvPlayer::abandonCore() {
    // The old render context's callback points at this object; it must not
    // fire again. Nothing else of the old core is touched: freeing either
    // handle would wait on the stuck decoder, so they are simply left behind
    // (with their threads and the codec), which happens only after a stall.
    if (renderCtx_) {
        mpv_render_context_set_update_callback(renderCtx_, nullptr, nullptr);
    }
    renderCtx_ = nullptr;
    mpv_ = nullptr;
    destroyFbo();
    paused_ = false;
    hwdecCurrent_.clear();
    displayWidth_ = 0;
    displayHeight_ = 0;
    pausedForCache_ = false;
}

std::string MpvPlayer::statsLine() const {
    if (!mpv_) {
        return {};
    }
    auto property = [&](const char* name) {
        char* value = mpv_get_property_string(mpv_, name);
        std::string result = value ? value : "?";
        if (value) mpv_free(value);
        return result;
    };
    return "t=" + property("time-pos") + " vo-drops=" + property("frame-drop-count") +
           " decoder-drops=" + property("decoder-frame-drop-count") + " delayed=" + property("vo-delayed-frame-count") +
           " fps=" + property("estimated-vf-fps") + " hwdec=" + property("hwdec-current");
}

bool MpvPlayer::consumeHardwareDecodingFallback() {
    const bool value = hwdecFallbackPending_;
    hwdecFallbackPending_ = false;
    return value;
}

void MpvPlayer::setPaused(bool paused) {
    if (!mpv_) {
        return;
    }
    int flag = paused ? 1 : 0;
    checkMpvError(mpv_set_property_async(mpv_, kSetPropertyReply, "pause", MPV_FORMAT_FLAG, &flag), "set pause");
}

void MpvPlayer::seekRelative(double seconds) {
    if (!mpv_) {
        return;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f", seconds);
    const char* cmd[] = {"seek", buf, "relative", nullptr};
    checkMpvError(mpv_command_async(mpv_, kCommandReply, cmd), "seek");
}

void MpvPlayer::stop() {
    if (!mpv_) {
        return;
    }
    const char* cmd[] = {"stop", nullptr};
    checkMpvError(mpv_command_async(mpv_, kCommandReply, cmd), "stop");
    filename_.clear();
    hwdecCurrent_.clear();
    pendingStartSeconds_ = 0.0;
}

void MpvPlayer::setVolume(double volumePercent) {
    if (!mpv_) {
        return;
    }
    checkMpvError(mpv_set_property_async(mpv_, kSetPropertyReply, "volume", MPV_FORMAT_DOUBLE, &volumePercent),
                  "set volume");
}

void MpvPlayer::setAspectOverride(const std::string& ratio) {
    if (!mpv_) {
        return;
    }
    const char* value = ratio.c_str();
    checkMpvError(mpv_set_property_async(mpv_, kSetPropertyReply, "video-aspect-override", MPV_FORMAT_STRING, &value),
                  "set video-aspect-override");
}

bool MpvPlayer::videoDisplaySize(int& width, int& height) const {
    if (!mpv_) {
        return false;
    }
    // Observed and cached (see pollEvents()), not queried: this runs every
    // frame while playing.
    if (displayWidth_ <= 0 || displayHeight_ <= 0) {
        return false;
    }
    width = displayWidth_;
    height = displayHeight_;
    return true;
}

bool MpvPlayer::consumeEndOfFile() {
    bool value = endOfFileFlag_;
    endOfFileFlag_ = false;
    return value;
}

void MpvPlayer::shutdown() {
    destroyFbo();
    if (renderCtx_) {
        mpv_render_context_free(renderCtx_);
        renderCtx_ = nullptr;
    }
    if (mpv_) {
        mpv_terminate_destroy(mpv_);
        mpv_ = nullptr;
    }
}
