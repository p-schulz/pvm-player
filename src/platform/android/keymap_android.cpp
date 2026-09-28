#include "platform/android/keymap_android.h"

#include <android/keycodes.h>

#include <cctype>
#include <cstdlib>
#include <string>
#include <utility>

#include "input/gamepad_input.h"

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
    {"LEFT_SHIFT", AKEYCODE_SHIFT_LEFT},
    {"RIGHT_SHIFT", AKEYCODE_SHIFT_RIGHT},
    {"PAD_A", AKEYCODE_BUTTON_A},
    {"PAD_B", AKEYCODE_BUTTON_B},
    {"PAD_X", AKEYCODE_BUTTON_X},
    {"PAD_Y", AKEYCODE_BUTTON_Y},
    {"PAD_L1", AKEYCODE_BUTTON_L1},
    {"PAD_R1", AKEYCODE_BUTTON_R1},
    {"PAD_L2", AKEYCODE_BUTTON_L2},
    {"PAD_R2", AKEYCODE_BUTTON_R2},
    {"PAD_START", AKEYCODE_BUTTON_START},
    {"PAD_SELECT", AKEYCODE_BUTTON_SELECT},
    {"PAD_MODE", AKEYCODE_BUTTON_MODE},
    {"PAD_THUMBL", AKEYCODE_BUTTON_THUMBL},
    {"PAD_THUMBR", AKEYCODE_BUTTON_THUMBR},
    {"BUTTON_A", AKEYCODE_BUTTON_A},
    {"BUTTON_B", AKEYCODE_BUTTON_B},
    {"BUTTON_X", AKEYCODE_BUTTON_X},
    {"BUTTON_Y", AKEYCODE_BUTTON_Y},
    {"BUTTON_L1", AKEYCODE_BUTTON_L1},
    {"BUTTON_R1", AKEYCODE_BUTTON_R1},
    {"BUTTON_L2", AKEYCODE_BUTTON_L2},
    {"BUTTON_R2", AKEYCODE_BUTTON_R2},
    {"BUTTON_START", AKEYCODE_BUTTON_START},
    {"BUTTON_SELECT", AKEYCODE_BUTTON_SELECT},
    {"HAT_UP", input::kHatUp},
    {"HAT_DOWN", input::kHatDown},
    {"HAT_LEFT", input::kHatLeft},
    {"HAT_RIGHT", input::kHatRight},
    {"LSTICK_UP", input::kLeftStickUp},
    {"LSTICK_DOWN", input::kLeftStickDown},
    {"LSTICK_LEFT", input::kLeftStickLeft},
    {"LSTICK_RIGHT", input::kLeftStickRight},
    {"MEDIA_PLAY_PAUSE", AKEYCODE_MEDIA_PLAY_PAUSE},
    {"MEDIA_PLAY", AKEYCODE_MEDIA_PLAY},
    {"MEDIA_PAUSE", AKEYCODE_MEDIA_PAUSE},
    {"MEDIA_NEXT", AKEYCODE_MEDIA_NEXT},
    {"MEDIA_PREVIOUS", AKEYCODE_MEDIA_PREVIOUS},
    {"MEDIA_FAST_FORWARD", AKEYCODE_MEDIA_FAST_FORWARD},
    {"MEDIA_REWIND", AKEYCODE_MEDIA_REWIND},
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

