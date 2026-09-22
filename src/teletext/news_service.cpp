#include "news_service.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "feed_parser.h"
#include "http_fetch.h"

namespace fs = std::filesystem;

namespace teletext {

namespace {

constexpr const char* kCacheMagic = "PVMNEWS1";

// Pause between consecutive sources within one refresh round, so a round is
// a trickle of requests to different hosts rather than a burst.
constexpr auto kPauseBetweenSources = std::chrono::milliseconds(750);

std::string hashHex(const std::string& s) {
    std::uint64_t h = 1469598103934665603ULL;  // FNV-1a
    for (unsigned char c : s) {
        h = (h ^ c) * 1099511628211ULL;
    }
    char buf[20];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

}  // namespace

NewsService::NewsService(NewsConfig config, std::string cacheDir)
    : config_(std::move(config)), cacheDir_(std::move(cacheDir)), data_(config_.sources.size()) {
    loadCache();
    publish();
}

NewsService::~NewsService() {
    stop();
}

void NewsService::publish() {
    std::atomic_store(&snapshot_, buildPageStore(config_, data_));
}

// ---- disk cache ------------------------------------------------------------
// One file per source: a header line "PVMNEWS1 <fetched epoch> <url>" followed
// by the raw feed body exactly as received. Caching the raw XML (rather than
// parsed items) means the parser, ASCII folding and page layout can all
// improve without invalidating the cache.

std::string NewsService::cacheFileName(const std::string& url) {
    return hashHex(url) + ".feed";
}

std::string NewsService::cachePath(size_t index) const {
    return cacheDir_ + "/" + cacheFileName(config_.sources[index].url);
}

void NewsService::saveCache(size_t index, const std::string& body, std::time_t fetchedAt) const {
    if (cacheDir_.empty()) {
        return;
    }
    std::error_code ec;
    fs::create_directories(cacheDir_, ec);
    const std::string finalPath = cachePath(index);
    const std::string tempPath = finalPath + ".tmp";
    {
        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            std::fprintf(stderr, "[news] cannot write cache file %s\n", tempPath.c_str());
            return;
        }
        out << kCacheMagic << ' ' << static_cast<long long>(fetchedAt) << ' ' << config_.sources[index].url << '\n';
        out.write(body.data(), static_cast<std::streamsize>(body.size()));
        if (!out) {
            std::fprintf(stderr, "[news] failed writing cache file %s\n", tempPath.c_str());
            fs::remove(tempPath, ec);
            return;
        }
    }
    // Rename over the old file so a crash mid-write can't leave a torn cache.
    fs::rename(tempPath, finalPath, ec);
    if (ec) {
        std::fprintf(stderr, "[news] cannot replace cache file %s: %s\n", finalPath.c_str(), ec.message().c_str());
        fs::remove(tempPath, ec);
    }
}

void NewsService::loadCache() {
    if (cacheDir_.empty()) {
        return;
    }
    for (size_t i = 0; i < config_.sources.size(); ++i) {
        std::ifstream in(cachePath(i), std::ios::binary);
        if (!in) {
            continue;
        }
        std::string magic, url;
        long long epoch = 0;
        in >> magic >> epoch;
        in.get();  // the space before the URL
        std::getline(in, url);
        if (magic != kCacheMagic || url != config_.sources[i].url || epoch <= 0) {
            continue;  // foreign or damaged file: behave as if there were no cache
        }
        std::stringstream body;
        body << in.rdbuf();
        Feed feed = parseFeed(body.str());
        if (!feed.ok || feed.items.empty()) {
            continue;
        }
        data_[i].items = std::move(feed.items);
        data_[i].feedTitle = std::move(feed.title);
        data_[i].fetchedAt = static_cast<std::time_t>(epoch);
        data_[i].status = SourceStatus::Cached;
        std::fprintf(stdout, "[news] %s: %zu items from cache\n", config_.sources[i].category.c_str(),
                     data_[i].items.size());
    }
}

// ---- fetching --------------------------------------------------------------

bool NewsService::refreshSource(size_t index) {
    const NewsSource& src = config_.sources[index];
    SourceData& data = data_[index];

    const auto fail = [&](const std::string& reason) {
        if (stopRequested_) {
            return false;  // shutting down mid-transfer: not a source problem, nothing to report or record
        }
        std::fprintf(stderr, "[news] %s (%s): %s\n", src.category.c_str(), src.url.c_str(), reason.c_str());
        data.status = data.items.empty() ? SourceStatus::Failed : SourceStatus::Stale;
        return false;
    };

    FetchOptions options;
    options.cancel = &stopRequested_;
    const FetchResult fetched = httpGet(src.url, options);
    if (!fetched.ok) {
        return fail("fetch failed: " + fetched.error);
    }
    Feed feed = parseFeed(fetched.body);
    if (!feed.ok) {
        return fail("bad feed: " + feed.error);
    }
    if (feed.items.empty()) {
        return fail("feed contains no items");
    }

    std::fprintf(stdout, "[news] %s: %zu items\n", src.category.c_str(), feed.items.size());
    data.items = std::move(feed.items);
    data.feedTitle = std::move(feed.title);
    data.fetchedAt = std::time(nullptr);
    data.status = SourceStatus::Ok;
    saveCache(index, fetched.body, data.fetchedAt);
    return true;
}

void NewsService::refreshAll() {
    for (size_t i = 0; i < config_.sources.size(); ++i) {
        refreshSource(i);
    }
    publish();
}

// ---- background loop -------------------------------------------------------

void NewsService::start() {
    if (worker_.joinable() || config_.sources.empty()) {
        return;
    }
    stopRequested_ = false;
    worker_ = std::thread([this] { runLoop(); });
}

void NewsService::stop() {
    if (!worker_.joinable()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(wakeMutex_);
        stopRequested_ = true;
    }
    wake_.notify_all();
    worker_.join();
}

void NewsService::requestRefresh() {
    {
        std::lock_guard<std::mutex> lock(wakeMutex_);
        refreshRequested_ = true;
    }
    wake_.notify_all();
}

void NewsService::runLoop() {
    const auto sleepFor = [this](std::chrono::milliseconds duration) {
        std::unique_lock<std::mutex> lock(wakeMutex_);
        wake_.wait_for(lock, duration, [this] { return stopRequested_.load() || refreshRequested_.load(); });
        refreshRequested_ = false;
        return !stopRequested_;
    };

    const auto interval = intervalOverrideSeconds_ > 0
                              ? std::chrono::seconds(intervalOverrideSeconds_)
                              : std::chrono::seconds(std::max(config_.refreshMinutes, kMinRefreshMinutes) * 60);
    // After a round where something failed (network down, server hiccup),
    // look again sooner than the full interval -- but never faster than the
    // polite minimum, so a dead source can't be hammered.
    const auto retryInterval = intervalOverrideSeconds_ > 0 ? interval
                                                            : std::min<std::chrono::seconds>(
                                                                  interval, std::chrono::minutes(kMinRefreshMinutes));

    while (!stopRequested_) {
        bool anyFailed = false;
        for (size_t i = 0; i < config_.sources.size() && !stopRequested_; ++i) {
            anyFailed |= !refreshSource(i);
            // Publish after every source so each category appears as soon as
            // it arrives rather than after the slowest one.
            publish();
            if (i + 1 < config_.sources.size() && !sleepFor(kPauseBetweenSources)) {
                break;
            }
        }
        if (!sleepFor(std::chrono::duration_cast<std::chrono::milliseconds>(anyFailed ? retryInterval : interval))) {
            break;
        }
    }
}

}  // namespace teletext
