#pragma once

// Android side of the action layer: key codes (AKEYCODE_*) -> actions.
// KeyMap codes on Android are the raw AKEYCODE_* values.

#include <optional>
#include <string_view>

#include "input/input_action.h"
#include "input/keymap.h"

namespace input::android {

// AKEY_EVENT_ACTION_DOWN, or a repeated DOWN (repeatCount > 0) -> Press /
// Repeat; UP -> Release. Anything else (ACTION_MULTIPLE) -> nullopt.
std::optional<Phase> phaseFromKeyEvent(int akeyAction, int repeatCount);

// Default bindings: the handheld's D-pad and face buttons, plus a
// keyboard layout matching the desktop one (a USB/Bluetooth keyboard just
// works). Analog sticks and triggers are not covered here yet.
KeyMap defaultKeyMap();

// Key names for keys.cfg: keyboard names as on desktop ("A".."Z", "0".."9",
// "F1".."F12", "ENTER", "ESCAPE", "SPACE", "BACKSPACE", "KP_0".."KP_9", ...)
// plus the gamepad/D-pad names "DPAD_UP", "DPAD_DOWN", "DPAD_LEFT",
// "DPAD_RIGHT", "DPAD_CENTER", "BUTTON_A", "BUTTON_B", "BUTTON_X",
// "BUTTON_Y", "BUTTON_L1", "BUTTON_R1", "BUTTON_START", "BUTTON_SELECT",
// "BACK". Case-insensitive.
std::optional<int> codeFromName(std::string_view name);

}  // namespace input::android
