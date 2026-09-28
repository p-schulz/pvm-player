#include "retro/retro_core.h"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "libretro.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace fs = std::filesystem;

namespace retro {

namespace {

// --- Shared libraries ------------------------------------------------------

void* openLibrary(const std::string& path) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(LoadLibraryA(path.c_str()));
#else
    return dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
#endif
}

void closeLibrary(void* library) {
    if (!library) {
        return;
    }
#if defined(_WIN32)
    FreeLibrary(reinterpret_cast<HMODULE>(library));
#else
    dlclose(library);
#endif
}

void* findSymbol(void* library, const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(library), name));
#else
    return dlsym(library, name);
#endif
}

std::string libraryError() {
#if defined(_WIN32)
    return "LoadLibrary failed (error " + std::to_string(GetLastError()) + ")";
#else
    const char* message = dlerror();
    return message ? message : "dlopen failed";
#endif
}

bool hasSharedLibrarySuffix(const std::string& name) {
#if defined(_WIN32)
    return name.size() > 4 && name.compare(name.size() - 4, 4, ".dll") == 0;
#elif defined(__APPLE__)
    return name.size() > 6 && name.compare(name.size() - 6, 6, ".dylib") == 0;
#else
    return name.size() > 3 && name.compare(name.size() - 3, 3, ".so") == 0;
#endif
}

std::string lowered(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string extensionOf(const std::string& path) {
    const std::string ext = fs::path(path).extension().string();
    return lowered(ext);
}

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0) {
        return false;
    }
    in.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    if (size > 0) {
        in.read(reinterpret_cast<char*>(out.data()), size);
    }
    return static_cast<bool>(in) || in.eof();
}

// Written beside the target and renamed over it, so a crash mid-write cannot
// leave a half-written save.
bool writeFileAtomic(const std::string& path, const uint8_t* data, size_t size) {
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    const std::string temp = path + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        if (size > 0) {
            out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
        }
        if (!out) {
            return false;
        }
    }
    fs::rename(temp, path, ec);
    if (ec) {
        fs::remove(temp, ec);
        return false;
    }
    return true;
}

bool verboseCoreLog() {
    static const bool on = std::getenv("PVM_RETRO_LOG") != nullptr;
    return on;
}

}  // namespace

// The entry points every core exports.
struct Core::Api {
    void (*set_environment)(retro_environment_t) = nullptr;
    void (*set_video_refresh)(retro_video_refresh_t) = nullptr;
    void (*set_audio_sample)(retro_audio_sample_t) = nullptr;
    void (*set_audio_sample_batch)(retro_audio_sample_batch_t) = nullptr;
    void (*set_input_poll)(retro_input_poll_t) = nullptr;
    void (*set_input_state)(retro_input_state_t) = nullptr;
    void (*init)() = nullptr;
    void (*deinit)() = nullptr;
    unsigned (*api_version)() = nullptr;
    void (*get_system_info)(retro_system_info*) = nullptr;
    void (*get_system_av_info)(retro_system_av_info*) = nullptr;
    void (*set_controller_port_device)(unsigned, unsigned) = nullptr;
    void (*reset)() = nullptr;
    void (*run)() = nullptr;
    size_t (*serialize_size)() = nullptr;
    bool (*serialize)(void*, size_t) = nullptr;
    bool (*unserialize)(const void*, size_t) = nullptr;
    bool (*load_game)(const retro_game_info*) = nullptr;
    bool (*load_game_special)(unsigned, const retro_game_info*, size_t) = nullptr;
    void (*unload_game)() = nullptr;
    void* (*get_memory_data)(unsigned) = nullptr;
    size_t (*get_memory_size)(unsigned) = nullptr;
};

