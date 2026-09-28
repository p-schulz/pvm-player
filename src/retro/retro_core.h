#pragma once

// A libretro core (an emulator built as a shared library) loaded at run time.
// This is the frontend half of the libretro API: it loads the library, answers
// the core's environment queries, receives its video and audio and hands it
// input. Everything runs on the calling thread; callbacks fire from inside
// run(). Cores render either in software (an RGBA picture read back from
// `frame()` and uploaded as a texture, see Session::uploadFrame()) or, if
// they ask for OpenGL/GLES (see HwRenderInfo), directly into an FBO the
// caller owns and hands over via setHwFramebuffer() -- Vulkan/D3D cores are
// refused (RETRO_ENVIRONMENT_SET_HW_RENDER returns false for them).
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

// One save-memory region a subsystem rom slot exposes (RETRO_ENVIRONMENT_
// SET_SUBSYSTEM_INFO): `type` is the id to pass to retro_get_memory_data/
// _size() once the game is running -- a core-defined constant, not
// necessarily RETRO_MEMORY_SAVE_RAM (bsnes's Super Game Boy slot uses its
// own id for the Game Boy side's battery RAM, for instance).
struct SubsystemMemory {
    std::string extension;  // "srm" -- the file extension to save it under
    unsigned type = 0;
};

// One piece of content a subsystem needs (e.g. Super Game Boy needs both a
// Game Boy ROM and a separate Super Game Boy BIOS/boot ROM).
struct SubsystemRomSlot {
    std::string desc;                     // "Game Boy ROM"
    std::vector<std::string> extensions;  // ".gb", ".gbc"
    bool needFullpath = true;
    bool required = true;
    std::vector<SubsystemMemory> memory;  // usually zero or one entry
};

// A secondary platform/mode a core supports loading through
// retro_load_game_special() instead of the normal single-ROM
// retro_load_game() -- e.g. bsnes's "Super Game Boy" and "BS-X Satellaview".
struct SubsystemInfo {
    std::string desc;   // "Super Game Boy"
    std::string ident;  // "sgb"
    unsigned gameType = 0;  // passed to retro_load_game_special()
    std::vector<SubsystemRomSlot> roms;
};

// What a core says about itself; readable without initialising it (loading
// it only far enough to ask, via retro_set_environment(), never
// retro_init()).
struct CoreInfo {
    std::string path;                     // the shared library
    std::string name;                     // "Gambatte"
    std::string version;
    std::vector<std::string> extensions;  // ".gb", ".gbc" (lowercase, with the dot)
    bool needFullpath = false;            // wants a file path, not the ROM's bytes
    std::vector<SubsystemInfo> subsystems;
};

// Reads the identity of the library at `path` (loads and unloads it; the core
// is not initialised, though retro_set_environment() is called just long
// enough to collect `subsystems`). False if it is not a libretro core.
bool probeCore(const std::string& path, CoreInfo& info);

// Cores found in `dirs`: shared libraries with "_libretro" in the name,
// sorted by name. A directory that does not exist is skipped.
std::vector<CoreInfo> scanCores(const std::vector<std::string>& dirs);

// A subsystem (of any scanned core) whose first rom slot accepts `romPath`'s
// extension as its primary content -- e.g. picks bsnes's Super Game Boy
// subsystem for a .gb file. Null if no scanned core offers one. `core` is set
// to the core it belongs to.
struct SubsystemMatch {
    const CoreInfo* core = nullptr;
    const SubsystemInfo* subsystem = nullptr;
};
SubsystemMatch pickSubsystem(const std::vector<CoreInfo>& cores, const std::string& romPath);

// Re-finds a specific subsystem by core name + gameType (see SubsystemInfo::
// gameType) -- used to relaunch a subsystem game after an Android GL context
// loss, where only those two things survive (see App::PlaybackSnapshot).
SubsystemMatch findSubsystem(const std::vector<CoreInfo>& cores, const std::string& coreName, unsigned gameType);

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

