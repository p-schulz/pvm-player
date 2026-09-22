#include "mvw_config.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "html_strip.h"
#include "page.h"

namespace teletext {

namespace {

std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    const size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos) {
        return "";
    }
    return s.substr(b, s.find_last_not_of(ws) - b + 1);
}

bool parseInt(const std::string& s, int& out) {
    if (s.empty()) {
        return false;
    }
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (*end != '\0' || v < -1'000'000 || v > 1'000'000) {
        return false;
    }
    out = static_cast<int>(v);
    return true;
}

std::string upperAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

}  // namespace

void parseMvwConfig(const std::string& text, MvwConfig& config, std::vector<std::string>* warnings) {
    const auto warn = [&](int line, const std::string& msg) {
        if (warnings) {
            warnings->push_back("mvw config line " + std::to_string(line) + ": " + msg);
        }
    };

    std::vector<MvwFavorite> favorites;
    std::istringstream in(text);
    std::string raw;
    int lineNo = 0;
    while (std::getline(in, raw)) {
        ++lineNo;
        const std::string line = trim(raw);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            warn(lineNo, "expected key=value, ignoring");
            continue;
        }
        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));

        int number = 0;
        if (key == "channel") {
            if (value.empty()) {
                warn(lineNo, "channel must not be empty");
            } else {
                config.channel = value;
            }
        } else if (key == "service_name") {
            if (value.empty()) {
                warn(lineNo, "service_name must not be empty");
            } else {
                config.serviceName = value;
            }
        } else if (key == "service_logo") {
            if (value.empty()) {
                warn(lineNo, "service_logo must not be empty");
            } else {
                config.serviceLogo = upperAscii(collapseWhitespace(toTeletextAscii(value)));
            }
        } else if (key == "max_episode_pages") {
            if (!parseInt(value, number) || number < 1 || number > 500) {
                warn(lineNo, "max_episode_pages must be a whole number from 1 to 500");
            } else {
                config.maxEpisodePages = number;
            }
        } else if (key == "az_start_page") {
            if (!parseInt(value, number) || number < 101 || number > kMaxPage) {
                warn(lineNo, "az_start_page must be a page number from 101 to 999");
            } else {
                config.azStartPage = number;
            }
        } else if (key == "az_window_days") {
            if (!parseInt(value, number) || number < 1 || number > 90) {
                warn(lineNo, "az_window_days must be a whole number from 1 to 90");
            } else {
                config.azWindowDays = number;
            }
        } else if (key == "az_query_size") {
            if (!parseInt(value, number) || number < 50 || number > 3000) {
                warn(lineNo, "az_query_size must be a whole number from 50 to 3000");
            } else {
                config.azQuerySize = number;
            }
        } else if (key == "az_max_shows") {
            if (!parseInt(value, number) || number < 1 || number > 800) {
                warn(lineNo, "az_max_shows must be a whole number from 1 to 800");
            } else {
                config.azMaxShows = number;
            }
        } else if (key == "refresh_minutes") {
            if (!parseInt(value, number)) {
                warn(lineNo, "refresh_minutes must be a whole number");
            } else {
                config.refreshMinutes = std::max(kMvwMinRefreshMinutes, number);
                if (number < kMvwMinRefreshMinutes) {
                    warn(lineNo,
                         "refresh_minutes raised to the minimum of " + std::to_string(kMvwMinRefreshMinutes));
                }
            }
        } else if (key == "favorite") {
            std::vector<std::string> fields;
            std::istringstream fs(value);
            std::string field;
            while (std::getline(fs, field, '|')) {
                fields.push_back(trim(field));
            }
            if (fields.size() < 2 || fields.size() > 4) {
                warn(lineNo, "favorite needs: page | topic [| LABEL [| CHANNEL]]");
                continue;
            }
            MvwFavorite fav;
            if (!parseInt(fields[0], fav.page) || fav.page < 101 || fav.page > kMaxPage) {
                warn(lineNo, "favorite's page must be a number from 101 to 999");
                continue;
            }
            if (fields[1].empty()) {
                warn(lineNo, "favorite's topic is empty");
                continue;
            }
            fav.topic = fields[1];
            const std::string label = fields.size() > 2 && !fields[2].empty() ? fields[2] : fav.topic;
            fav.label = upperAscii(collapseWhitespace(toTeletextAscii(label)));
            if (fields.size() > 3 && !fields[3].empty()) {
                fav.channel = fields[3];
            }
            favorites.push_back(std::move(fav));
        } else {
            warn(lineNo, "unknown setting '" + key + "', ignoring");
        }
    }

    // A favorite now costs exactly one page number (its episodes live on
    // that page's own sub-pages, see mvw_pages.h), so the only possible
    // collision is two favorites -- or a favorite and the generated A-Z
    // section -- claiming the very same number. Sorting first makes a
    // duplicate trivial to spot as an adjacent pair.
    std::stable_sort(favorites.begin(), favorites.end(),
                     [](const MvwFavorite& a, const MvwFavorite& b) { return a.page < b.page; });

    config.favorites.clear();
    for (MvwFavorite& fav : favorites) {
        const auto skip = [&](const std::string& why) {
            if (warnings) {
                warnings->push_back("mvw config: favorite '" + fav.topic + "' at page " +
                                    std::to_string(fav.page) + ": " + why + ", ignoring");
            }
        };
        if (!config.favorites.empty() && fav.page == config.favorites.back().page) {
            skip("page already used by favorite '" + config.favorites.back().topic + "'");
        } else if (fav.page >= config.azStartPage) {
            skip("overlaps the generated A-Z section (az_start_page=" + std::to_string(config.azStartPage) + ")");
        } else {
            if (fav.channel.empty()) {
                fav.channel = config.channel;  // no explicit CHANNEL field: inherit this section's own channel
            }
            config.favorites.push_back(std::move(fav));
        }
    }
}

bool loadMvwConfig(const std::string& path, MvwConfig& config, std::vector<std::string>* warnings) {
    std::ifstream file(path);
    if (!file) {
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    parseMvwConfig(buffer.str(), config, warnings);
    return true;
}

}  // namespace teletext
