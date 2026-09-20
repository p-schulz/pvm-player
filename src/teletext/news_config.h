#pragma once

#include <string>
#include <vector>

namespace teletext {

struct NewsSource {
    int startPage = 0;         // first page of this category's block, e.g. 110
    int endPage = 0;           // last page of the block (inclusive); always set after parsing
    std::string category;      // display name, upper-case ASCII, <= kMaxCategoryName chars
    std::string url;           // http(s) feed URL
    std::string provider;      // shown cyan in the page header, e.g. "tagesschau.de" (<= kMaxProviderName)
    std::string logo;          // title-art text on the provider's index page, e.g. "TAGESSCHAU"
};

constexpr int kMaxCategoryName = 16;  // what fits in the header row next to the page number
constexpr int kMaxProviderName = 16;  // header columns available for it
constexpr int kMaxSources = 14;       // what fits on the index page (classic mode)
constexpr int kMaxSourcesHundreds = 40;

struct NewsConfig {
    std::vector<NewsSource> sources;   // validated: sorted by startPage, blocks don't overlap
    int refreshMinutes = 15;
    int blockSize = 10;                // pages reserved per category (headline page(s) + articles)
    int maxArticlePages = 3;           // cap per article; longer text is cut with a notice
    int weatherPage = 0;               // where the blue "Weather" key goes (0 = nowhere; the label is then blank)

    // false: page 100 is one index listing every source (the NEWS section).
    // true: every hundred page (100, 200, ... 900) that has sources in its
    // range is an overview of the top articles located in that range, and
    // article pages never use those numbers (the tagesschau section).
    bool hundredOverviews = false;
    std::string serviceName = "PVM NEWS";  // header text of page 100 / not-found pages
    std::string serviceLogo = "PVM NEWS";  // banner text of page 100
};

// Refresh interval floor. Sources are personal-use feeds polled politely;
// nothing in the config file can make this app poll faster than this.
constexpr int kMinRefreshMinutes = 5;

// news.cfg is a flat text file, one setting per line ('#' starts a comment):
//
//   refresh_minutes=15
//   block_size=10
//   max_article_pages=3
//   weather_page=150
//   overview_pages=hundreds        (or "index", the default)
//   service_name=tagesschau.de     service_logo=TAGESSCHAU
//   source=110 | WORLD | https://feeds.bbci.co.uk/news/world/rss.xml | bbc.co.uk | BBC NEWS
//
// A source is `page | CATEGORY | url` (page may be a range, `210-259`, giving
// the block explicitly instead of block_size pages), optionally followed by `| provider`
// (header text; defaults to the URL's host) and `| logo` (title-art text on
// the source's index page; defaults to the category).
//
// Problems (bad lines, overlapping blocks, ...) are reported through
// `warnings` and the offending line skipped -- one typo never disables the
// whole news section. Returns false only if the file could not be opened.
bool loadNewsConfig(const std::string& path, NewsConfig& config, std::vector<std::string>* warnings);

// Same, from text already in memory (used by the self-test).
void parseNewsConfig(const std::string& text, NewsConfig& config, std::vector<std::string>* warnings);

}  // namespace teletext
