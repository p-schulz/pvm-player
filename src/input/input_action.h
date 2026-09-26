#pragma once

// Platform-free input vocabulary. App reacts only to these actions; each
// platform translates its own raw events (GLFW keys, Android key/motion
// events, a remote, a test script) into them via a KeyMap. See
// docs/android-port-plan (Phase 1).

#include <cstdint>
#include <optional>
#include <string_view>

namespace input {

enum class Action : uint8_t {
    // Navigation
    Up,
    Down,
    Left,
    Right,
    Confirm,   // activate the selection (Enter)
    Back,      // leave: close OSD, stop playback, leave a screen, quit at the root menu (Esc)
    BackSoft,  // go up a level, but never stop playback or quit (Backspace)

    // Playback
    PlayPause,
    SeekBack,  // explicit seek; Left/Right also seek while playing
    SeekFwd,
    VolumeDown,
    VolumeUp,
    ToggleOsd,    // in-playback OSD menu
    MediaInfo,    // media info overlay
    VideoScale,   // cycle video scale mode
    AspectRatio,  // cycle aspect-ratio override

    // Global
    ToggleCrt,

    // Teletext colour keys
    FastextRed,     // previous page
    FastextGreen,   // next page
    FastextYellow,  // index page
    FastextBlue,    // refresh

    // Direct page entry
    Digit0,
    Digit1,
    Digit2,
    Digit3,
    Digit4,
    Digit5,
    Digit6,
    Digit7,
    Digit8,
    Digit9,

    Count
};

// Press = first event of a physical press; Repeat = auto-repeat while held
// (keyboard auto-repeat, or a platform repeat timer for gamepads); Release =
// let go. Handlers that must fire once per press check for Press.
enum class Phase : uint8_t { Press, Repeat, Release };

struct InputEvent {
    Action action;
    Phase phase;
    float value = 1.0f;  // analog magnitude (triggers, sticks); 1 for buttons
};

constexpr int kActionCount = static_cast<int>(Action::Count);

// Stable snake_case names ("play_pause", "fastext_red", "digit_7"), used in
// keys.cfg bindings and test logs.
const char* actionName(Action action);

// Inverse of actionName(); case-insensitive, accepts '-' for '_'.
std::optional<Action> actionFromName(std::string_view name);

// 0..9 for Digit0..Digit9, else -1.
int digitOf(Action action);

}  // namespace input
