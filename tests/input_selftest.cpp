// Logic tests for the action layer: default GLFW bindings, keys.cfg
// overrides, and the invariant that no screen sees two actions from one key.
// Run: ctest --test-dir build, or ./input_selftest directly.

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdio>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "input/input_action.h"
#include "input/keymap.h"
#include "platform/glfw/keymap_glfw.h"

using input::Action;

namespace {

int failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

bool emits(const input::KeyMap& map, int code, Action action) {
    const auto& actions = map.actionsFor(code);
    return std::find(actions.begin(), actions.end(), action) != actions.end();
}

void testNames() {
    for (int i = 0; i < input::kActionCount; ++i) {
        const auto action = static_cast<Action>(i);
        const auto roundTrip = input::actionFromName(input::actionName(action));
        CHECK(roundTrip && *roundTrip == action);
    }
    CHECK(input::actionFromName("PLAY-PAUSE") == Action::PlayPause);
    CHECK(!input::actionFromName("jump"));
    CHECK(input::digitOf(Action::Digit7) == 7);
    CHECK(input::digitOf(Action::Confirm) == -1);
}

// The bindings the app used before the action layer (see git history of
// App::handleKey). Any change here is a user-visible behaviour change.
void testDefaultsMatchLegacyKeys() {
    const input::KeyMap map = input::glfw::defaultKeyMap();
    CHECK(emits(map, GLFW_KEY_UP, Action::Up));
    CHECK(emits(map, GLFW_KEY_DOWN, Action::Down));
    CHECK(emits(map, GLFW_KEY_LEFT, Action::Left));
    CHECK(emits(map, GLFW_KEY_RIGHT, Action::Right));
    CHECK(emits(map, GLFW_KEY_ENTER, Action::Confirm));
    CHECK(emits(map, GLFW_KEY_KP_ENTER, Action::Confirm));
    CHECK(emits(map, GLFW_KEY_ESCAPE, Action::Back));
    CHECK(emits(map, GLFW_KEY_BACKSPACE, Action::BackSoft));
    CHECK(!emits(map, GLFW_KEY_BACKSPACE, Action::Back));  // must never quit or stop playback
    CHECK(emits(map, GLFW_KEY_SPACE, Action::PlayPause));
    CHECK(emits(map, GLFW_KEY_F, Action::PlayPause));
    CHECK(emits(map, GLFW_KEY_C, Action::ToggleCrt));
    CHECK(emits(map, GLFW_KEY_M, Action::ToggleOsd));
    CHECK(emits(map, GLFW_KEY_M, Action::FastextYellow));
    CHECK(emits(map, GLFW_KEY_I, Action::ToggleOsd));
    CHECK(emits(map, GLFW_KEY_R, Action::MediaInfo));
    CHECK(emits(map, GLFW_KEY_R, Action::FastextRed));
    CHECK(emits(map, GLFW_KEY_G, Action::FastextGreen));
    CHECK(emits(map, GLFW_KEY_Y, Action::FastextYellow));
    CHECK(emits(map, GLFW_KEY_B, Action::AspectRatio));
    CHECK(emits(map, GLFW_KEY_B, Action::FastextBlue));
    CHECK(emits(map, GLFW_KEY_V, Action::VideoScale));
    CHECK(emits(map, GLFW_KEY_COMMA, Action::VolumeDown));
    CHECK(emits(map, GLFW_KEY_PERIOD, Action::VolumeUp));
    CHECK(emits(map, GLFW_KEY_F1, Action::FastextRed));
    CHECK(emits(map, GLFW_KEY_F2, Action::FastextGreen));
    CHECK(emits(map, GLFW_KEY_F3, Action::FastextYellow));
    CHECK(emits(map, GLFW_KEY_F4, Action::FastextBlue));
    for (int d = 0; d <= 9; ++d) {
        const auto digit = static_cast<Action>(static_cast<int>(Action::Digit0) + d);
        CHECK(emits(map, GLFW_KEY_0 + d, digit));
        CHECK(emits(map, GLFW_KEY_KP_0 + d, digit));
    }
    CHECK(map.actionsFor(GLFW_KEY_Q).empty());
}

// Each screen reacts to a subset of actions. A key emitting two actions from
// the same subset would fire twice there -- e.g. binding M to both
// FastextYellow and FastextBlue. Mirrors the cases in App::handleInput.
void testNoScreenSeesTwoActionsFromOneKey() {
    const std::vector<std::pair<const char*, std::set<Action>>> screens = {
        {"root", {Action::Up, Action::Down, Action::Confirm, Action::Back, Action::ToggleCrt}},
        {"browser", {Action::Up, Action::Down, Action::Confirm, Action::Back, Action::BackSoft, Action::ToggleCrt}},
        {"settings",
         {Action::Up, Action::Down, Action::Left, Action::Right, Action::Confirm, Action::Back, Action::BackSoft,
          Action::ToggleCrt}},
        {"playing",
         {Action::ToggleOsd, Action::PlayPause, Action::Left, Action::Right, Action::SeekBack, Action::SeekFwd,
          Action::VideoScale, Action::AspectRatio, Action::MediaInfo, Action::VolumeDown, Action::VolumeUp,
          Action::Back, Action::Up, Action::Down, Action::Confirm, Action::ToggleCrt}},
        {"teletext",
         {Action::Up, Action::Down, Action::Left, Action::Right, Action::FastextRed, Action::FastextGreen,
          Action::FastextYellow, Action::FastextBlue, Action::Confirm, Action::Back, Action::BackSoft,
          Action::ToggleCrt, Action::Digit0, Action::Digit1, Action::Digit2, Action::Digit3, Action::Digit4,
          Action::Digit5, Action::Digit6, Action::Digit7, Action::Digit8, Action::Digit9}},
    };
    const input::KeyMap map = input::glfw::defaultKeyMap();
    for (int code = 0; code <= GLFW_KEY_LAST; ++code) {
        const auto& actions = map.actionsFor(code);
        for (const auto& [name, handled] : screens) {
            int count = 0;
            for (Action a : actions) count += handled.count(a) ? 1 : 0;
            if (count > 1) {
                std::fprintf(stderr, "key %d emits %d actions on screen %s\n", code, count, name);
            }
            CHECK(count <= 1);
        }
    }
}

void testCodeFromName() {
    using input::glfw::codeFromName;
    CHECK(codeFromName("a") == GLFW_KEY_A);
    CHECK(codeFromName("7") == GLFW_KEY_7);
    CHECK(codeFromName("kp_3") == GLFW_KEY_KP_3);
    CHECK(codeFromName("F12") == GLFW_KEY_F12);
    CHECK(codeFromName("escape") == GLFW_KEY_ESCAPE);
    CHECK(codeFromName("SCANCODE_122") == input::glfw::kScancodeBase + 0x122);
    CHECK(!codeFromName("F0"));
    CHECK(!codeFromName("F1x"));
    CHECK(!codeFromName("NOPE"));
    CHECK(input::glfw::inputCode(GLFW_KEY_A, 38) == GLFW_KEY_A);
    CHECK(input::glfw::inputCode(GLFW_KEY_UNKNOWN, 0x122) == input::glfw::kScancodeBase + 0x122);
    CHECK(input::glfw::phaseFromGlfwAction(GLFW_REPEAT) == input::Phase::Repeat);
}

void testOverrides() {
    input::KeyMap map = input::glfw::defaultKeyMap();
    std::istringstream cfg(
        "# comment\n"
        "\n"
        "confirm = SPACE, kp_enter\n"
        "toggle_crt =\n"
        "bogus = A\n"
        "back = ESCAPE, NOPE\n"
        "no equals here\n");
    const auto warnings = map.loadOverrides(cfg, input::glfw::codeFromName, "keys.cfg");
    CHECK(warnings.size() == 3);
    CHECK(emits(map, GLFW_KEY_SPACE, Action::Confirm));
    CHECK(emits(map, GLFW_KEY_SPACE, Action::PlayPause));  // other actions on the key untouched
    CHECK(emits(map, GLFW_KEY_KP_ENTER, Action::Confirm));
    CHECK(!emits(map, GLFW_KEY_ENTER, Action::Confirm));   // replaced, not added
    CHECK(map.codesFor(Action::ToggleCrt).empty());        // empty rhs unbinds
    CHECK(emits(map, GLFW_KEY_ESCAPE, Action::Back));      // bad line leaves defaults
    if (!warnings.empty()) {
        CHECK(warnings[0].rfind("keys.cfg:5: ", 0) == 0);
    }
}

}  // namespace

int main() {
    testNames();
    testDefaultsMatchLegacyKeys();
    testNoScreenSeesTwoActionsFromOneKey();
    testCodeFromName();
    testOverrides();
    if (failures == 0) {
        std::printf("input_selftest: all checks passed\n");
        return 0;
    }
    std::printf("input_selftest: %d check(s) failed\n", failures);
    return 1;
}
