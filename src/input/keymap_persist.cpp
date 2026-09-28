#include "input/keymap_persist.h"

#include <fstream>
#include <optional>
#include <sstream>

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

// Reads every line of `path` (empty if it doesn't exist -- not an error, a
// keys.cfg is optional to begin with).
std::vector<std::string> readLines(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(std::move(line));
    }
    return lines;
}

bool writeLines(const std::string& path, const std::vector<std::string>& lines) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        return false;
    }
    for (const std::string& line : lines) {
        out << line << "\n";
    }
    return static_cast<bool>(out);
}

// The action a line binds, the same way KeyMap::loadOverrides() parses it
// (blank/comment lines and lines with no '=' have none).
std::optional<Action> lineAction(const std::string& rawLine) {
    const std::string trimmed = trim(rawLine);
    if (trimmed.empty() || trimmed[0] == '#') {
        return std::nullopt;
    }
    const size_t eq = trimmed.find('=');
    if (eq == std::string::npos) {
        return std::nullopt;
    }
    return actionFromName(trim(std::string_view(trimmed).substr(0, eq)));
}

std::string joinNames(const std::vector<std::string>& names) {
    std::string joined;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i > 0) {
            joined += ", ";
        }
        joined += names[i];
    }
    return joined;
}

}  // namespace

bool saveKeyBinding(const std::string& path, Action action, const std::vector<std::string>& names) {
    std::vector<std::string> lines = readLines(path);
    const std::string newLine = std::string(actionName(action)) + " = " + joinNames(names);
    for (std::string& line : lines) {
        if (lineAction(line) == action) {
            line = newLine;
            return writeLines(path, lines);
        }
    }
    lines.push_back(newLine);
    return writeLines(path, lines);
}

bool clearKeyBindingOverride(const std::string& path, Action action) {
    std::vector<std::string> lines = readLines(path);
    bool changed = false;
    for (size_t i = 0; i < lines.size();) {
        if (lineAction(lines[i]) == action) {
            lines.erase(lines.begin() + static_cast<long>(i));
            changed = true;
        } else {
            ++i;
        }
    }
    return !changed || writeLines(path, lines);
}

}  // namespace input
