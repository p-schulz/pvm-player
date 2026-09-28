#include "retro/session.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "gl.h"
#include "gl_state_guard.h"

namespace fs = std::filesystem;

namespace retro {

namespace {
constexpr double kSaveRamCheckSeconds = 5.0;
constexpr int kMaxCatchUpFrames = 4;
}  // namespace

Session::Session() = default;

Session::~Session() {
    // The GL texture belongs to a context that may be gone by now; close()
    // is the orderly way out.
    if (core_) {
        close();
    }
}

bool Session::open(const CoreInfo& info, const std::string& romPath, const std::string& dataDir,
                   const std::string& savesDir, std::string& error) {
    close();
    const std::string root = dataDir + "/retro";
    const std::string actualSavesDir = savesDir.empty() ? root + "/saves" : savesDir;
    const fs::path rom(romPath);
    gameName_ = rom.stem().string();
    romPath_ = romPath;
    coreName_ = info.name;
    subsystemDesc_.clear();
    subsystemGameType_ = 0;
    subsystemContentPaths_.clear();
    savePath_ = actualSavesDir + "/" + gameName_ + ".srm";
    stateBase_ = actualSavesDir + "/" + gameName_ + ".state";

    auto core = std::make_unique<Core>();
    std::string coreFile = fs::path(info.path).stem().string();
    if (!core->load(info, root + "/system", actualSavesDir, root + "/options/" + coreFile + ".cfg", error) ||
        !core->loadGame(romPath, error)) {
        return false;
    }
    return finishOpen(std::move(core), savePath_, error);
}

bool Session::openSubsystem(const CoreInfo& info, const SubsystemInfo& subsystem,
                            const std::vector<std::string>& contentPaths, const std::string& dataDir,
                            const std::string& savesDir, std::string& error) {
    if (contentPaths.empty()) {
        error = "no content given";
        return false;
    }
    close();
    const std::string root = dataDir + "/retro";
    const std::string actualSavesDir = savesDir.empty() ? root + "/saves" : savesDir;
    // Named and saved after the content the player actually picked (slot 0 --
    // the Game Boy ROM in Super Game Boy), not the BIOS/second file.
    const fs::path primary(contentPaths.front());
    gameName_ = primary.stem().string();
    romPath_ = contentPaths.front();
    coreName_ = info.name;
    subsystemDesc_ = subsystem.desc;
    subsystemGameType_ = subsystem.gameType;
    subsystemContentPaths_ = contentPaths;
    savePath_ = actualSavesDir + "/" + gameName_ + ".srm";
    stateBase_ = actualSavesDir + "/" + gameName_ + ".state";

    auto core = std::make_unique<Core>();
    std::string coreFile = fs::path(info.path).stem().string();
    if (!core->load(info, root + "/system", actualSavesDir, root + "/options/" + coreFile + ".cfg", error) ||
        !core->loadGameSpecial(subsystem.gameType, contentPaths, error)) {
        return false;
    }
    return finishOpen(std::move(core), savePath_, error);
}

bool Session::finishOpen(std::unique_ptr<Core> core, const std::string& savePath, std::string& error) {
    core->loadSaveRam(savePath);

    core_ = std::move(core);
    core_->onAudio = [this](const int16_t* frames, size_t count) { audio_.push(frames, count); };
    audio_.start(core_->sampleRate());
    lastSampleRate_ = core_->sampleRate();

    buttons_ = 0;
    started_ = false;
    wasPaused_ = false;
    owed_ = 0.0;
    averageFrameSeconds_ = 1.0 / 60.0;
    uploadedSerial_ = 0;

    if (core_->wantsHwRender() && !setupHwRender(error)) {
        core_.reset();  // unloads the game and the library
        return false;
    }
    return true;
}

void Session::close() {
    if (core_) {
        flushSaveRam();
        audio_.stop();
        destroyHwRender();  // needs core_ (context_destroy()) and the GL context, both still valid here
        core_.reset();      // unloads the game and the library
    }
    if (texture_ != 0) {
        glDeleteTextures(1, &texture_);
        texture_ = 0;
    }
    textureWidth_ = textureHeight_ = 0;
}

void Session::releaseButtons() {
    buttons_ = 0;
    if (core_) {
        core_->setButtons(0);
    }
}

void Session::setButton(int button, bool down) {
    if (button < 0 || button >= kPadButtonCount) {
        return;
    }
    if (down) {
        buttons_ |= 1u << button;
    } else {
        buttons_ &= ~(1u << button);
    }
    if (core_) {
        core_->setButtons(buttons_);
    }
}

void Session::update(double now, bool paused) {
    if (!core_) {
        return;
    }
    if (paused != wasPaused_) {
        audio_.setPaused(paused);
        wasPaused_ = paused;
        started_ = false;  // the pause is not time the game owes
    }
    if (paused) {
        return;
    }

    double dt = started_ ? now - lastNow_ : 1.0 / core_->fps();
    lastNow_ = now;
    started_ = true;
    if (dt <= 0.0 || dt > 0.25) {
        dt = 1.0 / core_->fps();  // first frame, or a long stall: do not race to catch up
    }
    averageFrameSeconds_ += (dt - averageFrameSeconds_) * 0.1;

    if (core_->sampleRate() != lastSampleRate_) {
        lastSampleRate_ = core_->sampleRate();
        audio_.setInputRate(lastSampleRate_);
    }

    const double period = 1.0 / core_->fps();
    int frames = 0;
    const double ratio = averageFrameSeconds_ / period;
    if (ratio > 0.94 && ratio < 1.06) {
        frames = 1;  // display and core agree to within a few percent
        owed_ = 0.0;
    } else {
        owed_ += dt;
        while (owed_ >= period && frames < kMaxCatchUpFrames) {
            owed_ -= period;
            ++frames;
        }
        if (frames == kMaxCatchUpFrames) {
            owed_ = 0.0;
        }
    }
    if (frames > 0 && core_->wantsHwRender()) {
        // The core changes GL state freely (its own shaders, buffers, blend/
        // depth state, ...) while it renders; nothing here relies on any of
        // it surviving, but our own rendering right after this frame does
        // rely on the *baseline* being sane again -- see GLStateGuard.
        GLStateGuard guard;
        guard.capture();
        for (int i = 0; i < frames; ++i) {
            glBindFramebuffer(GL_FRAMEBUFFER, hwFbo_);
            glViewport(0, 0, hwFboWidth_, hwFboHeight_);
            core_->run();
        }
        guard.restore();
    } else {
        for (int i = 0; i < frames; ++i) {
            core_->run();
        }
    }
    if (frames > 0) {
        uploadFrame();
    }

    if (now - lastSaveRamCheck_ >= kSaveRamCheckSeconds) {
        lastSaveRamCheck_ = now;
        flushSaveRam();
        if (std::getenv("PVM_RETRO_LOG")) {
            std::fprintf(stderr, "[retro] %s %.2f fps core, audio %s, buffer %.0f%%\n", coreName_.c_str(),
                         core_->fps(), audio_.running() ? "on" : "off", audio_.fill() * 100.0f);
        }
    }
}

void Session::uploadFrame() {
    if (core_->wantsHwRender()) {
        return;  // already drawn straight into hwFbo_'s texture -- nothing to upload
    }
    const Frame& frame = core_->frame();
    if (frame.width <= 0 || frame.height <= 0 || frame.serial == uploadedSerial_) {
        return;
    }
    uploadedSerial_ = frame.serial;

    const bool created = texture_ == 0;
    if (created) {
        glGenTextures(1, &texture_);
    }
    glBindTexture(GL_TEXTURE_2D, texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (created || frame.width != textureWidth_ || frame.height != textureHeight_) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, frame.width, frame.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     frame.rgba.data());
        textureWidth_ = frame.width;
        textureHeight_ = frame.height;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        const GLint filter = smooth_ ? GL_LINEAR : GL_NEAREST;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame.width, frame.height, GL_RGBA, GL_UNSIGNED_BYTE,
                        frame.rgba.data());
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Session::setSmooth(bool smooth) {
    smooth_ = smooth;
    const GLint filter = smooth ? GL_LINEAR : GL_NEAREST;
    for (unsigned int tex : {texture_, hwColorTexture_}) {
        if (tex == 0) {
            continue;
        }
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

unsigned int Session::texture() const {
    return (core_ && core_->wantsHwRender()) ? hwColorTexture_ : texture_;
}

int Session::textureStorageWidth() const {
    return (core_ && core_->wantsHwRender()) ? hwFboWidth_ : textureWidth();
}

int Session::textureStorageHeight() const {
    return (core_ && core_->wantsHwRender()) ? hwFboHeight_ : textureHeight();
}

bool Session::needsFlipY() const {
    return !(core_ && core_->wantsHwRender() && core_->hwRenderInfo().bottomLeftOrigin);
}

bool Session::setupHwRender(std::string& error) {
    if (!getProcAddress_) {
        error = "this core needs OpenGL, but no OpenGL loader was set up";
        return false;
    }
    core_->setProcAddressResolver(getProcAddress_);

    hwFboWidth_ = static_cast<int>(std::max(core_->maxWidth(), 1u));
    hwFboHeight_ = static_cast<int>(std::max(core_->maxHeight(), 1u));

    glGenFramebuffers(1, &hwFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, hwFbo_);

    glGenTextures(1, &hwColorTexture_);
    glBindTexture(GL_TEXTURE_2D, hwColorTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, hwFboWidth_, hwFboHeight_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    const GLint filter = smooth_ ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, hwColorTexture_, 0);

    // "Only attaching stencil is invalid and will be ignored" (libretro.h):
    // depth-only or depth+stencil are the only combinations that make sense.
    const HwRenderInfo& hw = core_->hwRenderInfo();
    if (hw.depth) {
        glGenRenderbuffers(1, &hwDepthStencilRbo_);
        glBindRenderbuffer(GL_RENDERBUFFER, hwDepthStencilRbo_);
        const GLenum format = hw.stencil ? GL_DEPTH24_STENCIL8 : GL_DEPTH_COMPONENT24;
        glRenderbufferStorage(GL_RENDERBUFFER, format, hwFboWidth_, hwFboHeight_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, hwDepthStencilRbo_);
        if (hw.stencil) {
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, hwDepthStencilRbo_);
        }
    }

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        char message[64];
        std::snprintf(message, sizeof(message), "incomplete framebuffer for hardware rendering (0x%x)", status);
        error = message;
        destroyHwRender();
        return false;
    }

    core_->setHwFramebuffer(hwFbo_);
    core_->hwContextReset();
    return true;
}

void Session::destroyHwRender() {
    if (hwFbo_ == 0) {
        return;  // this core never asked for hardware rendering
    }
    if (core_) {
        core_->hwContextDestroy();
    }
    if (hwDepthStencilRbo_ != 0) {
        glDeleteRenderbuffers(1, &hwDepthStencilRbo_);
        hwDepthStencilRbo_ = 0;
    }
    glDeleteTextures(1, &hwColorTexture_);
    hwColorTexture_ = 0;
    glDeleteFramebuffers(1, &hwFbo_);
    hwFbo_ = 0;
    hwFboWidth_ = hwFboHeight_ = 0;
}

void Session::reset() {
    if (core_) {
        core_->reset();
    }
}

std::string Session::statePath(int slot) const {
    if (slot == kRecoverySlot) {
        return stateBase_ + "recovery";
    }
    return slot <= 0 ? stateBase_ : stateBase_ + std::to_string(slot);
}

bool Session::saveState(int slot) {
    return core_ && core_->saveState(statePath(slot));
}

bool Session::loadState(int slot) {
    return core_ && core_->loadState(statePath(slot));
}

void Session::flushSaveRam() {
    if (core_ && !core_->flushSaveRam(savePath_)) {
        std::fprintf(stderr, "[retro] could not write %s\n", savePath_.c_str());
    }
}

}  // namespace retro
