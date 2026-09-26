#pragma once

// GLFW side of the action layer: turns GLFW key callbacks into
// input::InputEvents. Only this file (and the window setup in App) knows
// GLFW key codes.

#include <optional>
#include <string_view>

#include "input/input_action.h"
#include "input/keymap.h"

namespace input::glfw {

// The KeyMap code for a GLFW key event. Normally the GLFW key itself; keys
// GLFW doesn't recognize (GLFW_KEY_UNKNOWN) are identified by scancode
// instead, offset by kScancodeBase so they can't collide with key codes.
constexpr int kScancodeBase = 0x10000;
int inputCode(int glfwKey, int scancode);

// GLFW_PRESS / GLFW_REPEAT / GLFW_RELEASE -> Phase.
std::optional<Phase> phaseFromGlfwAction(int glfwAction);

// The desktop bindings, matching the keys the player has always used.
KeyMap defaultKeyMap();

// Key names for keys.cfg: "A".."Z", "0".."9", "KP_0".."KP_9", "F1".."F12",
// "UP", "ENTER", "KP_ENTER", "ESCAPE", "BACKSPACE", "SPACE", "COMMA",
// "PERIOD", ... plus "SCANCODE_<hex>" for keys GLFW has no name for.
// Case-insensitive.
std::optional<int> codeFromName(std::string_view name);

}  // namespace input::glfw
