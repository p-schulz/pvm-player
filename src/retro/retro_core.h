#pragma once

// A libretro core (an emulator built as a shared library) loaded at run time.
// This is the frontend half of the libretro API: it loads the library, answers
// the core's environment queries, receives its video and audio and hands it
// input. Software-rendered cores only -- a core that asks for a hardware
// (OpenGL/Vulkan) context is refused. Everything runs on the calling thread;
// callbacks fire from inside run().
//
// The libretro API has no user-data pointer, so the callbacks reach the
// active Core through a process-wide pointer: one Core at a time.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace retro {

// What a core says about itself; readable without initialising it.
struct CoreInfo {
    std::string path;                     // the shared library
    std::string name;                     // "Gambatte"
    std::string version;
    std::vector<std::string> extensions;  // ".gb", ".gbc" (lowercase, with the dot)
    bool needFullpath = false;            // wants a file path, not the ROM's bytes
};

// Reads the identity of the library at `path` (loads and unloads it; the core
// is not initialised). False if it is not a libretro core.
bool probeCore(const std::string& path, CoreInfo& info);

// Cores found in `dirs`: shared libraries with "_libretro" in the name,
// sorted by name. A directory that does not exist is skipped.
std::vector<CoreInfo> scanCores(const std::vector<std::string>& dirs);

// The core to run `romPath` with: of those that list its extension, the one
// with the fewest extensions (the specialist -- Gambatte over bsnes for
// .gb, since bsnes only runs Game Boy games through its Super Game Boy
// support). Null if none does.
const CoreInfo* pickCore(const std::vector<CoreInfo>& cores, const std::string& romPath);

// The extensions any of `cores` handles, for the file browser.
std::vector<std::string> allExtensions(const std::vector<CoreInfo>& cores);

// One video frame, converted to RGBA8 (bytes R, G, B, A in memory).
struct Frame {
    int width = 0;
    int height = 0;
    std::vector<uint32_t> rgba;
    uint64_t serial = 0;  // increases with every new picture
};

// libretro's RETRO_DEVICE_ID_JOYPAD_* numbering.
enum PadButton : int {
    kPadB = 0,
    kPadY,
    kPadSelect,
    kPadStart,
    kPadUp,
    kPadDown,
    kPadLeft,
    kPadRight,
    kPadA,
    kPadX,
    kPadL,
    kPadR,
    kPadL2,
    kPadR2,
    kPadL3,
    kPadR3,
    kPadButtonCount
};

class Core {
public:
    struct Api;  // the core's entry points (retro_core.cpp)

    Core();
    ~Core();
    Core(const Core&) = delete;
    Core& operator=(const Core&) = delete;

    // Loads the library and initialises it. `systemDir` is where cores look
    // for BIOS files, `saveDir` where they may keep their own files. Options
    // the core declares start at their defaults, then any "key = value" lines
    // of `optionsFile` (if it exists) override them.
    bool load(const CoreInfo& info, const std::string& systemDir, const std::string& saveDir,
              const std::string& optionsFile, std::string& error);
    bool loadGame(const std::string& romPath, std::string& error);
    void unloadGame();

    // Runs one frame: input is read, video and audio come back through the
    // callbacks below.
    void run();
    void reset();

    void setButtons(uint32_t mask) { buttons_ = mask; }

    // Called from inside run() with interleaved 16-bit stereo frames at
    // sampleRate().
    std::function<void(const int16_t* frames, size_t count)> onAudio;

    const Frame& frame() const { return frame_; }
    double fps() const { return fps_; }
    double sampleRate() const { return sampleRate_; }
    // Width / height the picture should be shown at (pixel aspect included).
    float aspectRatio() const;
    // The core asked to close the game (RETRO_ENVIRONMENT_SHUTDOWN); reading
    // clears the request.
    bool takeShutdownRequest();
    const CoreInfo& info() const { return info_; }
    bool gameLoaded() const { return gameLoaded_; }

    // Battery-backed memory ("SRAM"): read from / written to `path`.
    void loadSaveRam(const std::string& path);
    // Writes the save RAM if it differs from what was last read or written.
    // Returns false only when a write was needed and failed.
    bool flushSaveRam(const std::string& path);

    // Whole-machine snapshots.
    bool saveState(const std::string& path);
    bool loadState(const std::string& path);

private:
    friend struct Bridge;

    Api* api_ = nullptr;
    void* library_ = nullptr;
    CoreInfo info_;
    bool gameLoaded_ = false;
    bool initialised_ = false;

    std::string systemDir_;
    std::string saveDir_;
    std::map<std::string, std::string> variables_;

    int pixelFormat_ = 0;  // RETRO_PIXEL_FORMAT_0RGB1555, libretro's default
    Frame frame_;
    uint32_t buttons_ = 0;
    double fps_ = 60.0;
    double sampleRate_ = 44100.0;
    unsigned baseWidth_ = 0;
    unsigned baseHeight_ = 0;
    float geometryAspect_ = 0.0f;
    bool shutdownRequested_ = false;
    std::vector<uint8_t> lastSaveRam_;
};

}  // namespace retro
