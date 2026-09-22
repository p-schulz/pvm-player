#include "mvw_api.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <regex>
#include <unordered_set>

#include "html_strip.h"
#include "http_fetch.h"

namespace teletext {

namespace {

constexpr const char* kApiUrl = "https://mediathekviewweb.de/api/query";

}  // namespace

MvwQueryResult queryMediathek(const std::string& channel, const std::string& topicFilter, int size, bool newestFirst) {
    MvwQueryResult result;

    nlohmann::json queries = nlohmann::json::array();
    if (!channel.empty()) {
        queries.push_back({{"fields", {"channel"}}, {"query", channel}});
    }
    if (!topicFilter.empty()) {
        queries.push_back({{"fields", {"topic"}}, {"query", topicFilter}});
    }
    const nlohmann::json body = {
        {"queries", queries}, {"sortBy", "timestamp"},          {"sortOrder", newestFirst ? "desc" : "asc"},
        {"future", false},    {"offset", 0},                    {"size", std::clamp(size, 1, 3000)},
    };

    FetchOptions options;
    options.userAgent = "PVMPlayer-Teletext/1.0 (personal ARD Mediathek browser)";
    options.totalTimeoutSeconds = 30;  // a size=1500 query can take longer than a small RSS feed
    const FetchResult fetched = httpPostJson(kApiUrl, body.dump(), options);
    if (!fetched.ok) {
        result.error = "fetch failed: " + fetched.error;
        return result;
    }

    // MediathekViewWeb is a live, third-party service -- a shape it changes
    // out from under this app shows up as a JSON exception here, not a
    // crash; one try block covers parsing and every field access below.
    try {
        const nlohmann::json doc = nlohmann::json::parse(fetched.body);
        if (doc.contains("err") && !doc.at("err").is_null()) {
            result.error = "API error: " + doc.at("err").dump();
            return result;
        }
        const nlohmann::json& items = doc.at("result").at("results");
        if (!items.is_array()) {
            result.error = "unexpected response shape (result.results is not an array)";
            return result;
        }

        for (const auto& entry : items) {
            MvwItem item;
            item.title = entry.value("title", "");
            item.topic = entry.value("topic", "");
            item.channel = entry.value("channel", "");
            item.description = entry.value("description", "");
            item.timestamp = static_cast<std::time_t>(entry.value<long long>("timestamp", 0));
            item.durationSeconds = entry.value("duration", 0);
            item.videoUrl = entry.value("url_video", "");
            if (item.videoUrl.empty()) {
                item.videoUrl = entry.value("url_video_hd", "");
            }
            if (item.title.empty() || item.videoUrl.empty()) {
                continue;  // nothing to show, or nothing to play
            }
            const SeasonEpisode se = parseSeasonEpisode(item.title);
            item.season = se.season;
            item.episode = se.episode;
            item.accessibilityVariant = isAccessibilityVariantTitle(item.title);
            result.items.push_back(std::move(item));
        }
        result.ok = true;
    } catch (const nlohmann::json::exception& e) {
        result.error = std::string("bad response: ") + e.what();
        result.items.clear();
    }
    return result;
}

SeasonEpisode parseSeasonEpisode(const std::string& title) {
    // Season is usually 1-2 digits, but some shows (e.g. ZDF's heute-show)
    // use the broadcast year as the "season" instead, e.g. "(S2026/E19)".
    static const std::regex pattern(R"(\(S(\d{1,4})/E(\d{1,3})\))", std::regex::icase);
    std::smatch match;
    if (std::regex_search(title, match, pattern)) {
        return {std::stoi(match[1]), std::stoi(match[2])};
    }
    return {};
}

bool isAccessibilityVariantTitle(const std::string& title) {
    return title.find("Audiodeskription") != std::string::npos || title.find("Gebärdensprache") != std::string::npos;
}

std::string shortEpisodeTitle(const std::string& title, const std::string& showTopic) {
    std::string t = title;

    // "(Audiodeskription)"/"(Gebärdensprache)" and the season/episode marker
    // are already shown as a separate badge -- strip both.
    static const std::regex accessibilityPattern(R"(\s*\((Audiodeskription|Gebärdensprache)\))", std::regex::icase);
    t = std::regex_replace(t, accessibilityPattern, "");
    static const std::regex sePattern(R"(\s*\(S\d{1,4}/E\d{1,3}\))", std::regex::icase);
    t = std::regex_replace(t, sePattern, "");

    // "Folge 4 · Staffel 2 | Babylon Berlin" -> "Folge 4": the show name and
    // season number are redundant once the S/E badge exists.
    const size_t bar = t.find(" | " + showTopic);
    if (bar != std::string::npos) {
        t.erase(bar);
    }
    static const std::regex staffelPattern(R"(\s*\xC2\xB7\s*Staffel\s*\d+\s*$)");  // "\xC2\xB7" = UTF-8 '·'
    t = std::regex_replace(t, staffelPattern, "");

    t = collapseWhitespace(t);
    return t.empty() ? collapseWhitespace(title) : t;
}

std::vector<MvwItem> dropAccessibilityDuplicates(std::vector<MvwItem> items) {
    // A key identifying "the same episode": season/episode when known, else
    // the title with any accessibility-variant marker stripped.
    const auto key = [](const MvwItem& item) {
        if (item.season > 0 || item.episode > 0) {
            return std::to_string(item.season) + "/" + std::to_string(item.episode);
        }
        static const std::regex accessibilityPattern(R"(\s*\((Audiodeskription|Gebärdensprache)\))",
                                                       std::regex::icase);
        return collapseWhitespace(std::regex_replace(item.title, accessibilityPattern, ""));
    };

    std::unordered_set<std::string> hasPlainVersion;
    for (const MvwItem& item : items) {
        if (!item.accessibilityVariant) {
            hasPlainVersion.insert(key(item));
        }
    }
    std::vector<MvwItem> out;
    out.reserve(items.size());
    for (MvwItem& item : items) {
        if (item.accessibilityVariant && hasPlainVersion.count(key(item)) != 0) {
            continue;  // the plain twin already covers this episode
        }
        out.push_back(std::move(item));
    }
    return out;
}

}  // namespace teletext
