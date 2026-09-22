#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "data_service.h"
#include "news_config.h"
#include "news_pages.h"
#include "page.h"

namespace teletext {

// Owns the news data: fetches every configured source, keeps the last good
// result per source (in memory and on disk), and turns it into an immutable
// PageStore snapshot.
//
// Threading model: once start() has been called a single worker thread owns
// all mutable state (per-source data, the network, the disk cache). It
// publishes a fresh snapshot by atomically swapping a shared_ptr; readers
// (the render thread) only ever call snapshot(), which is a lock-free-ish
// atomic pointer copy and never waits on the network or on page building.
class NewsService : public TeletextDataService {
public:
    // `cacheDir` may be empty to disable the disk cache. Loads whatever the
    // cache holds and publishes it immediately, so snapshot() has real pages
    // before any network activity.
    NewsService(NewsConfig config, std::string cacheDir);
    ~NewsService() override;

    NewsService(const NewsService&) = delete;
    NewsService& operator=(const NewsService&) = delete;

    std::shared_ptr<const PageStore> snapshot() const override { return std::atomic_load(&snapshot_); }
    std::string serviceName() const override { return config_.serviceName; }

    // Wakes the background loop so it refreshes right away instead of
    // waiting out the rest of its interval. A no-op if the loop isn't
    // running (start() was never called, or there are no sources). Not
    // exact: if a refresh round is already in progress this only shortens
    // the pause *between* sources, rather than restarting the round --
    // simple, and never violates the polite per-source rate limit.
    void requestRefresh() override;

    // Starts the background refresh loop: fetch everything now, then again
    // every refresh interval. Idempotent.
    void start();
    // Stops the loop (aborting any transfer in flight) and joins the thread.
    void stop();

    // Fetches every source once on the calling thread. Only for use when the
    // background thread is not running (command-line tools, tests).
    void refreshAll();

    // Test hook: replaces the configured interval (bypassing the polite
    // minimum). Call before start().
    void setRefreshIntervalSecondsForTesting(int seconds) { intervalOverrideSeconds_ = seconds; }

    const NewsConfig& config() const { return config_; }

    // File name (within the cache directory) used for a source URL.
    static std::string cacheFileName(const std::string& url);

private:
    bool refreshSource(size_t index);
    void publish();
    void runLoop();

    void loadCache();
    void saveCache(size_t index, const std::string& body, std::time_t fetchedAt) const;
    std::string cachePath(size_t index) const;

    NewsConfig config_;
    std::string cacheDir_;
    std::vector<SourceData> data_;  // worker thread only, once started
    std::shared_ptr<const PageStore> snapshot_;

    int intervalOverrideSeconds_ = 0;
    std::thread worker_;
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> refreshRequested_{false};
    std::mutex wakeMutex_;
    std::condition_variable wake_;
};

}  // namespace teletext
