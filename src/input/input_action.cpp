#include "input/input_action.h"

#include <array>
#include <cctype>
#include <string>

namespace input {

namespace {

constexpr std::array<const char*, kActionCount> kNames = {
    "up",           "down",          "left",           "right",       "confirm",     "back",
    "back_soft",    "play_pause",    "seek_back",      "seek_fwd",    "volume_down", "volume_up",
    "toggle_osd",   "media_info",    "video_scale",    "aspect_ratio", "toggle_crt",  "fastext_red",
    "fastext_green", "fastext_yellow", "fastext_blue", "digit_0",     "digit_1",     "digit_2",
    "digit_3",      "digit_4",       "digit_5",        "digit_6",     "digit_7",     "digit_8",
    "digit_9",
};

std::string normalize(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        out.push_back(c == '-' ? '_' : static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

}  // namespace

const char* actionName(Action action) {
    const auto index = static_cast<size_t>(action);
    return index < kNames.size() ? kNames[index] : "unknown";
}

std::optional<Action> actionFromName(std::string_view name) {
    const std::string wanted = normalize(name);
    for (size_t i = 0; i < kNames.size(); ++i) {
        if (wanted == kNames[i]) {
            return static_cast<Action>(i);
        }
    }
    return std::nullopt;
}

int digitOf(Action action) {
    const int value = static_cast<int>(action) - static_cast<int>(Action::Digit0);
    return value >= 0 && value <= 9 ? value : -1;
}

}  // namespace input
