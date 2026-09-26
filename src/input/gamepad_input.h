#pragma once

// Platform-free gamepad input: turns raw button and axis changes into
// InputEvents. The platform feeds it key up/down events (codes as in its
// KeyMap) and normalised axis values, and calls update() once per frame with
// the current time; it then drains the resulting events. No windowing or
// Android headers here, so the whole state machine is unit-tested on desktop
// (tests/pad_selftest.cpp).
//
// What it adds on top of a plain KeyMap lookup:
//  * axes as buttons: the left stick and D-pad hat become virtual keys (see
//    VirtualKey) with press/release hysteresis, so they bind like any button;
//  * one auto-repeat timer per *action*, so a gamepad (which has no key
//    repeat) gets 400 ms / 80 ms repeats, and two sources of one direction
//    (a D-pad key and the hat axis, which many devices report together) act
//    as one held direction rather than doubling;
//  * hold bindings: a code with a hold binding (kHoldFlag) fires its tap
//    actions on a short press, released before kHoldSeconds, and its hold
//    actions when kept down that long instead;
//  * analog bindings: triggers and the right stick emit their action on a
//    fixed tick while deflected, with InputEvent::value carrying the
//    (dead-zone-corrected) depth times a scale.

#include <array>
#include <vector>

#include "input/input_action.h"
#include "input/keymap.h"

namespace input {

// Added to a KeyMap code to bind the *hold* (long press) of that code.
constexpr int kHoldFlag = 1 << 24;

// Codes for buttons that exist only as axis positions; disjoint from the
// AKEYCODE_* range and from kHoldFlag.
enum VirtualKey : int {
    kVirtualBase = 1 << 20,
    kHatUp = kVirtualBase,
    kHatDown,
    kHatLeft,
    kHatRight,
    kLeftStickUp,
    kLeftStickDown,
    kLeftStickLeft,
    kLeftStickRight,
    kVirtualEnd,
};

// Axes in a platform-neutral convention: X grows right, Y grows *down*
// (as on Android), triggers run 0 (released) .. 1 (fully pressed).
enum class PadAxis : uint8_t {
    LeftX,
    LeftY,
    RightX,
    RightY,
    HatX,
    HatY,
    LeftTrigger,
    RightTrigger,
    Count
};

// An axis position that emits `action` (repeatedly, see class comment) while
// deflected past `deadzone` in direction `sign` (+1: positive values, -1:
// negative). event.value = depth * scale, depth being 0..1 over the range
// beyond the dead zone.
struct AnalogBinding {
    PadAxis axis;
    int sign;
    Action action;
    float scale;
    float deadzone;
};

// Trigger and right-stick behaviour of the default handheld layout: L2/R2
// seek back/forward, the right stick's vertical axis is volume (playback) and
// scroll (teletext) at once -- each screen reacts to only one of them.
std::vector<AnalogBinding> defaultAnalogBindings();

class PadTranslator {
public:
    static constexpr double kRepeatDelaySeconds = 0.4;
    static constexpr double kRepeatIntervalSeconds = 0.08;
    static constexpr double kHoldSeconds = 0.5;
    static constexpr float kAxisPress = 0.5f;    // |v| above this presses a virtual key...
    static constexpr float kAxisRelease = 0.3f;  // ...and below this releases it

    PadTranslator(KeyMap keyMap, std::vector<AnalogBinding> analog);

    // A button/key went down or up. Codes as bound in the KeyMap (raw
    // platform key codes, or VirtualKey values).
    void keyDown(int code, double now);
    void keyUp(int code, double now);

    // A new value for an axis (called on every motion event).
    void setAxis(PadAxis axis, float value, double now);

    // Advances timers -- repeats, hold detection, analog ticks -- to `now`.
    void update(double now);

    // Releases everything held (emitting Release events), e.g. when the app
    // loses focus and key-up events will never arrive.
    void releaseAll();

    // Moves the accumulated events to `out`.
    void drain(std::vector<InputEvent>& out);

    const KeyMap& keyMap() const { return keyMap_; }

private:
    struct HeldAction {
        int sources = 0;         // buttons currently holding this action
        double nextRepeat = 0.0;
    };
    struct HeldCode {
        bool down = false;
        bool holdPending = false;  // waiting to see whether this becomes a long press
        bool holdFired = false;
        double downTime = 0.0;
    };
    struct AnalogState {
        bool active = false;
        double nextTick = 0.0;
    };

    void pressAction(Action action, double now);
    void releaseAction(Action action);
    void emit(Action action, Phase phase, float value = 1.0f);
    // Events emitted between beginGroup() and endGroup() belong to one press
    // (see InputEvent::group).
    void beginGroup() { currentGroup_ = nextGroup_++; }
    void endGroup() { currentGroup_ = 0; }
    void setVirtualKey(int code, bool down, double now);
    void updateAnalog(size_t index, double now);
    float analogDepth(const AnalogBinding& binding) const;

    KeyMap keyMap_;
    std::vector<AnalogBinding> analog_;
    std::vector<AnalogState> analogState_;
    std::array<float, static_cast<size_t>(PadAxis::Count)> axes_{};
    std::array<bool, kVirtualEnd - kVirtualBase> virtualDown_{};
    std::array<HeldAction, kActionCount> held_{};
    std::vector<std::pair<int, HeldCode>> codes_;  // few buttons are ever down at once
    std::vector<InputEvent> out_;
    uint32_t nextGroup_ = 1;
    uint32_t currentGroup_ = 0;

    HeldCode& codeState(int code);
};

}  // namespace input
