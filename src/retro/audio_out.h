#pragma once

// Audio output for a running core. The core produces samples in bursts, once
// per emulated frame, at its own rate (32040 Hz, 44100 Hz, ...); the sound
// device consumes them steadily at its own. push() resamples into a ring
// buffer the device callback drains, and nudges the resampling ratio by a
// fraction of a percent to keep the buffer about half full -- the standard
// "dynamic rate control" that lets video run in step with the display rather
// than needing the audio clock to drive everything.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace retro {

class AudioOut {
public:
    AudioOut();
    ~AudioOut();
    AudioOut(const AudioOut&) = delete;
    AudioOut& operator=(const AudioOut&) = delete;

    // Opens the default playback device. False if there is none; the session
    // then runs silently. `inputRate` is the core's sample rate.
    bool start(double inputRate);
    void stop();
    bool running() const { return running_; }

    // The core's rate changed (RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO).
    void setInputRate(double inputRate) { inputRate_ = inputRate; }

    // Interleaved stereo 16-bit frames at the input rate.
    void push(const int16_t* frames, size_t count);

    // While paused the device plays silence and pushed audio is dropped.
    void setPaused(bool paused);

    // Fraction of the ring buffer in use, 0..1 (tests and logs).
    float fill() const;

private:
    struct Impl;
    Impl* impl_ = nullptr;

    void drain(int16_t* out, size_t frames);  // device thread

    bool running_ = false;
    double inputRate_ = 44100.0;
    double deviceRate_ = 48000.0;
    double phase_ = 0.0;  // position between prev_ and the next input frame, 0..1
    int16_t prev_[2] = {0, 0};

    std::vector<int16_t> ring_;  // interleaved stereo
    size_t capacityFrames_ = 0;
    std::atomic<size_t> readPos_{0};   // frame counters, wrapped by capacityFrames_ on use
    std::atomic<size_t> writePos_{0};
    std::atomic<bool> paused_{false};
    std::atomic<bool> flush_{false};
};

}  // namespace retro
