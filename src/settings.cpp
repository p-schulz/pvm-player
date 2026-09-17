#include "settings.h"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace {

std::string trim(const std::string& s) {
    const char* whitespace = " \t\r\n";
    const size_t begin = s.find_first_not_of(whitespace);
    if (begin == std::string::npos) {
        return "";
    }
    const size_t end = s.find_last_not_of(whitespace);
    return s.substr(begin, end - begin + 1);
}

bool parseInt(const std::string& value, int& out) {
    try {
        size_t consumed = 0;
        int parsed = std::stoi(value, &consumed);
        if (consumed != value.size()) {
            return false;
        }
        out = parsed;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool parseFloat(const std::string& value, float& out) {
    try {
        size_t consumed = 0;
        float parsed = std::stof(value, &consumed);
        if (consumed != value.size()) {
            return false;
        }
        out = parsed;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool parseBool(const std::string& value, bool& out) {
    if (value == "1" || value == "true" || value == "on") {
        out = true;
        return true;
    }
    if (value == "0" || value == "false" || value == "off") {
        out = false;
        return true;
    }
    return false;
}

}  // namespace

bool loadSettings(const std::string& path, AppSettings& settings) {
    std::ifstream file(path);
    if (!file) {
        return false;  // no config file yet (e.g. first run) -- not an error
    }

    std::string line;
    int lineNumber = 0;
    while (std::getline(file, line)) {
        ++lineNumber;
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }

        const size_t eq = trimmed.find('=');
        if (eq == std::string::npos) {
            std::fprintf(stderr, "%s:%d: ignoring malformed line (no '='): %s\n", path.c_str(), lineNumber,
                         trimmed.c_str());
            continue;
        }

        const std::string key = trim(trimmed.substr(0, eq));
        const std::string value = trim(trimmed.substr(eq + 1));

        bool ok = true;
        if (key == "font_size") {
            ok = parseInt(value, settings.fontSizePx);
        } else if (key == "font_file") {
            settings.fontFile = value;
        } else if (key == "menu_position") {
            ok = parseInt(value, settings.menuPositionIndex);
        } else if (key == "selection_style") {
            ok = parseInt(value, settings.selectionStyleIndex);
        } else if (key == "menu_scale_x") {
            ok = parseFloat(value, settings.menuScaleX);
        } else if (key == "menu_scale_y") {
            ok = parseFloat(value, settings.menuScaleY);
        } else if (key == "text_scale_x") {
            ok = parseFloat(value, settings.textScaleX);
        } else if (key == "text_scale_y") {
            ok = parseFloat(value, settings.textScaleY);
        } else if (key == "start_directory") {
            settings.startDirectory = value;
        } else if (key == "last_used_directory") {
            settings.lastUsedDirectory = value;
        } else if (key == "show_hidden_files") {
            ok = parseBool(value, settings.showHiddenFiles);
        } else if (key == "brightness") {
            ok = parseFloat(value, settings.brightness);
        } else if (key == "contrast") {
            ok = parseFloat(value, settings.contrast);
        } else if (key == "saturation") {
            ok = parseFloat(value, settings.saturation);
        } else if (key == "crt_enabled") {
            ok = parseBool(value, settings.crtEnabled);
        } else if (key == "crt_effect_strength") {
            ok = parseFloat(value, settings.crtEffectStrength);
        } else if (key == "bloom_strength") {
            ok = parseFloat(value, settings.bloomStrength);
        } else if (key == "scanline_count") {
            ok = parseInt(value, settings.scanlineCount);
        } else if (key == "vignette_strength") {
            ok = parseFloat(value, settings.vignetteStrength);
        } else if (key == "color_tear") {
            ok = parseFloat(value, settings.colorTear);
        } else if (key == "volume") {
            ok = parseInt(value, settings.volume);
        } else if (key == "video_scale_mode") {
            ok = parseInt(value, settings.videoScaleModeIndex);
        } else if (key == "aspect_override_index") {
            ok = parseInt(value, settings.aspectOverrideIndex);
        } else {
            // Unknown key: ignored rather than treated as an error, so an
            // older config file still loads after new settings are added.
            continue;
        }

        if (!ok) {
            std::fprintf(stderr, "%s:%d: ignoring invalid value for %s: %s\n", path.c_str(), lineNumber,
                         key.c_str(), value.c_str());
        }
    }

    return true;
}

bool saveSettings(const std::string& path, const AppSettings& settings) {
    std::ofstream file(path, std::ios::out | std::ios::trunc);
    if (!file) {
        std::fprintf(stderr, "Failed to write settings to %s\n", path.c_str());
        return false;
    }

    file << "# PVM Player settings -- edited automatically, but plain text if you\n";
    file << "# want to tweak it by hand while the app isn't running.\n";
    file << "font_size=" << settings.fontSizePx << "\n";
    file << "font_file=" << settings.fontFile << "\n";
    file << "menu_position=" << settings.menuPositionIndex << "\n";
    file << "selection_style=" << settings.selectionStyleIndex << "\n";
    file << "menu_scale_x=" << settings.menuScaleX << "\n";
    file << "menu_scale_y=" << settings.menuScaleY << "\n";
    file << "text_scale_x=" << settings.textScaleX << "\n";
    file << "text_scale_y=" << settings.textScaleY << "\n";
    file << "start_directory=" << settings.startDirectory << "\n";
    file << "last_used_directory=" << settings.lastUsedDirectory << "\n";
    file << "show_hidden_files=" << (settings.showHiddenFiles ? "true" : "false") << "\n";
    file << "brightness=" << settings.brightness << "\n";
    file << "contrast=" << settings.contrast << "\n";
    file << "saturation=" << settings.saturation << "\n";
    file << "crt_enabled=" << (settings.crtEnabled ? "true" : "false") << "\n";
    file << "crt_effect_strength=" << settings.crtEffectStrength << "\n";
    file << "bloom_strength=" << settings.bloomStrength << "\n";
    file << "scanline_count=" << settings.scanlineCount << "\n";
    file << "vignette_strength=" << settings.vignetteStrength << "\n";
    file << "color_tear=" << settings.colorTear << "\n";
    file << "volume=" << settings.volume << "\n";
    file << "video_scale_mode=" << settings.videoScaleModeIndex << "\n";
    file << "aspect_override_index=" << settings.aspectOverrideIndex << "\n";

    return static_cast<bool>(file);
}
