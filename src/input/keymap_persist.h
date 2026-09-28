#pragma once

// Line-level editing of a keys.cfg-format file, for the in-app Controls menu
// (Settings > Controls) to persist a rebind without disturbing anything else
// in the file -- the user's own comments and other actions' overrides
// included. Platform-independent: the caller supplies the code->name work
// already done (see each platform's nameFromCode()).

#include <string>
#include <vector>

#include "input/input_action.h"

namespace input {

// Replaces action's own "<action> = ..." line in the file at `path` so it
// reads "<action> = names[0], names[1], ...", appending a new line if it had
// none, and creating the file if it doesn't exist yet. Every other line is
// left exactly as it was. Returns false only if the file could not be
// written (e.g. a read-only filesystem).
bool saveKeyBinding(const std::string& path, Action action, const std::vector<std::string>& names);

// Removes action's own override line from the file at `path`, if it has one,
// so its compiled-in default binding applies again after a restart. A no-op
// (still returns true) if the file doesn't exist or has no such line.
bool clearKeyBindingOverride(const std::string& path, Action action);

}  // namespace input