namespace {

// Resolves the entry points; returns the name of the first missing one.
const char* resolveApi(void* library, Core::Api& api) {
    struct Entry {
        const char* name;
        void** slot;
    };
    const Entry entries[] = {
        {"retro_set_environment", reinterpret_cast<void**>(&api.set_environment)},
        {"retro_set_video_refresh", reinterpret_cast<void**>(&api.set_video_refresh)},
        {"retro_set_audio_sample", reinterpret_cast<void**>(&api.set_audio_sample)},
        {"retro_set_audio_sample_batch", reinterpret_cast<void**>(&api.set_audio_sample_batch)},
        {"retro_set_input_poll", reinterpret_cast<void**>(&api.set_input_poll)},
        {"retro_set_input_state", reinterpret_cast<void**>(&api.set_input_state)},
        {"retro_init", reinterpret_cast<void**>(&api.init)},
        {"retro_deinit", reinterpret_cast<void**>(&api.deinit)},
        {"retro_api_version", reinterpret_cast<void**>(&api.api_version)},
        {"retro_get_system_info", reinterpret_cast<void**>(&api.get_system_info)},
        {"retro_get_system_av_info", reinterpret_cast<void**>(&api.get_system_av_info)},
        {"retro_set_controller_port_device", reinterpret_cast<void**>(&api.set_controller_port_device)},
        {"retro_reset", reinterpret_cast<void**>(&api.reset)},
        {"retro_run", reinterpret_cast<void**>(&api.run)},
        {"retro_serialize_size", reinterpret_cast<void**>(&api.serialize_size)},
        {"retro_serialize", reinterpret_cast<void**>(&api.serialize)},
        {"retro_unserialize", reinterpret_cast<void**>(&api.unserialize)},
        {"retro_load_game", reinterpret_cast<void**>(&api.load_game)},
        {"retro_load_game_special", reinterpret_cast<void**>(&api.load_game_special)},
        {"retro_unload_game", reinterpret_cast<void**>(&api.unload_game)},
        {"retro_get_memory_data", reinterpret_cast<void**>(&api.get_memory_data)},
        {"retro_get_memory_size", reinterpret_cast<void**>(&api.get_memory_size)},
    };
    for (const Entry& entry : entries) {
        *entry.slot = findSymbol(library, entry.name);
        if (!*entry.slot) {
            return entry.name;
        }
    }
    return nullptr;
}

void fillInfo(const retro_system_info& raw, CoreInfo& info) {
    info.name = raw.library_name ? raw.library_name : "";
    info.version = raw.library_version ? raw.library_version : "";
    info.needFullpath = raw.need_fullpath;
    info.extensions.clear();
    if (raw.valid_extensions) {
        std::stringstream list(raw.valid_extensions);
        std::string ext;
        while (std::getline(list, ext, '|')) {
            if (!ext.empty()) {
                info.extensions.push_back("." + lowered(ext));
            }
        }
    }
}

}  // namespace

namespace {

// Set only for the duration of probeCore()'s retro_set_environment() call
// below, so its answer for RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO has somewhere
// to write -- entirely separate from the real session's g_active (a core is
// never both probed and running at once; probing never calls retro_init()).
std::vector<SubsystemInfo>* g_probeSubsystems = nullptr;

std::vector<std::string> splitExtensions(const char* pipeSeparated) {
    std::vector<std::string> out;
    if (!pipeSeparated) {
        return out;
    }
    std::stringstream list(pipeSeparated);
    std::string ext;
    while (std::getline(list, ext, '|')) {
        if (!ext.empty()) {
            out.push_back("." + lowered(ext));
        }
    }
    return out;
}

// Answers just enough of the environment API for a core's retro_set_
// environment() to hand us RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO (the only
// thing probeCore() wants); everything else politely declines, since no
// core is required to tolerate an environment callback that supports
// nothing -- "not supported" (false) is always a safe answer here.
bool probeEnvironment(unsigned cmd, void* data) {
    cmd &= ~(RETRO_ENVIRONMENT_EXPERIMENTAL | RETRO_ENVIRONMENT_PRIVATE);
    if (cmd != RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO || !g_probeSubsystems) {
        return false;
    }
    for (const auto* sub = static_cast<const retro_subsystem_info*>(data); sub && sub->desc; ++sub) {
        SubsystemInfo info;
        info.desc = sub->desc;
        info.ident = sub->ident ? sub->ident : "";
        info.gameType = sub->id;
        for (unsigned i = 0; i < sub->num_roms; ++i) {
            const retro_subsystem_rom_info& rom = sub->roms[i];
            SubsystemRomSlot slot;
            slot.desc = rom.desc ? rom.desc : "";
            slot.extensions = splitExtensions(rom.valid_extensions);
            slot.needFullpath = rom.need_fullpath;
            slot.required = rom.required;
            for (unsigned m = 0; m < rom.num_memory; ++m) {
                slot.memory.push_back({rom.memory[m].extension ? rom.memory[m].extension : "", rom.memory[m].type});
            }
            info.roms.push_back(std::move(slot));
        }
        g_probeSubsystems->push_back(std::move(info));
    }
    return true;
}

}  // namespace