// What a core asked for via RETRO_ENVIRONMENT_SET_HW_RENDER. `type == None`
// (the default) means it never asked -- a plain software-rendered core.
enum class HwContextType { None, OpenGL, OpenGLCore, OpenGLES2, OpenGLES3 };
struct HwRenderInfo {
    HwContextType type = HwContextType::None;
    unsigned versionMajor = 0;
    unsigned versionMinor = 0;
    bool depth = false;     // wants a depth (or depth+stencil) buffer attached
    bool stencil = false;   // only meaningful together with depth -- see libretro.h
    // How to read back the FBO texture: true is normal GL convention (row 0
    // at the bottom, no flip needed to display it); false is what libretro
    // calls "top-left" semantics (needs the same Y-flip as a software
    // frame). Real GL cores overwhelmingly set this true.
    bool bottomLeftOrigin = true;
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
    // Multi-content ROM (a subsystem, see SubsystemInfo): `gameType` is the
    // subsystem's id, `contentPaths` one path per rom slot, in slot order.
    // The first path's memory (if the subsystem declares one for that slot)
    // becomes what loadSaveRam()/flushSaveRam() read and write.
    bool loadGameSpecial(unsigned gameType, const std::vector<std::string>& contentPaths, std::string& error);
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

    // --- Hardware (OpenGL/GLES) rendering ---------------------------------
    // Set once load() has run (from RETRO_ENVIRONMENT_SET_HW_RENDER); {None,
    // ...} if the core never asked for one.
    const HwRenderInfo& hwRenderInfo() const { return hwRender_; }
    bool wantsHwRender() const { return hwRender_.type != HwContextType::None; }
    unsigned maxWidth() const { return maxWidth_; }
    unsigned maxHeight() const { return maxHeight_; }

    // The frontend's own OpenGL loader (glad's glfwGetProcAddress on
    // desktop, eglGetProcAddress on Android) -- answers the core's
    // get_proc_address(). The caller (Session) must set this before calling
    // hwContextReset(); load() itself does not need it.
    using ProcAddressResolver = std::function<void*(const char*)>;
    void setProcAddressResolver(ProcAddressResolver resolver) { getProcAddress_ = std::move(resolver); }

    // The FBO id the core's get_current_framebuffer() should answer with.
    // The caller (Session) owns the actual framebuffer object; this just
    // tells the core which one to use.
    void setHwFramebuffer(unsigned fbo) { hwFramebuffer_ = fbo; }

    // Tells the core its GL resources are (still) valid and it may create
    // its own -- once after the game loads (with the FBO already set), and
    // again any time the context is rebuilt from scratch (a fresh Core, so
    // there is nothing to destroy() first -- see libretro.h's own note that
    // context_reset without a preceding context_destroy means exactly that).
    void hwContextReset();
    // Tells the core to release its own GL resources while the context is
    // still valid (a no-op if it declared no context_destroy) -- called
    // once, from Session::close(), before the FBO/textures themselves are
    // deleted.
    void hwContextDestroy();

private:
    friend struct Bridge;
    void finishLoad();

    Api* api_ = nullptr;
    void* library_ = nullptr;
    CoreInfo info_;
    bool gameLoaded_ = false;
    // The retro_get_memory_data/_size() id for save RAM: RETRO_MEMORY_SAVE_RAM
    // for a plain loadGame(), or a subsystem-specific id after loadGameSpecial()
    // (see SubsystemMemory::type).
    unsigned saveMemoryId_ = 0;  // set to RETRO_MEMORY_SAVE_RAM in the .cpp (avoids the libretro.h include here)
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

    unsigned maxWidth_ = 0;
    unsigned maxHeight_ = 0;
    HwRenderInfo hwRender_;
    ProcAddressResolver getProcAddress_;
    unsigned hwFramebuffer_ = 0;
    // The core's own callbacks from RETRO_ENVIRONMENT_SET_HW_RENDER, called
    // by hwContextReset()/hwContextDestroy(). Either may be null.
    void (*hwContextResetFn_)() = nullptr;
    void (*hwContextDestroyFn_)() = nullptr;
};

}  // namespace retro
