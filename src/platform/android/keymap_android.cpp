#include "platform/android/keymap_android.h"

#include <android/input.h>
#include <android/keycodes.h>

#include <cctype>
#include <cstdlib>
#include <string>
#include <utility>

namespace input::android {

namespace {

constexpr std::pair<const char*, int> kNamedKeys[] = {
    {"UP", AKEYCODE_DPAD_UP},
    {"DOWN", AKEYCODE_DPAD_DOWN},
    {"LEFT", AKEYCODE_DPAD_LEFT},
    {"RIGHT", AKEYCODE_DPAD_RIGHT},
    {"DPAD_UP", AKEYCODE_DPAD_UP},
    {"DPAD_DOWN", AKEYCODE_DPAD_DOWN},
    {"DPAD_LEFT", AKEYCODE_DPAD_LEFT},
    {"DPAD_RIGHT", AKEYCODE_DPAD_RIGHT},
    {"DPAD_CENTER", AKEYCODE_DPAD_CENTER},
    {"ENTER", AKEYCODE_ENTER},
    {"KP_ENTER", AKEYCODE_NUMPAD_ENTER},
    {"ESCAPE", AKEYCODE_ESCAPE},
    {"BACKSPACE", AKEYCODE_DEL},
    {"SPACE", AKEYCODE_SPACE},
    {"TAB", AKEYCODE_TAB},
    {"COMMA", AKEYCODE_COMMA},
    {"PERIOD", AKEYCODE_PERIOD},
    {"MINUS", AKEYCODE_MINUS},
    {"EQUAL", AKEYCODE_EQUALS},
    {"PAGE_UP", AKEYCODE_PAGE_UP},
    {"PAGE_DOWN", AKEYCODE_PAGE_DOWN},
    {"HOME", AKEYCODE_MOVE_HOME},
    {"END", AKEYCODE_MOVE_END},
    {"BACK", AKEYCODE_BACK},
    {"BUTTON_A", AKEYCODE_BUTTON_A},
    {"BUTTON_B", AKEYCODE_BUTTON_B},
    {"BUTTON_X", AKEYCODE_BUTTON_X},
    {"BUTTON_Y", AKEYCODE_BUTTON_Y},
    {"BUTTON_L1", AKEYCODE_BUTTON_L1},
    {"BUTTON_R1", AKEYCODE_BUTTON_R1},
    {"BUTTON_START", AKEYCODE_BUTTON_START},
    {"BUTTON_SELECT", AKEYCODE_BUTTON_SELECT},
    {"MEDIA_PLAY_PAUSE", AKEYCODE_MEDIA_PLAY_PAUSE},
};

std::string upper(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    return out;
}

}  // namespace

std::optional<Phase> phaseFromKeyEvent(int akeyAction, int repeatCount) {
    switch (akeyAction) {
        case AKEY_EVENT_ACTION_DOWN:
            return repeatCount > 0 ? Phase::Repeat : Phase::Press;
        case AKEY_EVENT_ACTION_UP:
            return Phase::Release;
        default:
            return std::nullopt;
    }
}

KeyMap defaultKeyMap() {
    KeyMap map;

    // Handheld: D-pad and face buttons.
    map.bind(AKEYCODE_DPAD_UP, Action::Up);
    map.bind(AKEYCODE_DPAD_DOWN, Action::Down);
    map.bind(AKEYCODE_DPAD_LEFT, Action::Left);
    map.bind(AKEYCODE_DPAD_RIGHT, Action::Right);
    map.bind(AKEYCODE_BUTTON_A, Action::Confirm);
    map.bind(AKEYCODE_DPAD_CENTER, Action::Confirm);
    map.bind(AKEYCODE_BUTTON_B, Action::Back);
    map.bind(AKEYCODE_BACK, Action::Back);
    map.bind(AKEYCODE_BUTTON_X, Action::ToggleCrt);
    // Shoulders and Y follow the desktop pairing of playback and teletext
    // actions (each screen reacts to only one of the two).
    map.bind(AKEYCODE_BUTTON_L1, Action::SeekBack);
    map.bind(AKEYCODE_BUTTON_R1, Action::SeekFwd);
    map.bind(AKEYCODE_BUTTON_L1, Action::FastextRed);
    map.bind(AKEYCODE_BUTTON_R1, Action::FastextGreen);
    map.bind(AKEYCODE_BUTTON_Y, Action::MediaInfo);
    map.bind(AKEYCODE_BUTTON_Y, Action::FastextYellow);
    map.bind(AKEYCODE_BUTTON_START, Action::FastextBlue);
    map.bind(AKEYCODE_MEDIA_PLAY_PAUSE, Action::PlayPause);

    // Keyboard, mirroring the desktop defaults.
    map.bind(AKEYCODE_ENTER, Action::Confirm);
    map.bind(AKEYCODE_NUMPAD_ENTER, Action::Confirm);
    map.bind(AKEYCODE_ESCAPE, Action::Back);
    map.bind(AKEYCODE_DEL, Action::BackSoft);
    map.bind(AKEYCODE_SPACE, Action::PlayPause);
    map.bind(AKEYCODE_F, Action::PlayPause);
    map.bind(AKEYCODE_COMMA, Action::VolumeDown);
    map.bind(AKEYCODE_PERIOD, Action::VolumeUp);
    map.bind(AKEYCODE_M, Action::ToggleOsd);
    map.bind(AKEYCODE_I, Action::ToggleOsd);
    map.bind(AKEYCODE_R, Action::MediaInfo);
    map.bind(AKEYCODE_V, Action::VideoScale);
    map.bind(AKEYCODE_B, Action::AspectRatio);
    map.bind(AKEYCODE_C, Action::ToggleCrt);

    map.bind(AKEYCODE_F1, Action::FastextRed);
    map.bind(AKEYCODE_R, Action::FastextRed);
    map.bind(AKEYCODE_F2, Action::FastextGreen);
    map.bind(AKEYCODE_G, Action::FastextGreen);
    map.bind(AKEYCODE_F3, Action::FastextYellow);
    map.bind(AKEYCODE_Y, Action::FastextYellow);
    map.bind(AKEYCODE_M, Action::FastextYellow);
    map.bind(AKEYCODE_F4, Action::FastextBlue);
    map.bind(AKEYCODE_B, Action::FastextBlue);

    for (int d = 0; d <= 9; ++d) {
        const auto digit = static_cast<Action>(static_cast<int>(Action::Digit0) + d);
        map.bind(AKEYCODE_0 + d, digit);
        map.bind(AKEYCODE_NUMPAD_0 + d, digit);
    }
    return map;
}

std::optional<int> codeFromName(std::string_view rawName) {
    const std::string name = upper(rawName);
    if (name.size() == 1) {
        const char c = name[0];
        if (c >= 'A' && c <= 'Z') return AKEYCODE_A + (c - 'A');
        if (c >= '0' && c <= '9') return AKEYCODE_0 + (c - '0');
    }
    if (name.size() == 4 && name.rfind("KP_", 0) == 0 && name[3] >= '0' && name[3] <= '9') {
        return AKEYCODE_NUMPAD_0 + (name[3] - '0');
    }
    if (name.size() >= 2 && name.size() <= 3 && name[0] == 'F') {
        const int n = std::atoi(name.c_str() + 1);
        if (n >= 1 && n <= 12 && std::to_string(n) == name.substr(1)) return AKEYCODE_F1 + (n - 1);
    }
    for (const auto& [keyName, code] : kNamedKeys) {
        if (name == keyName) return code;
    }
    return std::nullopt;
}

}  // namespace input::android