bool probeCore(const std::string& path, CoreInfo& info) {
    void* library = openLibrary(path);
    if (!library) {
        return false;
    }
    using GetSystemInfo = void (*)(retro_system_info*);
    using ApiVersion = unsigned (*)();
    using SetEnvironment = void (*)(retro_environment_t);
    auto getInfo = reinterpret_cast<GetSystemInfo>(findSymbol(library, "retro_get_system_info"));
    auto version = reinterpret_cast<ApiVersion>(findSymbol(library, "retro_api_version"));
    auto setEnvironment = reinterpret_cast<SetEnvironment>(findSymbol(library, "retro_set_environment"));
    bool ok = false;
    if (getInfo && version && version() == RETRO_API_VERSION) {
        retro_system_info raw{};
        getInfo(&raw);
        info = CoreInfo{};
        info.path = path;
        fillInfo(raw, info);
        if (setEnvironment) {
            g_probeSubsystems = &info.subsystems;
            setEnvironment(&probeEnvironment);
            g_probeSubsystems = nullptr;
        }
        ok = true;
    }
    closeLibrary(library);
    return ok;
}

std::vector<CoreInfo> scanCores(const std::vector<std::string>& dirs) {
    std::vector<CoreInfo> cores;
    for (const std::string& dir : dirs) {
        std::error_code ec;
        if (dir.empty() || !fs::is_directory(dir, ec)) {
            continue;
        }
        for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec)) {
            const std::string name = entry.path().filename().string();
            if (name.find("_libretro") == std::string::npos || !hasSharedLibrarySuffix(name)) {
                continue;
            }
            CoreInfo info;
            if (!probeCore(entry.path().string(), info)) {
                std::fprintf(stderr, "[retro] %s is not a usable libretro core\n", name.c_str());
                continue;
            }
            // The same core in two search directories: the first one wins.
            const bool known = std::any_of(cores.begin(), cores.end(), [&](const CoreInfo& other) {
                return fs::path(other.path).filename() == entry.path().filename();
            });
            if (!known) {
                cores.push_back(std::move(info));
            }
        }
    }
    std::sort(cores.begin(), cores.end(), [](const CoreInfo& a, const CoreInfo& b) { return a.name < b.name; });
    return cores;
}

const CoreInfo* pickCore(const std::vector<CoreInfo>& cores, const std::string& romPath) {
    const std::string ext = extensionOf(romPath);
    const CoreInfo* best = nullptr;
    for (const CoreInfo& core : cores) {
        if (std::find(core.extensions.begin(), core.extensions.end(), ext) == core.extensions.end()) {
            continue;
        }
        if (!best || core.extensions.size() < best->extensions.size()) {
            best = &core;
        }
    }
    return best;
}

SubsystemMatch pickSubsystem(const std::vector<CoreInfo>& cores, const std::string& romPath) {
    const std::string ext = extensionOf(romPath);
    for (const CoreInfo& core : cores) {
        for (const SubsystemInfo& subsystem : core.subsystems) {
            // The first rom slot is the "main" content -- the one the file
            // browser's selection stands for (e.g. the Game Boy ROM in
            // bsnes's Super Game Boy subsystem; the rest, like the SGB BIOS,
            // are asked for afterwards).
            if (!subsystem.roms.empty() &&
                std::find(subsystem.roms.front().extensions.begin(), subsystem.roms.front().extensions.end(), ext) !=
                    subsystem.roms.front().extensions.end()) {
                return SubsystemMatch{&core, &subsystem};
            }
        }
    }
    return SubsystemMatch{};
}

SubsystemMatch findSubsystem(const std::vector<CoreInfo>& cores, const std::string& coreName, unsigned gameType) {
    for (const CoreInfo& core : cores) {
        if (core.name != coreName) {
            continue;
        }
        for (const SubsystemInfo& subsystem : core.subsystems) {
            if (subsystem.gameType == gameType) {
                return SubsystemMatch{&core, &subsystem};
            }
        }
    }
    return SubsystemMatch{};
}

