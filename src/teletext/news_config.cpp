#include "news_config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
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

// "https://www.tagesschau.de/xml/rss2/" -> "tagesschau.de". Drops the
// scheme, path and the usual "www."/"feeds." host prefixes.
std::string providerFromUrl(const std::string& url) {
    size_t start = url.find("://");
    start = start == std::string::npos ? 0 : start + 3;
    const size_t end = url.find_first_of("/:?", start);
    std::string host = url.substr(start, end == std::string::npos ? std::string::npos : end - start);
    for (const char* prefix : {"www.", "feeds.", "rss."}) {
        if (host.rfind(prefix, 0) == 0 && host.size() > std::strlen(prefix) + 3) {
            host.erase(0, std::strlen(prefix));
        }
    }
    return host;
}

}  // namespace

void parseNewsConfig(const std::string& text, NewsConfig& config, std::vector<std::string>* warnings) {
    const auto warn = [&](int line, const std::string& msg) {
        if (warnings) {
            warnings->push_back("news config line " + std::to_string(line) + ": " + msg);
        }
    };

    std::vector<NewsSource> sources;
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
        if (key == "refresh_minutes") {
            if (!parseInt(value, number)) {
                warn(lineNo, "refresh_minutes must be a whole number");
            } else {
                config.refreshMinutes = std::max(kMinRefreshMinutes, number);
                if (number < kMinRefreshMinutes) {
                    warn(lineNo, "refresh_minutes raised to the minimum of " + std::to_string(kMinRefreshMinutes));
                }
            }
        } else if (key == "block_size") {
            if (!parseInt(value, number) || number < 2 || number > 100) {
                warn(lineNo, "block_size must be a whole number from 2 to 100");
            } else {
                config.blockSize = number;
            }
        } else if (key == "max_article_pages") {
            if (!parseInt(value, number) || number < 1 || number > 20) {
                warn(lineNo, "max_article_pages must be a whole number from 1 to 20");
            } else {
                config.maxArticlePages = number;
            }
        } else if (key == "overview_pages") {
            if (value == "hundreds" || value == "index") {
                config.hundredOverviews = value == "hundreds";
            } else {
                warn(lineNo, "overview_pages must be 'index' or 'hundreds'");
            }
        } else if (key == "service_name") {
            config.serviceName = collapseWhitespace(toTeletextAscii(value)).substr(0, kMaxProviderName);
        } else if (key == "service_logo") {
            config.serviceLogo = collapseWhitespace(toTeletextAscii(value));
            std::transform(config.serviceLogo.begin(), config.serviceLogo.end(), config.serviceLogo.begin(),
                           [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        } else if (key == "source") {
            std::vector<std::string> fields;
            std::istringstream fs(value);
            std::string field;
            while (std::getline(fs, field, '|')) {
                fields.push_back(trim(field));
            }
            if (fields.size() < 3 || fields.size() > 5) {
                warn(lineNo, "source needs: page | CATEGORY | url [| provider [| logo]]");
                continue;
            }
            NewsSource src;
            // "210" (block_size pages from there) or "210-259" (explicit block).
            const size_t dash = fields[0].find('-');
            const bool okStart = parseInt(trim(fields[0].substr(0, dash)), src.startPage);
            const bool okEnd = dash == std::string::npos || parseInt(trim(fields[0].substr(dash + 1)), src.endPage);
            if (!okStart || !okEnd || src.startPage < 101 || src.startPage > kMaxPage ||
                (dash != std::string::npos && (src.endPage < src.startPage + 1 || src.endPage > kMaxPage))) {
                warn(lineNo, "pages must be a number 101-999, or a range like 210-259 (at least 2 pages)");
                continue;
            }
            std::string name = toTeletextAscii(fields[1]);
            std::transform(name.begin(), name.end(), name.begin(),
                           [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            src.category = collapseWhitespace(name).substr(0, kMaxCategoryName);
            if (src.category.empty()) {
                warn(lineNo, "category name is empty");
                continue;
            }
            if (fields[2].rfind("http://", 0) != 0 && fields[2].rfind("https://", 0) != 0) {
                warn(lineNo, "feed URL must start with http:// or https://");
                continue;
            }
            src.url = fields[2];

            const std::string provider = fields.size() > 3 && !fields[3].empty() ? fields[3] : providerFromUrl(src.url);
            src.provider = collapseWhitespace(toTeletextAscii(provider)).substr(0, kMaxProviderName);
            std::string logo = fields.size() > 4 && !fields[4].empty() ? fields[4] : src.category;
            src.logo = collapseWhitespace(toTeletextAscii(logo));
            std::transform(src.logo.begin(), src.logo.end(), src.logo.begin(),
                           [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            sources.push_back(std::move(src));
        } else {
            warn(lineNo, "unknown setting '" + key + "', ignoring");
        }
    }

    // Blocks must not overlap (blockSize is only final once the whole file
    // has been read, hence validating here rather than per line).
    for (NewsSource& src : sources) {
        if (src.endPage == 0) {
            src.endPage = std::min(src.startPage + config.blockSize - 1, kMaxPage);
        }
    }
    std::stable_sort(sources.begin(), sources.end(),
                     [](const NewsSource& a, const NewsSource& b) { return a.startPage < b.startPage; });
    const size_t maxSources = static_cast<size_t>(config.hundredOverviews ? kMaxSourcesHundreds : kMaxSources);
    config.sources.clear();
    for (NewsSource& src : sources) {
        const auto skip = [&](const std::string& why) {
            if (warnings) {
                warnings->push_back("news config: source '" + src.category + "' at page " +
                                    std::to_string(src.startPage) + ": " + why + ", ignoring");
            }
        };
        if (!config.sources.empty() && src.startPage <= config.sources.back().endPage) {
            skip("overlaps the pages of '" + config.sources.back().category + "'");
        } else if (config.hundredOverviews && src.startPage % 100 == 0) {
            skip("a hundred page is reserved for the overview");
        } else if (config.sources.size() >= maxSources) {
            skip("too many sources");
        } else {
            config.sources.push_back(std::move(src));
        }
    }
}

bool loadNewsConfig(const std::string& path, NewsConfig& config, std::vector<std::string>* warnings) {
    std::ifstream file(path);
    if (!file) {
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    parseNewsConfig(buffer.str(), config, warnings);
    return true;
}

}  // namespace teletext
