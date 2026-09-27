#include "retro/session.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "gl.h"

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
                   std::string& error) {
    close();
    const std::string root = dataDir + "/retro";
    const std::string savesDir = root + "/saves";
    const fs::path rom(romPath);
    gameName_ = rom.stem().string();
    romPath_ = romPath;
    coreName_ = info.name;
    savePath_ = savesDir + "/" + gameName_ + ".srm";
    stateBase_ = savesDir + "/" + gameName_ + ".state";

    auto core = std::make_unique<Core>();
    std::string coreFile = fs::path(info.path).stem().string();
    if (!core->load(info, root + "/system", savesDir, root + "/options/" + coreFile + ".cfg", error) ||
        !core->loadGame(romPath, error)) {
        return false;
    }
    core->loadSaveRam(savePath_);

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
    return true;
}

void Session::close() {
    if (core_) {
        flushSaveRam();
        audio_.stop();
        core_.reset();  // unloads the game and the library
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
    for (int i = 0; i < frames; ++i) {
        core_->run();
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
    if (texture_ != 0) {
        glBindTexture(GL_TEXTURE_2D, texture_);
        const GLint filter = smooth ? GL_LINEAR : GL_NEAREST;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
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
