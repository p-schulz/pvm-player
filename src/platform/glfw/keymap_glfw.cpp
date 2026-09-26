#include "platform/glfw/keymap_glfw.h"

#include <GLFW/glfw3.h>

#include <cctype>
#include <cstdlib>
#include <string>
#include <utility>

namespace input::glfw {

namespace {

// Named keys beyond letters, digits and F-keys (handled algorithmically).
constexpr std::pair<const char*, int> kNamedKeys[] = {
    {"UP", GLFW_KEY_UP},
    {"DOWN", GLFW_KEY_DOWN},
    {"LEFT", GLFW_KEY_LEFT},
    {"RIGHT", GLFW_KEY_RIGHT},
    {"ENTER", GLFW_KEY_ENTER},
    {"KP_ENTER", GLFW_KEY_KP_ENTER},
    {"ESCAPE", GLFW_KEY_ESCAPE},
    {"BACKSPACE", GLFW_KEY_BACKSPACE},
    {"SPACE", GLFW_KEY_SPACE},
    {"TAB", GLFW_KEY_TAB},
    {"COMMA", GLFW_KEY_COMMA},
    {"PERIOD", GLFW_KEY_PERIOD},
    {"MINUS", GLFW_KEY_MINUS},
    {"EQUAL", GLFW_KEY_EQUAL},
    {"KP_ADD", GLFW_KEY_KP_ADD},
    {"KP_SUBTRACT", GLFW_KEY_KP_SUBTRACT},
    {"PAGE_UP", GLFW_KEY_PAGE_UP},
    {"PAGE_DOWN", GLFW_KEY_PAGE_DOWN},
    {"HOME", GLFW_KEY_HOME},
    {"END", GLFW_KEY_END},
    {"INSERT", GLFW_KEY_INSERT},
    {"DELETE", GLFW_KEY_DELETE},
};

std::string upper(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    return out;
}

// Windows delivers the hardware Play/Pause media key as a WM_KEYDOWN with no
// virtual key GLFW recognizes, so it arrives as GLFW_KEY_UNKNOWN; GLFW
// derives `scancode` from the PS/2 Set-1 code, and 0x122 is that key's code
// (0x22 plus GLFW's extended-key bit 0x100). The VK code never appears.
constexpr int kWin32MediaPlayPauseScancode = 0x122;

}  // namespace

int inputCode(int glfwKey, int scancode) {
    return glfwKey != GLFW_KEY_UNKNOWN ? glfwKey : kScancodeBase + scancode;
}

std::optional<Phase> phaseFromGlfwAction(int glfwAction) {
    switch (glfwAction) {
        case GLFW_PRESS:
            return Phase::Press;
        case GLFW_REPEAT:
            return Phase::Repeat;
        case GLFW_RELEASE:
            return Phase::Release;
        default:
            return std::nullopt;
    }
}

KeyMap defaultKeyMap() {
    KeyMap map;
    map.bind(GLFW_KEY_UP, Action::Up);
    map.bind(GLFW_KEY_DOWN, Action::Down);
    map.bind(GLFW_KEY_LEFT, Action::Left);
    map.bind(GLFW_KEY_RIGHT, Action::Right);
    map.bind(GLFW_KEY_ENTER, Action::Confirm);
    map.bind(GLFW_KEY_KP_ENTER, Action::Confirm);
    map.bind(GLFW_KEY_ESCAPE, Action::Back);
    map.bind(GLFW_KEY_BACKSPACE, Action::BackSoft);

    map.bind(GLFW_KEY_SPACE, Action::PlayPause);
    map.bind(GLFW_KEY_F, Action::PlayPause);
#ifdef _WIN32
    map.bind(kScancodeBase + kWin32MediaPlayPauseScancode, Action::PlayPause);
#endif
    map.bind(GLFW_KEY_COMMA, Action::VolumeDown);
    map.bind(GLFW_KEY_PERIOD, Action::VolumeUp);
    map.bind(GLFW_KEY_M, Action::ToggleOsd);
    map.bind(GLFW_KEY_I, Action::ToggleOsd);
    map.bind(GLFW_KEY_R, Action::MediaInfo);
    map.bind(GLFW_KEY_V, Action::VideoScale);
    map.bind(GLFW_KEY_B, Action::AspectRatio);

    map.bind(GLFW_KEY_C, Action::ToggleCrt);

    // Teletext colour keys: F1-F4, or R/G/Y/B, and M for "menu" (index).
    // R, B and M share keys with playback actions; the two never apply on
    // the same screen.
    map.bind(GLFW_KEY_F1, Action::FastextRed);
    map.bind(GLFW_KEY_R, Action::FastextRed);
    map.bind(GLFW_KEY_F2, Action::FastextGreen);
    map.bind(GLFW_KEY_G, Action::FastextGreen);
    map.bind(GLFW_KEY_F3, Action::FastextYellow);
    map.bind(GLFW_KEY_Y, Action::FastextYellow);
    map.bind(GLFW_KEY_M, Action::FastextYellow);
    map.bind(GLFW_KEY_F4, Action::FastextBlue);
    map.bind(GLFW_KEY_B, Action::FastextBlue);

    for (int d = 0; d <= 9; ++d) {
        const auto digit = static_cast<Action>(static_cast<int>(Action::Digit0) + d);
        map.bind(GLFW_KEY_0 + d, digit);
        map.bind(GLFW_KEY_KP_0 + d, digit);
    }
    return map;
}

std::optional<int> codeFromName(std::string_view rawName) {
    const std::string name = upper(rawName);
    if (name.size() == 1) {
        const char c = name[0];
        if (c >= 'A' && c <= 'Z') return GLFW_KEY_A + (c - 'A');
        if (c >= '0' && c <= '9') return GLFW_KEY_0 + (c - '0');
    }
    if (name.size() == 4 && name.rfind("KP_", 0) == 0 && name[3] >= '0' && name[3] <= '9') {
        return GLFW_KEY_KP_0 + (name[3] - '0');
    }
    if (name.size() >= 2 && name.size() <= 3 && name[0] == 'F') {
        const int n = std::atoi(name.c_str() + 1);
        if (n >= 1 && n <= 25 && std::to_string(n) == name.substr(1)) return GLFW_KEY_F1 + (n - 1);
    }
    for (const auto& [keyName, code] : kNamedKeys) {
        if (name == keyName) return code;
    }
    if (name.rfind("SCANCODE_", 0) == 0 && name.size() > 9) {
        char* end = nullptr;
        const long scancode = std::strtol(name.c_str() + 9, &end, 16);
        if (end && *end == '\0' && scancode >= 0) return kScancodeBase + static_cast<int>(scancode);
    }
    return std::nullopt;
}

}  // namespace input::glfw