std::vector<std::string> allExtensions(const std::vector<CoreInfo>& cores) {
    std::vector<std::string> all;
    for (const CoreInfo& core : cores) {
        for (const std::string& ext : core.extensions) {
            if (std::find(all.begin(), all.end(), ext) == all.end()) {
                all.push_back(ext);
            }
        }
    }
    return all;
}

// --- The frontend's side of the API ----------------------------------------

namespace {
Core* g_active = nullptr;
}

struct Bridge {
    static void log(retro_log_level level, const char* fmt, ...) {
        if (level < RETRO_LOG_WARN && !verboseCoreLog()) {
            return;
        }
        static const char* const kLevels[] = {"debug", "info", "warn", "error"};
        std::va_list args;
        va_start(args, fmt);
        char text[1024];
        std::vsnprintf(text, sizeof(text), fmt, args);
        va_end(args);
        std::string line = text;
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
            line.pop_back();
        }
        std::fprintf(stderr, "[retro:%s] %s\n", kLevels[std::min<int>(level, 3)], line.c_str());
    }

    // Splits "Description; a|b|c" and takes the first value as the default.
    static void declareVariables(Core& core, const retro_variable* vars) {
        for (; vars && vars->key; ++vars) {
            if (core.variables_.count(vars->key)) {
                continue;  // already overridden by the options file
            }
            const std::string spec = vars->value ? vars->value : "";
            const size_t semi = spec.find("; ");
            std::string values = semi == std::string::npos ? spec : spec.substr(semi + 2);
            const size_t bar = values.find('|');
            core.variables_[vars->key] = bar == std::string::npos ? values : values.substr(0, bar);
        }
    }

    static bool environment(unsigned cmd, void* data) {
        Core* core = g_active;
        if (!core) {
            return false;
        }
        cmd &= ~(RETRO_ENVIRONMENT_EXPERIMENTAL | RETRO_ENVIRONMENT_PRIVATE);
        switch (cmd) {
            case RETRO_ENVIRONMENT_SET_ROTATION:
            case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
            case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
            case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
            case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
            case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
            case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
            case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
                return true;
            case RETRO_ENVIRONMENT_GET_CAN_DUPE:
                *static_cast<bool*>(data) = true;
                return true;
            case RETRO_ENVIRONMENT_SET_MESSAGE: {
                const auto* message = static_cast<const retro_message*>(data);
                std::fprintf(stderr, "[retro] %s\n", message->msg);
                return true;
            }
            case RETRO_ENVIRONMENT_SHUTDOWN:
                core->shutdownRequested_ = true;
                return true;
            case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
            case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
                *static_cast<const char**>(data) = core->systemDir_.c_str();
                return true;
            case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
                *static_cast<const char**>(data) = core->saveDir_.c_str();
                return true;
            case RETRO_ENVIRONMENT_GET_LIBRETRO_PATH:
                *static_cast<const char**>(data) = core->info_.path.c_str();
                return true;
            case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
                const int format = *static_cast<const int*>(data);
                if (format == RETRO_PIXEL_FORMAT_0RGB1555 || format == RETRO_PIXEL_FORMAT_XRGB8888 ||
                    format == RETRO_PIXEL_FORMAT_RGB565) {
                    core->pixelFormat_ = format;
                    return true;
                }
                return false;
            }
            case RETRO_ENVIRONMENT_SET_VARIABLES:
                declareVariables(*core, static_cast<const retro_variable*>(data));
                return true;
            case RETRO_ENVIRONMENT_GET_VARIABLE: {
                auto* var = static_cast<retro_variable*>(data);
                const auto found = core->variables_.find(var->key ? var->key : "");
                if (found == core->variables_.end()) {
                    var->value = nullptr;
                    return false;
                }
                var->value = found->second.c_str();
                return true;
            }
            case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
                *static_cast<bool*>(data) = false;
                return true;
            case RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES:
                *static_cast<uint64_t*>(data) = 1ull << RETRO_DEVICE_JOYPAD;
                return true;
            case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
                static_cast<retro_log_callback*>(data)->log = &Bridge::log;
                return true;
            case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
                const auto* av = static_cast<const retro_system_av_info*>(data);
                core->fps_ = av->timing.fps > 0.0 ? av->timing.fps : core->fps_;
                core->sampleRate_ = av->timing.sample_rate > 0.0 ? av->timing.sample_rate : core->sampleRate_;
                core->geometryAspect_ = av->geometry.aspect_ratio;
                return true;
            }
            case RETRO_ENVIRONMENT_SET_GEOMETRY:
                core->geometryAspect_ = static_cast<const retro_game_geometry*>(data)->aspect_ratio;
                return true;
            case RETRO_ENVIRONMENT_GET_USERNAME:
                *static_cast<const char**>(data) = "PVM";
                return true;
            case RETRO_ENVIRONMENT_GET_LANGUAGE:
                *static_cast<unsigned*>(data) = RETRO_LANGUAGE_ENGLISH;
                return true;
            case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
                *static_cast<int*>(data) = 3;  // video and audio wanted
                return true;
            case RETRO_ENVIRONMENT_GET_FASTFORWARDING:
                *static_cast<bool*>(data) = false;
                return true;
            case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
                return true;
            case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
                *static_cast<unsigned*>(data) = 0;  // the plain SET_VARIABLES form is enough
                return true;
            case RETRO_ENVIRONMENT_GET_INPUT_MAX_USERS:
                *static_cast<unsigned*>(data) = 1;
                return true;
            case RETRO_ENVIRONMENT_SET_HW_RENDER: {
                auto* hw = static_cast<retro_hw_render_callback*>(data);
                switch (hw->context_type) {
                    case RETRO_HW_CONTEXT_OPENGL:
                        core->hwRender_.type = HwContextType::OpenGL;
                        break;
                    case RETRO_HW_CONTEXT_OPENGL_CORE:
                        core->hwRender_.type = HwContextType::OpenGLCore;
                        break;
                    case RETRO_HW_CONTEXT_OPENGLES2:
                        core->hwRender_.type = HwContextType::OpenGLES2;
                        break;
                    case RETRO_HW_CONTEXT_OPENGLES3:
                        core->hwRender_.type = HwContextType::OpenGLES3;
                        break;
                    case RETRO_HW_CONTEXT_OPENGLES_VERSION:
                        // We hand out whatever context our own loader already set up
                        // (GL 3.3 core on desktop, GLES 3 on Android) regardless of the
                        // requested version -- there is no path to negotiate a newer
                        // one, so treat this the same as a plain GLES3 request.
                        core->hwRender_.type = HwContextType::OpenGLES3;
                        break;
                    default:
                        return false;  // Vulkan/D3D/etc: not something we can provide
                }
                core->hwRender_.versionMajor = hw->version_major;
                core->hwRender_.versionMinor = hw->version_minor;
                core->hwRender_.depth = hw->depth;
                core->hwRender_.stencil = hw->stencil;
                core->hwRender_.bottomLeftOrigin = hw->bottom_left_origin;
                core->hwContextResetFn_ = hw->context_reset;
                core->hwContextDestroyFn_ = hw->context_destroy;
                hw->get_current_framebuffer = &Bridge::hwGetCurrentFramebuffer;
                hw->get_proc_address = &Bridge::hwGetProcAddress;
                return true;
            }
            case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
