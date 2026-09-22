#pragma once

#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include "feed_parser.h"
#include "news_config.h"
#include "page.h"

namespace teletext {

enum class SourceStatus {
    Pending,  // nothing fetched yet and nothing cached
    Ok,       // most recent fetch succeeded
    Cached,   // items came from the disk cache; first live refresh not done yet
    Stale,    // has items, but the most recent fetch failed
    Failed,   // no items and the fetch failed
};

// What the aggregator knows about one configured source.
struct SourceData {
    SourceStatus status = SourceStatus::Pending;
    std::vector<FeedItem> items;   // as parsed (UTF-8); page layout normalizes to ASCII
    std::string feedTitle;
    std::time_t fetchedAt = 0;     // when `items` were fetched (0 = never)
};

// Builds the complete page set from the config and per-source data
// (`data[i]` belongs to `config.sources[i]`; missing entries count as
// Pending). Always contains page 100, whatever state the sources are in.
//
// Per source, the block [startPage, startPage + blockSize) holds:
//   * headline page(s) first (as many as the headlines need), then
//   * articles, newest first, each taking 1..maxArticlePages consecutive pages.
// When the block is full the oldest articles are dropped (they simply
// never get a page); the last article that fits is cut to whatever pages
// remain rather than wasting them.
std::shared_ptr<const PageStore> buildPageStore(const NewsConfig& config, const std::vector<SourceData>& data);

}  // namespace teletext
