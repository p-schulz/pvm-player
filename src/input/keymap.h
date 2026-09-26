#pragma once

// Maps platform input codes (opaque ints: GLFW key codes on desktop,
// AKEYCODE_* on Android) to actions. One code may emit several actions --
// e.g. B is "aspect ratio" while playing and "blue" on a teletext page -- as
// long as no single screen reacts to more than one of them.

#include <functional>
#include <istream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "input/input_action.h"

namespace input {

class KeyMap {
public:
    using CodeFromName = std::function<std::optional<int>(std::string_view)>;

    void bind(int code, Action action);
    void unbindAction(Action action);  // removes every code bound to `action`

    // Actions emitted by `code`, in binding order; empty if unbound.
    const std::vector<Action>& actionsFor(int code) const;
    std::vector<int> codesFor(Action action) const;

    // Reads override lines of the form
    //     <action> = NAME[, NAME...]
    // ('#' comments and blank lines ignored). Each line replaces that
    // action's existing bindings; an empty right-hand side unbinds it.
    // Unknown actions or key names are skipped and reported in the returned
    // warnings ("keys.cfg:3: unknown key 'FOO'").
    std::vector<std::string> loadOverrides(std::istream& in, const CodeFromName& codeFromName,
                                           const std::string& sourceLabel);

private:
    std::unordered_map<int, std::vector<Action>> map_;
};

}  // namespace input
