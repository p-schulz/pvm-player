// Logic tests for the gamepad layer: PadTranslator (axes as buttons with
// hysteresis, per-action auto-repeat, dedup of D-pad key + hat, tap vs. hold,
// analog triggers and sticks) and the Android default key map, which is
// compiled here against a shim of <android/keycodes.h>.
// Run: ctest --test-dir build, or ./pad_selftest directly.

#include <android/keycodes.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "input/gamepad_input.h"
#include "input/input_action.h"
#include "input/keymap.h"
#include "platform/android/keymap_android.h"

using input::Action;
using input::InputEvent;
using input::PadAxis;
using input::PadTranslator;
using input::Phase;

namespace {

int failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

std::vector<InputEvent> take(PadTranslator& pad) {
    std::vector<InputEvent> events;
    pad.drain(events);
    return events;
}

int count(const std::vector<InputEvent>& events, Action action, Phase phase) {
    return static_cast<int>(std::count_if(events.begin(), events.end(), [&](const InputEvent& e) {
        return e.action == action && e.phase == phase;
    }));
}

bool emits(const input::KeyMap& map, int code, Action action) {
    const auto& actions = map.actionsFor(code);
    return std::find(actions.begin(), actions.end(), action) != actions.end();
}

// A small map with hand-picked codes, independent of the Android defaults.
constexpr int kBtnA = 1, kBtnY = 2, kKeyUp = 3;

PadTranslator makePad(bool withAnalog = true) {
    input::KeyMap map;
    map.bind(kBtnA, Action::Confirm);
    map.bind(kBtnY, Action::MediaInfo);
    map.bind(kBtnY, Action::FastextYellow);
    map.bind(kBtnY | input::kHoldFlag, Action::PageEntry);
    map.bind(kKeyUp, Action::Up);
    map.bind(input::kHatUp, Action::Up);
    map.bind(input::kHatDown, Action::Down);
    map.bind(input::kLeftStickLeft, Action::Left);
    return PadTranslator(map, withAnalog ? input::defaultAnalogBindings() : std::vector<input::AnalogBinding>{});
}

void testPressRepeatRelease() {
    PadTranslator pad = makePad();
    pad.keyDown(kBtnA, 0.0);
    auto events = take(pad);
    CHECK(events.size() == 1 && events[0].action == Action::Confirm && events[0].phase == Phase::Press);
    CHECK(events[0].value == 1.0f);

    pad.update(0.39);  // before the 400 ms delay
    CHECK(take(pad).empty());
    pad.update(0.40);
    events = take(pad);
    CHECK(events.size() == 1 && events[0].phase == Phase::Repeat);
    pad.update(0.45);
    CHECK(take(pad).empty());
    pad.update(0.49);  // 80 ms after the first repeat (with float slack)
    CHECK(count(take(pad), Action::Confirm, Phase::Repeat) == 1);

    pad.keyDown(kBtnA, 0.5);  // the OS's own key repeat must not re-press
    CHECK(take(pad).empty());
    pad.keyUp(kBtnA, 0.6);
    events = take(pad);
    CHECK(events.size() == 1 && events[0].phase == Phase::Release);
    pad.update(1.0);
    CHECK(take(pad).empty());  // nothing repeats once released
    pad.keyUp(kBtnA, 1.1);     // a stray up is ignored
    CHECK(take(pad).empty());
}

// The events of one press share a group (InputEvent::group), so App can apply
// the press to the screen it was made on only.
void testPressGroups() {
    input::KeyMap map;
    map.bind(kBtnA, Action::Confirm);
    map.bind(kBtnA, Action::PlayPause);
    map.bind(kBtnY, Action::MediaInfo);
    map.bind(kBtnY, Action::FastextYellow);
    map.bind(kBtnY | input::kHoldFlag, Action::PageEntry);
    PadTranslator pad(map, {});

    pad.keyDown(kBtnA, 0.0);
    auto first = take(pad);
    CHECK(first.size() == 2);
    CHECK(first[0].group != 0 && first[0].group == first[1].group);
    pad.keyUp(kBtnA, 0.1);
    take(pad);

    pad.keyDown(kBtnA, 0.2);
    auto second = take(pad);
    CHECK(second.size() == 2 && second[0].group != 0 && second[0].group != first[0].group);  // a new press, a new group
    pad.keyUp(kBtnA, 0.3);
    take(pad);

    // A deferred tap (released before the hold threshold) is one group too.
    pad.keyDown(kBtnY, 1.0);
    pad.keyUp(kBtnY, 1.1);
    auto tap = take(pad);
    CHECK(count(tap, Action::MediaInfo, Phase::Press) == 1 && count(tap, Action::FastextYellow, Phase::Press) == 1);
    uint32_t tapGroup = 0;
    for (const auto& e : tap) {
        if (e.phase == Phase::Press) {
            CHECK(e.group != 0 && (tapGroup == 0 || e.group == tapGroup));
            tapGroup = e.group;
        }
    }
}

void testTwoSourcesOfOneDirection() {
    PadTranslator pad = makePad();
    pad.keyDown(kKeyUp, 0.0);
    pad.setAxis(PadAxis::HatY, -1.0f, 0.0);
    auto events = take(pad);
    CHECK(count(events, Action::Up, Phase::Press) == 1);  // one press, not two

    pad.keyUp(kKeyUp, 0.1);
    CHECK(take(pad).empty());  // the hat still holds it
    pad.setAxis(PadAxis::HatY, 0.0f, 0.2);
    CHECK(count(take(pad), Action::Up, Phase::Release) == 1);
}

void testAxisHysteresis() {
    PadTranslator pad = makePad();
    pad.setAxis(PadAxis::HatY, -0.4f, 0.0);
    CHECK(take(pad).empty());  // below the press threshold
    pad.setAxis(PadAxis::HatY, -0.6f, 0.0);
    CHECK(count(take(pad), Action::Up, Phase::Press) == 1);
    pad.setAxis(PadAxis::HatY, -0.4f, 0.1);
    CHECK(take(pad).empty());  // between the thresholds: still held
    pad.setAxis(PadAxis::HatY, -0.29f, 0.2);
    CHECK(count(take(pad), Action::Up, Phase::Release) == 1);
    pad.setAxis(PadAxis::HatY, -0.4f, 0.3);
    CHECK(take(pad).empty());  // and it doesn't re-press without crossing 0.5 again

    // Opposite directions on one axis are independent virtual keys.
    pad.setAxis(PadAxis::HatY, 0.7f, 0.4);
    CHECK(count(take(pad), Action::Down, Phase::Press) == 1);
    pad.setAxis(PadAxis::LeftX, -0.8f, 0.4);
    CHECK(count(take(pad), Action::Left, Phase::Press) == 1);
}

void testTapVersusHold() {
    // Short press: the tap actions fire on release, the hold action never.
    PadTranslator pad = makePad();
    pad.keyDown(kBtnY, 0.0);
    CHECK(take(pad).empty());  // undecided until release or 500 ms
    pad.update(0.3);
    CHECK(take(pad).empty());
    pad.keyUp(kBtnY, 0.3);
    auto events = take(pad);
    CHECK(count(events, Action::MediaInfo, Phase::Press) == 1);
    CHECK(count(events, Action::MediaInfo, Phase::Release) == 1);
    CHECK(count(events, Action::FastextYellow, Phase::Press) == 1);
    CHECK(count(events, Action::PageEntry, Phase::Press) == 0);

    // Long press: the hold action fires once the threshold passes; the tap
    // actions never do, not even on release.
    pad.keyDown(kBtnY, 1.0);
    pad.update(1.4);
    CHECK(take(pad).empty());
    pad.update(1.5);
    events = take(pad);
    CHECK(count(events, Action::PageEntry, Phase::Press) == 1);
    CHECK(count(events, Action::MediaInfo, Phase::Press) == 0);
    pad.keyUp(kBtnY, 1.8);
    events = take(pad);
    CHECK(count(events, Action::PageEntry, Phase::Release) == 1);
    CHECK(count(events, Action::MediaInfo, Phase::Press) == 0);
    CHECK(count(events, Action::FastextYellow, Phase::Press) == 0);
}

void testAnalogTrigger() {
    PadTranslator pad = makePad();
    pad.setAxis(PadAxis::RightTrigger, 0.05f, 0.0);  // inside the dead zone
    pad.update(0.0);
    CHECK(take(pad).empty());

    pad.setAxis(PadAxis::RightTrigger, 0.55f, 0.0);  // depth 0.5 past the dead zone
    pad.update(0.0);
    auto events = take(pad);
    CHECK(events.size() == 1 && events[0].action == Action::SeekFwd && events[0].phase == Phase::Press);
    CHECK(std::fabs(events[0].value - 0.25f) < 0.001f);  // depth 0.5 x scale 0.5

    pad.update(0.05);
    CHECK(take(pad).empty());  // ticks every 80 ms, not every frame
    pad.setAxis(PadAxis::RightTrigger, 1.0f, 0.06);
    pad.update(0.08);
    events = take(pad);
    CHECK(events.size() == 1 && events[0].phase == Phase::Repeat);
    CHECK(std::fabs(events[0].value - 0.5f) < 0.001f);  // full pull

    pad.setAxis(PadAxis::RightTrigger, 0.0f, 0.1);
    pad.update(0.1);
    CHECK(count(take(pad), Action::SeekFwd, Phase::Release) == 1);
    pad.update(0.5);
    CHECK(take(pad).empty());
}

void testRightStickVolumeAndScroll() {
    PadTranslator pad = makePad();
    pad.setAxis(PadAxis::RightY, -0.2f, 0.0);  // stick drift: inside the dead zone
    pad.update(0.0);
    CHECK(take(pad).empty());

    pad.setAxis(PadAxis::RightY, -1.0f, 0.0);  // pushed fully up (Android: up is negative)
    pad.update(0.0);
    auto events = take(pad);
    CHECK(count(events, Action::VolumeUp, Phase::Press) == 1);
    CHECK(count(events, Action::ScrollUp, Phase::Press) == 1);
    CHECK(count(events, Action::VolumeDown, Phase::Press) == 0);
    for (const auto& e : events) {
        CHECK(std::fabs(e.value - (e.action == Action::VolumeUp ? 0.5f : 1.0f)) < 0.001f);
    }

    pad.setAxis(PadAxis::RightY, 0.0f, 0.1);
    pad.update(0.1);
    events = take(pad);
    CHECK(count(events, Action::VolumeUp, Phase::Release) == 1);
    CHECK(count(events, Action::ScrollUp, Phase::Release) == 1);
}

void testReleaseAll() {
    PadTranslator pad = makePad();
    pad.keyDown(kBtnA, 0.0);
    pad.setAxis(PadAxis::HatY, -1.0f, 0.0);
    pad.setAxis(PadAxis::RightTrigger, 1.0f, 0.0);
    pad.update(0.0);
    take(pad);

    pad.releaseAll();
    auto events = take(pad);
    CHECK(count(events, Action::Confirm, Phase::Release) == 1);
    CHECK(count(events, Action::Up, Phase::Release) == 1);
    CHECK(count(events, Action::SeekFwd, Phase::Release) == 1);
    pad.update(5.0);
    CHECK(take(pad).empty());
    pad.keyUp(kBtnA, 5.0);  // the late key-up is harmless
    CHECK(take(pad).empty());
}

// ---- the Android default map ------------------------------------------------

void testAndroidDefaults() {
    const input::KeyMap map = input::android::defaultKeyMap();
    CHECK(emits(map, AKEYCODE_DPAD_UP, Action::Up));
    CHECK(emits(map, input::kHatUp, Action::Up));
    CHECK(emits(map, input::kLeftStickUp, Action::Up));
    CHECK(emits(map, AKEYCODE_BUTTON_A, Action::Confirm));
    CHECK(emits(map, AKEYCODE_BUTTON_A, Action::PlayPause));
    CHECK(emits(map, AKEYCODE_BUTTON_B, Action::Back));
    CHECK(emits(map, AKEYCODE_BACK, Action::Back));
    CHECK(emits(map, AKEYCODE_BUTTON_X, Action::ToggleCrt));
    CHECK(emits(map, AKEYCODE_BUTTON_Y, Action::FastextYellow));
    CHECK(emits(map, AKEYCODE_BUTTON_Y | input::kHoldFlag, Action::PageEntry));
    CHECK(emits(map, AKEYCODE_BUTTON_L1, Action::PageUp));
    CHECK(emits(map, AKEYCODE_BUTTON_R1, Action::SeekFwd));
    CHECK(emits(map, AKEYCODE_BUTTON_START, Action::OpenSettings));
    CHECK(emits(map, AKEYCODE_BUTTON_START, Action::FastextBlue));
    CHECK(emits(map, AKEYCODE_BUTTON_SELECT, Action::NextSection));
    CHECK(emits(map, AKEYCODE_BUTTON_SELECT, Action::AspectRatio));
    CHECK(emits(map, AKEYCODE_BUTTON_THUMBR, Action::VideoScale));
    CHECK(emits(map, AKEYCODE_MEDIA_PLAY_PAUSE, Action::PlayPause));
    CHECK(emits(map, AKEYCODE_ENTER, Action::Confirm));  // keyboards keep working
    // The volume keys stay with the system; L2/R2 are analog only.
    CHECK(map.actionsFor(AKEYCODE_VOLUME_UP).empty());
    CHECK(map.actionsFor(AKEYCODE_BUTTON_L2).empty());
}

// Each screen reacts to a subset of actions (mirroring App::handleInput). Two
// actions from one button in the same subset would fire twice there. Taps and
// holds are separate presses, so they're checked separately.
void testAndroidNoScreenSeesTwoActionsFromOneButton() {
    const std::vector<std::pair<const char*, std::set<Action>>> screens = {
        {"root", {Action::Up, Action::Down, Action::Confirm, Action::Back, Action::OpenSettings, Action::NextSection,
                  Action::ToggleCrt}},
        {"browser", {Action::Up, Action::Down, Action::PageUp, Action::PageDown, Action::Confirm, Action::Back,
                     Action::BackSoft, Action::OpenSettings, Action::ToggleCrt}},
        {"settings", {Action::Up, Action::Down, Action::PageUp, Action::PageDown, Action::Left, Action::Right,
                      Action::Confirm, Action::Back, Action::BackSoft, Action::ToggleCrt}},
        {"picker", {Action::Up, Action::Down, Action::PageUp, Action::PageDown, Action::Confirm, Action::Back,
                    Action::BackSoft, Action::ToggleCrt}},
        {"playing", {Action::ToggleOsd, Action::PlayPause, Action::Left, Action::Right, Action::SeekBack,
                     Action::SeekFwd, Action::VideoScale, Action::AspectRatio, Action::MediaInfo, Action::VolumeDown,
                     Action::VolumeUp, Action::Up, Action::Down, Action::Back, Action::ToggleCrt}},
        {"playing-osd", {Action::ToggleOsd, Action::Up, Action::Down, Action::Left, Action::Right, Action::Confirm,
                         Action::Back, Action::ToggleCrt}},
        {"teletext", {Action::Up, Action::Down, Action::ScrollUp, Action::ScrollDown, Action::Left, Action::Right,
                      Action::FastextRed, Action::FastextGreen, Action::FastextYellow, Action::FastextBlue,
                      Action::Confirm, Action::Back, Action::BackSoft, Action::NextSection, Action::PageEntry,
                      Action::ToggleCrt, Action::Digit0, Action::Digit1, Action::Digit2, Action::Digit3,
                      Action::Digit4, Action::Digit5, Action::Digit6, Action::Digit7, Action::Digit8,
                      Action::Digit9}},
        {"spinner", {Action::Up, Action::Down, Action::Left, Action::Right, Action::Confirm, Action::Back,
                     Action::BackSoft, Action::Digit0, Action::Digit1, Action::Digit2, Action::Digit3,
                     Action::Digit4, Action::Digit5, Action::Digit6, Action::Digit7, Action::Digit8,
                     Action::Digit9}},
    };
    const input::KeyMap map = input::android::defaultKeyMap();
    for (int code = 0; code < input::kVirtualEnd; ++code) {
        for (const int variant : {code, code | input::kHoldFlag}) {
            const auto& actions = map.actionsFor(variant);
            for (const auto& [name, handled] : screens) {
                int n = 0;
                for (Action a : actions) n += handled.count(a) ? 1 : 0;
                if (n > 1) {
                    std::fprintf(stderr, "code %d%s emits %d actions on screen %s\n", code,
                                 variant != code ? " (hold)" : "", n, name);
                }
                CHECK(n <= 1);
            }
        }
        if (code == 400) code = input::kVirtualBase - 1;  // skip the unused gap; virtual keys follow
    }
}

void testAndroidNames() {
    using input::android::codeFromName;
    CHECK(codeFromName("pad_y") == AKEYCODE_BUTTON_Y);
    CHECK(codeFromName("BUTTON_Y") == AKEYCODE_BUTTON_Y);
    CHECK(codeFromName("HAT_UP") == input::kHatUp);
    CHECK(codeFromName("lstick_left") == input::kLeftStickLeft);
    CHECK(codeFromName("HOLD_PAD_Y") == (AKEYCODE_BUTTON_Y | input::kHoldFlag));
    CHECK(codeFromName("enter") == AKEYCODE_ENTER);
    CHECK(codeFromName("a") == AKEYCODE_A);
    CHECK(codeFromName("kp_3") == AKEYCODE_NUMPAD_3);
    CHECK(!codeFromName("HOLD_HOLD_PAD_Y"));
    CHECK(!codeFromName("HOLD_NOPE"));
    CHECK(!codeFromName("PAD_Q"));

    // keys.cfg on Android: swap A and B (Nintendo-style labels), add a hold.
    input::KeyMap map = input::android::defaultKeyMap();
    std::istringstream cfg(
        "confirm = PAD_B, ENTER\n"
        "back = PAD_A, ESCAPE, BACK\n"
        "page_entry = hold_pad_x\n");
    const auto warnings = map.loadOverrides(cfg, input::android::codeFromName, "keys.cfg");
    CHECK(warnings.empty());
    CHECK(emits(map, AKEYCODE_BUTTON_B, Action::Confirm));
    CHECK(!emits(map, AKEYCODE_BUTTON_B, Action::Back));
    CHECK(emits(map, AKEYCODE_BUTTON_A, Action::Back));
    CHECK(emits(map, AKEYCODE_BUTTON_A, Action::PlayPause));  // untouched
    CHECK(emits(map, AKEYCODE_BUTTON_X | input::kHoldFlag, Action::PageEntry));
    CHECK(!emits(map, AKEYCODE_BUTTON_Y | input::kHoldFlag, Action::PageEntry));  // replaced
}

}  // namespace

int main() {
    testPressRepeatRelease();
    testPressGroups();
    testTwoSourcesOfOneDirection();
    testAxisHysteresis();
    testTapVersusHold();
    testAnalogTrigger();
    testRightStickVolumeAndScroll();
    testReleaseAll();
    testAndroidDefaults();
    testAndroidNoScreenSeesTwoActionsFromOneButton();
    testAndroidNames();
    if (failures == 0) {
        std::printf("pad_selftest: all checks passed\n");
        return 0;
    }
    std::printf("pad_selftest: %d check(s) failed\n", failures);
    return 1;
}