#if defined(__ANDROID__)
                *static_cast<unsigned*>(data) = RETRO_HW_CONTEXT_OPENGLES3;
#else
                *static_cast<unsigned*>(data) = RETRO_HW_CONTEXT_OPENGL_CORE;
#endif
                return true;
            default:
                // VFS, rumble, disk control, core options v1/v2, Vulkan/D3D
                // negotiation, ... : not supported; cores fall back or say so.
                return false;
        }
    }

    static void video(const void* data, unsigned width, unsigned height, size_t pitch) {
        Core* core = g_active;
        if (!core || width == 0 || height == 0) {
            return;  // a duplicated frame: the previous picture stands
        }
        if (data == RETRO_HW_FRAME_BUFFER_VALID) {
            // Hardware-rendered: the core has already drawn into the FBO
            // hwFramebuffer_ names, directly -- there is no CPU pixel buffer
            // to convert, only the new size (it may change frame to frame,
            // up to maxWidth()/maxHeight()). Session::uploadFrame() sees this
            // as a no-op besides tracking that; App reads the FBO's own
            // texture (see Session::texture()) instead of frame().rgba.
            core->frame_.width = static_cast<int>(width);
            core->frame_.height = static_cast<int>(height);
            ++core->frame_.serial;
            return;
        }
        if (!data) {
            return;  // a duplicated frame: the previous picture stands
        }
        Frame& frame = core->frame_;
        frame.width = static_cast<int>(width);
        frame.height = static_cast<int>(height);
        frame.rgba.resize(static_cast<size_t>(width) * height);
        const auto* bytes = static_cast<const uint8_t*>(data);

        for (unsigned y = 0; y < height; ++y) {
            uint32_t* dst = frame.rgba.data() + static_cast<size_t>(y) * width;
            const uint8_t* row = bytes + static_cast<size_t>(y) * pitch;
            switch (core->pixelFormat_) {
                case RETRO_PIXEL_FORMAT_XRGB8888: {
                    const auto* src = reinterpret_cast<const uint32_t*>(row);
                    for (unsigned x = 0; x < width; ++x) {
                        const uint32_t v = src[x];
                        dst[x] = 0xFF000000u | ((v & 0xFF) << 16) | (v & 0xFF00) | ((v >> 16) & 0xFF);
                    }
                    break;
                }
                case RETRO_PIXEL_FORMAT_RGB565: {
                    const auto* src = reinterpret_cast<const uint16_t*>(row);
                    for (unsigned x = 0; x < width; ++x) {
                        const uint32_t v = src[x];
                        const uint32_t r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
                        dst[x] = 0xFF000000u | (((b << 3) | (b >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) |
                                 ((r << 3) | (r >> 2));
                    }
                    break;
                }
                default: {  // 0RGB1555
                    const auto* src = reinterpret_cast<const uint16_t*>(row);
                    for (unsigned x = 0; x < width; ++x) {
                        const uint32_t v = src[x];
                        const uint32_t r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
                        dst[x] = 0xFF000000u | (((b << 3) | (b >> 2)) << 16) | (((g << 3) | (g >> 2)) << 8) |
                                 ((r << 3) | (r >> 2));
                    }
                    break;
                }
            }
        }
        ++frame.serial;
    }

    static void audioSample(int16_t left, int16_t right) {
        if (g_active && g_active->onAudio) {
            const int16_t pair[2] = {left, right};
            g_active->onAudio(pair, 1);
        }
    }

    static size_t audioBatch(const int16_t* data, size_t frames) {
        if (g_active && g_active->onAudio && frames > 0) {
            g_active->onAudio(data, frames);
        }
        return frames;
    }

    static void inputPoll() {}

    static int16_t inputState(unsigned port, unsigned device, unsigned /*index*/, unsigned id) {
        Core* core = g_active;
        if (!core || port != 0 || (device & RETRO_DEVICE_MASK) != RETRO_DEVICE_JOYPAD) {
            return 0;
        }
        if (id == RETRO_DEVICE_ID_JOYPAD_MASK) {
            return static_cast<int16_t>(core->buttons_ & 0xFFFF);
        }
        return id < kPadButtonCount ? static_cast<int16_t>((core->buttons_ >> id) & 1u) : 0;
    }

    // The two callbacks RETRO_ENVIRONMENT_SET_HW_RENDER hands to the core
    // (hw->get_current_framebuffer/get_proc_address, above); both plain C
    // function pointers with no room for a context argument, hence g_active.
    static uintptr_t hwGetCurrentFramebuffer() { return g_active ? g_active->hwFramebuffer_ : 0; }

    static retro_proc_address_t hwGetProcAddress(const char* sym) {
        if (!g_active || !g_active->getProcAddress_) {
            return nullptr;
        }
        return reinterpret_cast<retro_proc_address_t>(g_active->getProcAddress_(sym));
    }
};

// --- Core ------------------------------------------------------------------

Core::Core() = default;

Core::~Core() {
    unloadGame();
    if (initialised_ && api_) {
        api_->deinit();
    }
    if (g_active == this) {
        g_active = nullptr;
    }
    closeLibrary(library_);
    delete api_;
}

bool Core::load(const CoreInfo& info, const std::string& systemDir, const std::string& saveDir,
                const std::string& optionsFile, std::string& error) {
    if (g_active) {
        error = "another core is already loaded";
        return false;
    }
    library_ = openLibrary(info.path);
    if (!library_) {
        error = libraryError();
        return false;
    }
    api_ = new Api();
    if (const char* missing = resolveApi(library_, *api_)) {
        error = std::string("not a libretro core (no ") + missing + ")";
        return false;
    }
    if (api_->api_version() != RETRO_API_VERSION) {
        error = "unsupported libretro API version " + std::to_string(api_->api_version());
        return false;
    }

    info_ = info;
    systemDir_ = systemDir;
    saveDir_ = saveDir;
    std::error_code ec;
    fs::create_directories(systemDir_, ec);
    fs::create_directories(saveDir_, ec);

    // "key = value" lines override a declared option's default.
    if (std::ifstream in(optionsFile); in) {
        std::string line;
        while (std::getline(in, line)) {
            const size_t eq = line.find('=');
            if (eq == std::string::npos || line.find('#') == 0) {
                continue;
            }
            auto trim = [](std::string s) {
                const size_t first = s.find_first_not_of(" \t\r");
                const size_t last = s.find_last_not_of(" \t\r");
                return first == std::string::npos ? std::string() : s.substr(first, last - first + 1);
            };
            variables_[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
        }
    }

    g_active = this;
    api_->set_environment(&Bridge::environment);
    api_->init();
    initialised_ = true;
    api_->set_video_refresh(&Bridge::video);
    api_->set_audio_sample(&Bridge::audioSample);
    api_->set_audio_sample_batch(&Bridge::audioBatch);
    api_->set_input_poll(&Bridge::inputPoll);
    api_->set_input_state(&Bridge::inputState);
    return true;
}

// Shared tail of loadGame()/loadGameSpecial(): once the core has accepted the
// content, read back its actual timing/geometry and connect a pad.
void Core::finishLoad() {
    gameLoaded_ = true;
    retro_system_av_info av{};
    api_->get_system_av_info(&av);
    fps_ = av.timing.fps > 0.0 ? av.timing.fps : 60.0;
    sampleRate_ = av.timing.sample_rate > 0.0 ? av.timing.sample_rate : 44100.0;
    baseWidth_ = av.geometry.base_width;
    baseHeight_ = av.geometry.base_height;
    geometryAspect_ = av.geometry.aspect_ratio;
    // For a hardware-rendered core this is the FBO size the caller needs to
    // allocate: base_width/height may grow up to this without notice.
    maxWidth_ = av.geometry.max_width > 0 ? av.geometry.max_width : baseWidth_;
    maxHeight_ = av.geometry.max_height > 0 ? av.geometry.max_height : baseHeight_;
    api_->set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
}

void Core::hwContextReset() {
    if (hwContextResetFn_) {
        hwContextResetFn_();
    }
}

void Core::hwContextDestroy() {
    if (hwContextDestroyFn_) {
        hwContextDestroyFn_();
    }
}

bool Core::loadGame(const std::string& romPath, std::string& error) {
    if (!initialised_) {
        error = "core not loaded";
        return false;
    }
    retro_game_info game{};
    game.path = romPath.c_str();
    std::vector<uint8_t> rom;
    if (!info_.needFullpath) {
        if (!readFile(romPath, rom)) {
            error = "cannot read " + romPath;
            return false;
        }
        game.data = rom.data();
        game.size = rom.size();
    }
    if (!api_->load_game(&game)) {
        error = "the core did not accept this game";
        return false;
    }
    saveMemoryId_ = RETRO_MEMORY_SAVE_RAM;
    finishLoad();
    return true;
}

bool Core::loadGameSpecial(unsigned gameType, const std::vector<std::string>& contentPaths, std::string& error) {
    if (!initialised_) {
        error = "core not loaded";
        return false;
    }
    const SubsystemInfo* subsystem = nullptr;
    for (const SubsystemInfo& candidate : info_.subsystems) {
        if (candidate.gameType == gameType) {
            subsystem = &candidate;
            break;
        }
    }
    if (!subsystem) {
        error = "this core does not declare that subsystem";
        return false;
    }
    if (contentPaths.size() != subsystem->roms.size()) {
        error = "expected " + std::to_string(subsystem->roms.size()) + " file(s) for " + subsystem->desc;
        return false;
    }
    if (!api_->load_game_special) {
        error = "this core does not implement subsystem loading";
        return false;
    }

    std::vector<retro_game_info> games(contentPaths.size());
    // Keeps file bytes alive for any slot that wants them inline rather than
    // by path (mirrors loadGame()'s own info_.needFullpath handling, just
    // per-slot instead of once for the whole core).
    std::vector<std::vector<uint8_t>> buffers(contentPaths.size());
    for (size_t i = 0; i < contentPaths.size(); ++i) {
        games[i] = retro_game_info{};
        games[i].path = contentPaths[i].c_str();
        if (!subsystem->roms[i].needFullpath) {
            if (!readFile(contentPaths[i], buffers[i])) {
                error = "cannot read " + contentPaths[i];
                return false;
            }
            games[i].data = buffers[i].data();
            games[i].size = buffers[i].size();
        }
    }
    if (!api_->load_game_special(gameType, games.data(), games.size())) {
        error = "the core did not accept this content";
        return false;
    }
    // The first slot's own declared memory region, if any -- e.g. bsnes's
    // Super Game Boy subsystem keeps the Game Boy side's battery RAM under
    // its own id, not RETRO_MEMORY_SAVE_RAM.
    saveMemoryId_ = !subsystem->roms.empty() && !subsystem->roms.front().memory.empty()
                        ? subsystem->roms.front().memory.front().type
                        : RETRO_MEMORY_SAVE_RAM;
    finishLoad();
    return true;
}

void Core::unloadGame() {
    if (gameLoaded_ && api_) {
        api_->unload_game();
    }
    gameLoaded_ = false;
}

void Core::run() {
    if (gameLoaded_) {
        api_->run();
    }
}

void Core::reset() {
    if (gameLoaded_) {
        api_->reset();
    }
}

float Core::aspectRatio() const {
    if (geometryAspect_ > 0.0f) {
        return geometryAspect_;
    }
    if (frame_.height > 0) {
        return static_cast<float>(frame_.width) / static_cast<float>(frame_.height);
    }
    return baseHeight_ > 0 ? static_cast<float>(baseWidth_) / static_cast<float>(baseHeight_) : 4.0f / 3.0f;
}

bool Core::takeShutdownRequest() {
    const bool requested = shutdownRequested_;
    shutdownRequested_ = false;
    return requested;
}

void Core::loadSaveRam(const std::string& path) {
    if (!gameLoaded_) {
        return;
    }
    void* memory = api_->get_memory_data(saveMemoryId_);
    const size_t size = api_->get_memory_size(saveMemoryId_);
    if (!memory || size == 0) {
        return;
    }
    std::vector<uint8_t> saved;
    if (readFile(path, saved) && !saved.empty()) {
        std::memcpy(memory, saved.data(), std::min(saved.size(), size));
    }
    lastSaveRam_.assign(static_cast<uint8_t*>(memory), static_cast<uint8_t*>(memory) + size);
}

bool Core::flushSaveRam(const std::string& path) {
    if (!gameLoaded_) {
        return true;
    }
    const void* memory = api_->get_memory_data(saveMemoryId_);
    const size_t size = api_->get_memory_size(saveMemoryId_);
    if (!memory || size == 0) {
        return true;
    }
    if (lastSaveRam_.size() == size && std::memcmp(lastSaveRam_.data(), memory, size) == 0) {
        return true;
    }
    if (!writeFileAtomic(path, static_cast<const uint8_t*>(memory), size)) {
        return false;
    }
    lastSaveRam_.assign(static_cast<const uint8_t*>(memory), static_cast<const uint8_t*>(memory) + size);
    return true;
}

bool Core::saveState(const std::string& path) {
    if (!gameLoaded_) {
        return false;
    }
    const size_t size = api_->serialize_size();
    if (size == 0) {
        return false;
    }
    std::vector<uint8_t> state(size);
    return api_->serialize(state.data(), size) && writeFileAtomic(path, state.data(), state.size());
}

bool Core::loadState(const std::string& path) {
    std::vector<uint8_t> state;
    if (!gameLoaded_ || !readFile(path, state) || state.empty()) {
        return false;
    }
    return api_->unserialize(state.data(), state.size());
}

}  // namespace retro
