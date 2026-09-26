#pragma once

// Android side of the action layer: key codes (AKEYCODE_*) and the virtual
// axis-as-button codes of input/gamepad_input.h -> actions. KeyMap codes on
// Android are the raw AKEYCODE_* values.

#include <optional>
#include <string_view>

#include "input/input_action.h"
#include "input/keymap.h"

namespace input::android {

// The handheld layout of the port plan (Phase 5), plus a keyboard layout
// matching the desktop one. Buttons with several actions (A = confirm and
// play/pause, ...) are bound to each screen's meaning; a screen only reacts
// to one of them (tests/pad_selftest.cpp checks that).
//
//   D-pad / left stick   up down left right
//   A                    confirm, play/pause          B  back
//   X                    toggle CRT
//   Y                    media info / yellow          hold Y  page-number spinner
//   L1 / R1              page up/down, seek -/+, red/green
//   Start                settings, OSD, blue          Select  next section, aspect ratio
//   L2 / R2, right stick analog (see defaultAnalogBindings())
KeyMap defaultKeyMap();

// Key names for keys.cfg. Keyboard names as on desktop ("A".."Z", "0".."9",
// "F1".."F12", "ENTER", "ESCAPE", "SPACE", "BACKSPACE", "KP_0".."KP_9", ...);
// gamepad buttons "PAD_A", "PAD_B", "PAD_X", "PAD_Y", "PAD_L1", "PAD_R1",
// "PAD_L2", "PAD_R2", "PAD_START", "PAD_SELECT", "PAD_MODE", "PAD_THUMBL",
// "PAD_THUMBR" (also spelled "BUTTON_A" ...); the D-pad as keys "DPAD_UP" ...
// "DPAD_CENTER"; the D-pad as an axis "HAT_UP" ... "HAT_RIGHT"; the left
// stick "LSTICK_UP" ... "LSTICK_RIGHT"; "BACK", "MEDIA_PLAY_PAUSE", ... A
// "HOLD_" prefix ("HOLD_PAD_Y") binds the long press of that button.
// Case-insensitive.
std::optional<int> codeFromName(std::string_view name);

}  // namespace input::android
