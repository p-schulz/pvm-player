#include "input/gamepad_input.h"

#include <algorithm>
#include <cmath>

namespace input {

std::vector<AnalogBinding> defaultAnalogBindings() {
    constexpr float kTriggerDeadzone = 0.10f;
    constexpr float kStickDeadzone = 0.25f;
    // A full trigger pull seeks 5 s per 80 ms tick (SeekBack/Fwd are 10 s x
    // value), about a minute of media per second; volume at full deflection
    // moves 2.5 points per tick (5 x value); scrolling one link per tick.
    return {
        {PadAxis::LeftTrigger, +1, Action::SeekBack, 0.5f, kTriggerDeadzone},
        {PadAxis::RightTrigger, +1, Action::SeekFwd, 0.5f, kTriggerDeadzone},
        {PadAxis::RightY, -1, Action::VolumeUp, 0.5f, kStickDeadzone},
        {PadAxis::RightY, +1, Action::VolumeDown, 0.5f, kStickDeadzone},
        {PadAxis::RightY, -1, Action::ScrollUp, 1.0f, kStickDeadzone},
        {PadAxis::RightY, +1, Action::ScrollDown, 1.0f, kStickDeadzone},
    };
}

PadTranslator::PadTranslator(KeyMap keyMap, std::vector<AnalogBinding> analog)
    : keyMap_(std::move(keyMap)), analog_(std::move(analog)), analogState_(analog_.size()) {}

PadTranslator::HeldCode& PadTranslator::codeState(int code) {
    for (auto& entry : codes_) {
        if (entry.first == code) {
            return entry.second;
        }
    }
    codes_.emplace_back(code, HeldCode{});
    return codes_.back().second;
}

void PadTranslator::emit(Action action, Phase phase, float value) {
    out_.push_back(InputEvent{action, phase, value, currentGroup_});
}

void PadTranslator::pressAction(Action action, double now) {
    HeldAction& held = held_[static_cast<size_t>(action)];
    if (held.sources++ == 0) {
        held.nextRepeat = now + kRepeatDelaySeconds;
        emit(action, Phase::Press);
    }
}

void PadTranslator::releaseAction(Action action) {
    HeldAction& held = held_[static_cast<size_t>(action)];
    if (held.sources > 0 && --held.sources == 0) {
        emit(action, Phase::Release);
    }
}

void PadTranslator::keyDown(int code, double now) {
    HeldCode& state = codeState(code);
    if (state.down) {
        return;  // the OS's own auto-repeat, or a duplicate
    }
    state = HeldCode{};
    state.down = true;
    state.downTime = now;

    if (!keyMap_.actionsFor(code | kHoldFlag).empty()) {
        // Undecided until released (tap) or kHoldSeconds pass (hold).
        state.holdPending = true;
        return;
    }
    beginGroup();
    for (const Action action : keyMap_.actionsFor(code)) {
        pressAction(action, now);
    }
    endGroup();
}

void PadTranslator::keyUp(int code, double now) {
    HeldCode& state = codeState(code);
    if (!state.down) {
        return;
    }
    const bool wasPending = state.holdPending;
    const bool holdFired = state.holdFired;
    state = HeldCode{};

    if (wasPending) {
        // Short press: the deferred tap, as a press and a release.
        beginGroup();
        for (const Action action : keyMap_.actionsFor(code)) {
            pressAction(action, now);
        }
        endGroup();
        for (const Action action : keyMap_.actionsFor(code)) {
            releaseAction(action);
        }
    } else if (holdFired) {
        for (const Action action : keyMap_.actionsFor(code | kHoldFlag)) {
            releaseAction(action);
        }
    } else {
        for (const Action action : keyMap_.actionsFor(code)) {
            releaseAction(action);
        }
    }
}

void PadTranslator::setVirtualKey(int code, bool down, double now) {
    bool& current = virtualDown_[static_cast<size_t>(code - kVirtualBase)];
    if (current == down) {
        return;
    }
    current = down;
    if (down) {
        keyDown(code, now);
    } else {
        keyUp(code, now);
    }
}

void PadTranslator::setAxis(PadAxis axis, float value, double now) {
    axes_[static_cast<size_t>(axis)] = value;

    // Digital use of the left stick and the hat, with hysteresis.
    auto direction = [&](int negativeKey, int positiveKey) {
        const bool negativeDown = virtualDown_[static_cast<size_t>(negativeKey - kVirtualBase)];
        const bool positiveDown = virtualDown_[static_cast<size_t>(positiveKey - kVirtualBase)];
        setVirtualKey(negativeKey, value <= -(negativeDown ? kAxisRelease : kAxisPress), now);
        setVirtualKey(positiveKey, value >= (positiveDown ? kAxisRelease : kAxisPress), now);
    };
    switch (axis) {
        case PadAxis::LeftX:
            direction(kLeftStickLeft, kLeftStickRight);
            break;
        case PadAxis::LeftY:
            direction(kLeftStickUp, kLeftStickDown);
            break;
        case PadAxis::HatX:
            direction(kHatLeft, kHatRight);
            break;
        case PadAxis::HatY:
            direction(kHatUp, kHatDown);
            break;
        default:
            break;  // analog-only axes are read in update()
    }
}

float PadTranslator::analogDepth(const AnalogBinding& binding) const {
    const float raw = axes_[static_cast<size_t>(binding.axis)] * static_cast<float>(binding.sign);
    if (raw <= binding.deadzone) {
        return 0.0f;
    }
    return std::min(1.0f, (raw - binding.deadzone) / (1.0f - binding.deadzone));
}

void PadTranslator::updateAnalog(size_t index, double now) {
    const AnalogBinding& binding = analog_[index];
    AnalogState& state = analogState_[index];
    const float depth = analogDepth(binding);

    if (depth <= 0.0f) {
        if (state.active) {
            state.active = false;
            emit(binding.action, Phase::Release);
        }
        return;
    }
    const float value = depth * binding.scale;
    if (!state.active) {
        state.active = true;
        state.nextTick = now + kRepeatIntervalSeconds;
        emit(binding.action, Phase::Press, value);
    } else if (now >= state.nextTick) {
        state.nextTick = now + kRepeatIntervalSeconds;
        emit(binding.action, Phase::Repeat, value);
    }
}

void PadTranslator::update(double now) {
    // Long presses that have been held long enough.
    for (auto& [code, state] : codes_) {
        if (state.down && state.holdPending && now - state.downTime >= kHoldSeconds) {
            state.holdPending = false;
            state.holdFired = true;
            beginGroup();
            for (const Action action : keyMap_.actionsFor(code | kHoldFlag)) {
                pressAction(action, now);
            }
            endGroup();
        }
    }

    // Auto-repeat of held actions.
    for (int i = 0; i < kActionCount; ++i) {
        HeldAction& held = held_[static_cast<size_t>(i)];
        if (held.sources > 0 && now >= held.nextRepeat) {
            held.nextRepeat = now + kRepeatIntervalSeconds;
            emit(static_cast<Action>(i), Phase::Repeat);
        }
    }

    for (size_t i = 0; i < analog_.size(); ++i) {
        updateAnalog(i, now);
    }
}

void PadTranslator::releaseAll() {
    for (int i = 0; i < kActionCount; ++i) {
        HeldAction& held = held_[static_cast<size_t>(i)];
        if (held.sources > 0) {
            held.sources = 0;
            emit(static_cast<Action>(i), Phase::Release);
        }
    }
    for (size_t i = 0; i < analog_.size(); ++i) {
        if (analogState_[i].active) {
            analogState_[i].active = false;
            emit(analog_[i].action, Phase::Release);
        }
    }
    codes_.clear();
    virtualDown_.fill(false);
    axes_.fill(0.0f);
}

void PadTranslator::drain(std::vector<InputEvent>& out) {
    out.insert(out.end(), out_.begin(), out_.end());
    out_.clear();
}

}  // namespace input
