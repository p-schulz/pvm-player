#include "mpv_player.h"

#include "gl.h"

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <cstdio>

#include "platform/platform.h"

namespace {

void* getProcAddressMpv(void* ctx, const char* name) {
    return static_cast<const Platform*>(ctx)->glProcAddress(name);
}

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
    mpv_ = mpv_create();
    if (!mpv_) {
        std::fprintf(stderr, "Failed to create mpv instance\n");
        return false;
    }

    // Quiet by default; still surface real errors via checkMpvError().
    mpv_set_option_string(mpv_, "terminal", "no");
    mpv_set_option_string(mpv_, "vo", "libmpv");

    if (mpv_initialize(mpv_) < 0) {
        std::fprintf(stderr, "Failed to initialize mpv\n");
        mpv_terminate_destroy(mpv_);
        mpv_ = nullptr;
        return false;
    }

    mpv_observe_property(mpv_, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv_, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv_, 0, "pause", MPV_FORMAT_FLAG);

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

bool MpvPlayer::loadFile(const std::string& path) {
    if (!mpv_) {
        return false;
    }
    const char* cmd[] = {"loadfile", path.c_str(), nullptr};
    int status = mpv_command(mpv_, cmd);
    checkMpvError(status, "loadfile");
    if (status >= 0) {
        filename_ = path;
        paused_ = false;
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
                } else if (prop->format == MPV_FORMAT_FLAG && prop->data) {
                    int flag = *static_cast<int*>(prop->data);
                    if (std::string(prop->name) == "pause") {
                        paused_ = flag != 0;
                    }
                }
                break;
            }
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
    checkMpvError(mpv_command(mpv_, cmd), "cycle pause");
}

void MpvPlayer::seekRelative(double seconds) {
    if (!mpv_) {
        return;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f", seconds);
    const char* cmd[] = {"seek", buf, "relative", nullptr};
    checkMpvError(mpv_command(mpv_, cmd), "seek");
}

void MpvPlayer::stop() {
    if (!mpv_) {
        return;
    }
    const char* cmd[] = {"stop", nullptr};
    checkMpvError(mpv_command(mpv_, cmd), "stop");
    filename_.clear();
}

void MpvPlayer::setVolume(double volumePercent) {
    if (!mpv_) {
        return;
    }
    checkMpvError(mpv_set_property(mpv_, "volume", MPV_FORMAT_DOUBLE, &volumePercent), "set volume");
}

void MpvPlayer::setAspectOverride(const std::string& ratio) {
    if (!mpv_) {
        return;
    }
    checkMpvError(mpv_set_property_string(mpv_, "video-aspect-override", ratio.c_str()),
                 "set video-aspect-override");
}

bool MpvPlayer::videoDisplaySize(int& width, int& height) const {
    if (!mpv_) {
        return false;
    }
    int64_t w = 0, h = 0;
    if (mpv_get_property(mpv_, "dwidth", MPV_FORMAT_INT64, &w) < 0 ||
        mpv_get_property(mpv_, "dheight", MPV_FORMAT_INT64, &h) < 0) {
        return false;
    }
    if (w <= 0 || h <= 0) {
        return false;
    }
    width = static_cast<int>(w);
    height = static_cast<int>(h);
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
