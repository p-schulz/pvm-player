#include "input/keymap.h"

#include <algorithm>

namespace input {

namespace {

std::string trim(std::string_view s) {
    const char* ws = " \t\r\n";
    const size_t begin = s.find_first_not_of(ws);
    if (begin == std::string_view::npos) {
        return {};
    }
    const size_t end = s.find_last_not_of(ws);
    return std::string(s.substr(begin, end - begin + 1));
}

const std::vector<Action>& emptyActions() {
    static const std::vector<Action> empty;
    return empty;
}

}  // namespace

void KeyMap::bind(int code, Action action) {
    std::vector<Action>& actions = map_[code];
    if (std::find(actions.begin(), actions.end(), action) == actions.end()) {
        actions.push_back(action);
    }
}

void KeyMap::unbindAction(Action action) {
    for (auto it = map_.begin(); it != map_.end();) {
        auto& actions = it->second;
        actions.erase(std::remove(actions.begin(), actions.end(), action), actions.end());
        it = actions.empty() ? map_.erase(it) : std::next(it);
    }
}

const std::vector<Action>& KeyMap::actionsFor(int code) const {
    const auto it = map_.find(code);
    return it == map_.end() ? emptyActions() : it->second;
}

std::vector<int> KeyMap::codesFor(Action action) const {
    std::vector<int> codes;
    for (const auto& [code, actions] : map_) {
        if (std::find(actions.begin(), actions.end(), action) != actions.end()) {
            codes.push_back(code);
        }
    }
    std::sort(codes.begin(), codes.end());
    return codes;
}

std::vector<std::string> KeyMap::loadOverrides(std::istream& in, const CodeFromName& codeFromName,
                                               const std::string& sourceLabel) {
    std::vector<std::string> warnings;
    std::string line;
    int lineNumber = 0;
    while (std::getline(in, line)) {
        ++lineNumber;
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }
        const std::string where = sourceLabel + ":" + std::to_string(lineNumber) + ": ";
        const size_t eq = trimmed.find('=');
        if (eq == std::string::npos) {
            warnings.push_back(where + "ignoring line without '='");
            continue;
        }
        const std::string actionText = trim(std::string_view(trimmed).substr(0, eq));
        const std::optional<Action> action = actionFromName(actionText);
        if (!action) {
            warnings.push_back(where + "unknown action '" + actionText + "'");
            continue;
        }

        std::vector<int> codes;
        bool ok = true;
        std::string_view rest = std::string_view(trimmed).substr(eq + 1);
        while (!rest.empty()) {
            const size_t comma = rest.find(',');
            const std::string name = trim(rest.substr(0, comma));
            rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
            if (name.empty()) {
                continue;
            }
            const std::optional<int> code = codeFromName(name);
            if (!code) {
                warnings.push_back(where + "unknown key '" + name + "'");
                ok = false;
                break;
            }
            codes.push_back(*code);
        }
        if (!ok) {
            continue;  // leave the defaults for this action untouched
        }
        unbindAction(*action);
        for (int code : codes) {
            bind(code, *action);
        }
    }
    return warnings;
}

}  // namespace input