KeyMap defaultKeyMap() {
    KeyMap map;

    // Handheld: D-pad (as keys, and as the hat axis some devices report it
    // as) and the left stick.
    for (const int code : {int(AKEYCODE_DPAD_UP), int(kHatUp), int(kLeftStickUp)}) map.bind(code, Action::Up);
    for (const int code : {int(AKEYCODE_DPAD_DOWN), int(kHatDown), int(kLeftStickDown)}) map.bind(code, Action::Down);
    for (const int code : {int(AKEYCODE_DPAD_LEFT), int(kHatLeft), int(kLeftStickLeft)}) map.bind(code, Action::Left);
    for (const int code : {int(AKEYCODE_DPAD_RIGHT), int(kHatRight), int(kLeftStickRight)}) {
        map.bind(code, Action::Right);
    }

    // Face buttons. Android names them by position in the Xbox layout; a
    // device with Nintendo-style labels can swap A and B in keys.cfg.
    map.bind(AKEYCODE_BUTTON_A, Action::Confirm);
    map.bind(AKEYCODE_BUTTON_A, Action::PlayPause);
    map.bind(AKEYCODE_DPAD_CENTER, Action::Confirm);
    map.bind(AKEYCODE_BUTTON_B, Action::Back);
    map.bind(AKEYCODE_BACK, Action::Back);
    map.bind(AKEYCODE_BUTTON_X, Action::ToggleCrt);
    map.bind(AKEYCODE_BUTTON_Y, Action::MediaInfo);
    map.bind(AKEYCODE_BUTTON_Y, Action::FastextYellow);
    map.bind(AKEYCODE_BUTTON_Y | kHoldFlag, Action::PageEntry);

    // Shoulders: page lists, seek while playing, red/green on teletext.
    map.bind(AKEYCODE_BUTTON_L1, Action::PageUp);
    map.bind(AKEYCODE_BUTTON_L1, Action::SeekBack);
    map.bind(AKEYCODE_BUTTON_L1, Action::FastextRed);
    map.bind(AKEYCODE_BUTTON_R1, Action::PageDown);
    map.bind(AKEYCODE_BUTTON_R1, Action::SeekFwd);
    map.bind(AKEYCODE_BUTTON_R1, Action::FastextGreen);

    map.bind(AKEYCODE_BUTTON_START, Action::OpenSettings);
    map.bind(AKEYCODE_BUTTON_START, Action::ToggleOsd);
    map.bind(AKEYCODE_BUTTON_START, Action::FastextBlue);
    map.bind(AKEYCODE_BUTTON_SELECT, Action::NextSection);
    map.bind(AKEYCODE_BUTTON_SELECT, Action::AspectRatio);
    map.bind(AKEYCODE_BUTTON_THUMBR, Action::VideoScale);  // pillarbox / stretch / full width
    map.bind(AKEYCODE_BUTTON_THUMBL, Action::ToggleFavorite);

    // Games. Android names the face buttons by position in the Xbox layout
    // (A bottom, B right, X left, Y top); the RetroPad is laid out the same
    // way (B bottom, A right, Y left, X top), so they cross over. A trigger
    // pulled as an axis is L2/R2 as well (see defaultAnalogBindings()).
    map.bind(AKEYCODE_BUTTON_A, Action::RetroB);
    map.bind(AKEYCODE_BUTTON_B, Action::RetroA);
    map.bind(AKEYCODE_BUTTON_X, Action::RetroY);
    map.bind(AKEYCODE_BUTTON_Y, Action::RetroX);
    map.bind(AKEYCODE_BUTTON_L1, Action::RetroL);
    map.bind(AKEYCODE_BUTTON_R1, Action::RetroR);
    map.bind(AKEYCODE_BUTTON_L2, Action::RetroL2);
    map.bind(AKEYCODE_BUTTON_R2, Action::RetroR2);
    map.bind(AKEYCODE_BUTTON_START, Action::RetroStart);
    map.bind(AKEYCODE_BUTTON_SELECT, Action::RetroSelect);
    // ...and on a keyboard, as on the desktop.
    map.bind(AKEYCODE_X, Action::RetroA);
    map.bind(AKEYCODE_Z, Action::RetroB);
    map.bind(AKEYCODE_S, Action::RetroX);
    map.bind(AKEYCODE_A, Action::RetroY);
    map.bind(AKEYCODE_Q, Action::RetroL);
    map.bind(AKEYCODE_W, Action::RetroR);
    map.bind(AKEYCODE_E, Action::RetroL2);
    map.bind(AKEYCODE_T, Action::RetroR2);
    map.bind(AKEYCODE_ENTER, Action::RetroStart);
    map.bind(AKEYCODE_NUMPAD_ENTER, Action::RetroStart);
    map.bind(AKEYCODE_SHIFT_RIGHT, Action::RetroSelect);
    map.bind(AKEYCODE_TAB, Action::RetroSelect);

    // Media keys (a headset, a keyboard, the system's media buttons). The
    // volume keys are deliberately not bound: they stay with the system.
    map.bind(AKEYCODE_MEDIA_PLAY_PAUSE, Action::PlayPause);
    map.bind(AKEYCODE_MEDIA_PLAY, Action::PlayPause);
    map.bind(AKEYCODE_MEDIA_PAUSE, Action::PlayPause);
    map.bind(AKEYCODE_MEDIA_NEXT, Action::SeekFwd);
    map.bind(AKEYCODE_MEDIA_FAST_FORWARD, Action::SeekFwd);
    map.bind(AKEYCODE_MEDIA_PREVIOUS, Action::SeekBack);
    map.bind(AKEYCODE_MEDIA_REWIND, Action::SeekBack);

    // Keyboard, mirroring the desktop defaults.
    map.bind(AKEYCODE_ENTER, Action::Confirm);
    map.bind(AKEYCODE_NUMPAD_ENTER, Action::Confirm);
    map.bind(AKEYCODE_ESCAPE, Action::Back);
    map.bind(AKEYCODE_DEL, Action::BackSoft);
    map.bind(AKEYCODE_PAGE_UP, Action::PageUp);
    map.bind(AKEYCODE_PAGE_DOWN, Action::PageDown);
    map.bind(AKEYCODE_TAB, Action::NextSection);
    map.bind(AKEYCODE_SPACE, Action::PlayPause);
    map.bind(AKEYCODE_F, Action::PlayPause);
    map.bind(AKEYCODE_F, Action::ToggleFavorite);  // games: only active in the ROM browser/favorites/in-game
    map.bind(AKEYCODE_P, Action::PlaySubsystem);   // games: only active in the ROM browser/favorites
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
    if (name.rfind("HOLD_", 0) == 0) {
        const std::optional<int> base = codeFromName(name.substr(5));
        if (!base || (*base & kHoldFlag)) {
            return std::nullopt;
        }
        return *base | kHoldFlag;
    }
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

std::optional<std::string> nameFromCode(int code) {
    if (code & kHoldFlag) {
        const std::optional<std::string> base = nameFromCode(code & ~kHoldFlag);
        return base ? std::optional<std::string>("HOLD_" + *base) : std::nullopt;
    }
    if ((code >= AKEYCODE_A && code <= AKEYCODE_Z)) {
        return std::string(1, static_cast<char>('A' + (code - AKEYCODE_A)));
    }
    if (code >= AKEYCODE_0 && code <= AKEYCODE_9) {
        return std::string(1, static_cast<char>('0' + (code - AKEYCODE_0)));
    }
    if (code >= AKEYCODE_NUMPAD_0 && code <= AKEYCODE_NUMPAD_9) {
        return "KP_" + std::to_string(code - AKEYCODE_NUMPAD_0);
    }
    if (code >= AKEYCODE_F1 && code <= AKEYCODE_F12) {
        return "F" + std::to_string(code - AKEYCODE_F1 + 1);
    }
    for (const auto& [keyName, keyCode] : kNamedKeys) {
        if (code == keyCode) {
            return std::string(keyName);
        }
    }
    return std::nullopt;
}

}  // namespace input::android
