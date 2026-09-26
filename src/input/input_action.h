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
    PageUp,    // move a list selection by a page (gamepad shoulder buttons)
    PageDown,
    ScrollUp,  // continuous scroll through a page's links; analog, see InputEvent::value
    ScrollDown,

    // Playback
    PlayPause,
    SeekBack,  // explicit seek, 10 s x value; Left/Right also seek (5 s) while playing
    SeekFwd,
    VolumeDown,  // 5 points x value
    VolumeUp,
    ToggleOsd,    // in-playback OSD menu
    MediaInfo,    // media info overlay
    VideoScale,   // cycle video scale mode
    AspectRatio,  // cycle aspect-ratio override

    // Global
    ToggleCrt,
    OpenSettings,  // menus: open the settings screen (gamepad Start)
    NextSection,   // teletext: cycle NEWS/TAGESSCHAU/ARD/ZDF; root menu: jump into the next one

    // Teletext colour keys
    FastextRed,     // previous page
    FastextGreen,   // next page
    FastextYellow,  // index page
    FastextBlue,    // refresh
    PageEntry,      // open the page-number spinner (direct entry without digit keys)

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
    // Analog magnitude for actions driven by triggers and sticks; 1 for
    // buttons. Handlers that take a continuous quantity scale by it (seek
    // seconds, volume points) or accumulate it (scroll steps); the rate
    // comes from the platform emitting Repeat events on a timer while the
    // axis is deflected, so "speed proportional to depth" needs no extra state.
    float value = 1.0f;

    // Events produced by one physical press (a button bound to several
    // actions, one per screen it means something on) share a non-zero group;
    // 0 means "on its own". App uses it to apply such a press to the screen
    // it was made on only: if the first action changes the screen (Confirm
    // starting playback), the rest (the same button's Play/Pause) are not
    // applied to the new one.
    uint32_t group = 0;
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
