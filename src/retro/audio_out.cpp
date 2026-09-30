#include "retro/audio_out.h"

#include <algorithm>
#include <cstdio>

// miniaudio.h pulls in windows.h on Windows; without this its min/max macros
// shadow std::min used below (drain()), breaking it at the call site.
#if defined(_WIN32)
#define NOMINMAX
#endif

#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

namespace retro {

namespace {
constexpr unsigned kDeviceRate = 48000;
constexpr size_t kCapacityFrames = 9600;         // 200 ms
constexpr double kTargetFill = 0.25;             // 50 ms of audio queued
constexpr double kMaxRateAdjust = 0.01;          // +-1 % of the resampling ratio
}  // namespace

struct AudioOut::Impl {
    ma_device device{};
    bool deviceInitialised = false;
};

AudioOut::AudioOut() = default;

AudioOut::~AudioOut() {
    stop();
}

bool AudioOut::start(double inputRate) {
    stop();
    inputRate_ = inputRate;
    ring_.assign(kCapacityFrames * 2, 0);
    capacityFrames_ = kCapacityFrames;
    readPos_ = 0;
    writePos_ = 0;
    paused_ = false;
    flush_ = false;
    phase_ = 0.0;
    prev_[0] = prev_[1] = 0;

    impl_ = new Impl();
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_s16;
    config.playback.channels = 2;
    config.sampleRate = kDeviceRate;
    config.periodSizeInFrames = 512;
    config.pUserData = this;
    config.dataCallback = [](ma_device* device, void* output, const void* /*input*/, ma_uint32 frames) {
        static_cast<AudioOut*>(device->pUserData)->drain(static_cast<int16_t*>(output), frames);
    };

    if (ma_device_init(nullptr, &config, &impl_->device) != MA_SUCCESS) {
        std::fprintf(stderr, "[retro] no audio device; running silently\n");
        delete impl_;
        impl_ = nullptr;
        return false;
    }
    impl_->deviceInitialised = true;
    deviceRate_ = impl_->device.sampleRate > 0 ? impl_->device.sampleRate : kDeviceRate;
    if (ma_device_start(&impl_->device) != MA_SUCCESS) {
        std::fprintf(stderr, "[retro] audio device would not start; running silently\n");
        stop();
        return false;
    }
    running_ = true;
    return true;
}

void AudioOut::stop() {
    running_ = false;
    if (impl_) {
        if (impl_->deviceInitialised) {
            ma_device_uninit(&impl_->device);  // waits for the callback to finish
        }
        delete impl_;
        impl_ = nullptr;
    }
}

float AudioOut::fill() const {
    if (capacityFrames_ == 0) {
        return 0.0f;
    }
    return static_cast<float>(writePos_.load() - readPos_.load()) / static_cast<float>(capacityFrames_);
}

void AudioOut::setPaused(bool paused) {
    if (paused_.exchange(paused) && !paused) {
        flush_ = true;  // resume from silence, not from stale audio
    }
}

void AudioOut::push(const int16_t* frames, size_t count) {
    if (!running_ || paused_.load() || count == 0) {
        return;
    }
    const size_t queued = writePos_.load() - readPos_.load();
    const double fillFraction = static_cast<double>(queued) / static_cast<double>(capacityFrames_);
    // Below target: stretch (more output samples per input); above: squeeze.
    const double adjust =
        std::clamp((kTargetFill - fillFraction) / kTargetFill * kMaxRateAdjust, -kMaxRateAdjust, kMaxRateAdjust);
    const double step = (inputRate_ / deviceRate_) / (1.0 + adjust);

    size_t write = writePos_.load(std::memory_order_relaxed);
    const size_t read = readPos_.load(std::memory_order_acquire);
    for (size_t i = 0; i < count; ++i) {
        const int16_t cur[2] = {frames[i * 2], frames[i * 2 + 1]};
        while (phase_ < 1.0) {
            if (write - read >= capacityFrames_) {
                phase_ = 1.0;  // full: drop the rest of this burst
                break;
            }
            const size_t slot = (write % capacityFrames_) * 2;
            ring_[slot] = static_cast<int16_t>(prev_[0] + (cur[0] - prev_[0]) * phase_);
            ring_[slot + 1] = static_cast<int16_t>(prev_[1] + (cur[1] - prev_[1]) * phase_);
            ++write;
            phase_ += step;
        }
        phase_ -= 1.0;
        prev_[0] = cur[0];
        prev_[1] = cur[1];
    }
    writePos_.store(write, std::memory_order_release);
}

void AudioOut::drain(int16_t* out, size_t frames) {
    size_t read = readPos_.load(std::memory_order_relaxed);
    const size_t write = writePos_.load(std::memory_order_acquire);
    if (paused_.load() || flush_.exchange(false)) {
        readPos_.store(write, std::memory_order_release);
        std::fill(out, out + frames * 2, int16_t{0});
        return;
    }
    const size_t available = write - read;
    const size_t take = std::min(frames, available);
    for (size_t i = 0; i < take; ++i, ++read) {
        const size_t slot = (read % capacityFrames_) * 2;
        out[i * 2] = ring_[slot];
        out[i * 2 + 1] = ring_[slot + 1];
    }
    std::fill(out + take * 2, out + frames * 2, int16_t{0});  // underrun: silence
    readPos_.store(read, std::memory_order_release);
}

}  // namespace retro
