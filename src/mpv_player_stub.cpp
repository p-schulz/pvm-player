// MpvPlayer without libmpv, for builds that don't have it yet (the Android
// skeleton). Same interface as mpv_player.cpp, but nothing ever loads or
// renders: init() succeeds so the rest of the app comes up, and loadFile()
// reports failure so the UI simply stays where it is. Replaced by the real
// player once libmpv is packaged for the target.

#include <cstdio>

#include "mpv_player.h"

MpvPlayer::MpvPlayer() = default;

MpvPlayer::~MpvPlayer() = default;

bool MpvPlayer::init(const Platform& /*platform*/) {
    std::fprintf(stderr, "libmpv is not available in this build; playback is disabled\n");
    return true;
}

void MpvPlayer::shutdown() {}

bool MpvPlayer::loadFile(const std::string& path, double /*startSeconds*/) {
    std::fprintf(stderr, "Cannot play %s: libmpv is not available in this build\n", path.c_str());
    return false;
}

void MpvPlayer::pollEvents() {}

unsigned int MpvPlayer::render(int /*width*/, int /*height*/) {
    return 0;
}

void MpvPlayer::togglePause() {}

void MpvPlayer::setPaused(bool /*paused*/) {}

void MpvPlayer::setHardwareDecoding(bool /*enabled*/) {}

void MpvPlayer::watchForStall() {}

bool MpvPlayer::consumeHardwareDecodingFallback() {
    return false;
}

std::string MpvPlayer::statsLine() const {
    return {};
}

void MpvPlayer::seekRelative(double /*seconds*/) {}

void MpvPlayer::stop() {}

void MpvPlayer::setVolume(double /*volumePercent*/) {}

void MpvPlayer::setAspectOverride(const std::string& /*ratio*/) {}

bool MpvPlayer::videoDisplaySize(int& /*width*/, int& /*height*/) const {
    return false;
}

bool MpvPlayer::consumeEndOfFile() {
    return false;
}

void MpvPlayer::ensureFbo(int /*width*/, int /*height*/) {}

void MpvPlayer::destroyFbo() {}

void MpvPlayer::onRenderUpdate(void* /*ctx*/) {}
