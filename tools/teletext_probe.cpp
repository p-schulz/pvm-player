// Phase 2 verification tool: fetches one feed URL, parses it and prints what
// the teletext pipeline would see -- no window, no UI.
//
//   teletext_probe <url> [--full] [--limit N]
//
// Reports per-item body length so feed quality (full text vs teaser) can be
// judged per source before it is added to news.cfg.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "teletext/feed_parser.h"
#include "teletext/html_strip.h"
#include "teletext/http_fetch.h"
#include "teletext/news_config.h"
#include "teletext/news_service.h"

// Phase 4 verification: `teletext_probe --news <news.cfg> [page ...]` builds the
// whole page set from the config and prints the index (or the given pages)
// as plain text, followed by a one-line-per-page overview.
int dumpNews(const char* configPath, int argc, char** argv) {
    teletext::NewsConfig config;
    std::vector<std::string> warnings;
    if (!teletext::loadNewsConfig(configPath, config, &warnings)) {
        std::fprintf(stderr, "cannot open %s\n", configPath);
        return 1;
    }
    for (const auto& w : warnings) {
        std::fprintf(stderr, "warning: %s\n", w.c_str());
    }
    teletext::NewsService service(config, "");  // no disk cache: always a live view
    service.refreshAll();
    const auto store = service.snapshot();

    std::vector<int> wanted;
    for (int i = 0; i < argc; ++i) {
        wanted.push_back(std::atoi(argv[i]));
    }
    if (wanted.empty()) {
        wanted.push_back(teletext::kIndexPage);
    }
    for (int number : wanted) {
        const teletext::TeletextPage* page = store->find(number);
        if (!page) {
            std::printf("--- page %d: NOT FOUND\n", number);
            continue;
        }
        std::printf("+%s+  page %d\n", std::string(teletext::kCols, '-').c_str(), number);
        for (const auto& line : page->lines) {
            std::printf("|%s|\n", line.c_str());
        }
        std::printf("+%s+\n", std::string(teletext::kCols, '-').c_str());
    }
    std::printf("\npage map (%zu pages):\n", store->size());
    for (const auto& [number, page] : store->pages()) {
        std::printf("  %d%s %s\n", number, page.articleFirstPage && page.articleFirstPage != number ? "  (cont.)" : "",
                    page.lines[teletext::kContentFirstRow].c_str());
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <feed-url> [--full] [--limit N]\n       %s --news <news.cfg> [page ...]\n",
                     argv[0], argv[0]);
        return 2;
    }
    if (!std::strcmp(argv[1], "--news") && argc >= 3) {
        return dumpNews(argv[2], argc - 3, argv + 3);
    }
    const std::string url = argv[1];
    bool full = false;
    int limit = 5;
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--full")) {
            full = true;
        } else if (!std::strcmp(argv[i], "--limit") && i + 1 < argc) {
            limit = std::atoi(argv[++i]);
        }
    }

    const teletext::FetchResult fetched = teletext::httpGet(url);
    if (!fetched.ok) {
        std::fprintf(stderr, "FETCH FAILED: %s (status %ld)\n", fetched.error.c_str(), fetched.status);
        return 1;
    }
    std::printf("fetched %zu bytes (HTTP %ld)\n", fetched.body.size(), fetched.status);

    const teletext::Feed feed = teletext::parseFeed(fetched.body);
    if (!feed.ok) {
        std::fprintf(stderr, "PARSE FAILED: %s\n", feed.error.c_str());
        return 1;
    }
    std::printf("feed: \"%s\" -- %zu items\n", teletext::toTeletextAscii(feed.title).c_str(), feed.items.size());

    size_t totalBody = 0;
    size_t empty = 0;
    for (const auto& item : feed.items) {
        totalBody += item.body.size();
        empty += item.body.empty() ? 1 : 0;
    }
    std::printf("body text: avg %zu chars/item, %zu items with none\n\n",
                feed.items.empty() ? 0 : totalBody / feed.items.size(), empty);

    const int shown = std::min<int>(limit, static_cast<int>(feed.items.size()));
    for (int i = 0; i < shown; ++i) {
        const auto& item = feed.items[static_cast<size_t>(i)];
        char when[32] = "(no date)";
        if (item.published) {
            std::tm tm{};
#ifdef _WIN32
            gmtime_s(&tm, &item.published);
#else
            gmtime_r(&item.published, &tm);
#endif
            std::strftime(when, sizeof(when), "%Y-%m-%d %H:%MZ", &tm);
        }
        std::printf("[%d] %s\n    %s | cat=\"%s\" | body=%zu chars\n    %s\n", i + 1,
                    teletext::toTeletextAscii(item.title).c_str(), when, item.category.c_str(), item.body.size(),
                    item.link.c_str());
        const std::string body = teletext::toTeletextAscii(item.body);
        if (full) {
            std::printf("----\n%s\n----\n", body.c_str());
        } else {
            std::printf("    > %s%s\n", body.substr(0, 160).c_str(), body.size() > 160 ? "..." : "");
        }
    }
    return 0;
}
