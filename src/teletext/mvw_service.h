#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "mvw_config.h"
#include "mvw_pages.h"
#include "data_service.h"
#include "page.h"

namespace teletext {

// Owns one broadcaster's MediathekViewWeb data (ARD, ZDF, ... -- see
// MvwConfig::channel): fetches every favorite's episode list plus the
// generated A-Z window, keeps the last good result of each (in memory and
// on disk), and turns it into an immutable PageStore snapshot -- same
// threading model as NewsService (see its own header comment: one worker
// thread owns all mutable state, publishes by an atomic shared_ptr swap,
// the render thread only ever calls snapshot()).
//
// Simplification versus a fully lazy "fetch only when that show's page is
// opened" design: like NewsService, this fetches everything once at
// start() and again every config.refreshMinutes (default 60, much longer
// than NEWS' 15 since a Mediathek catalog changes far more slowly than a
// news ticker) -- not only on demand. requestRefresh() (the blue key) still
// forces an immediate re-fetch, and the disk cache still means the section
// has real pages before the first live fetch completes.
class MvwService : public TeletextDataService {
public:
    // `cacheDir` may be empty to disable the disk cache.
    MvwService(MvwConfig config, std::string cacheDir);
    ~MvwService() override;

    MvwService(const MvwService&) = delete;
    MvwService& operator=(const MvwService&) = delete;

    std::shared_ptr<const PageStore> snapshot() const override { return std::atomic_load(&snapshot_); }
    std::string serviceName() const override { return config_.serviceName; }
    void requestRefresh() override;

    // Starts the background refresh loop. Idempotent.
    void start();
    // Stops the loop (aborting any transfer in flight) and joins the thread.
    void stop();

    // Fetches everything once on the calling thread. Only for use when the
    // background thread is not running (command-line tools, tests).
    void refreshAll();

    void setRefreshIntervalSecondsForTesting(int seconds) { intervalOverrideSeconds_ = seconds; }

    const MvwConfig& config() const { return config_; }

    // Cache file name (within the cache directory) for a query key -- a
    // favorite's "favorite:<topic>", or the fixed A-Z window key. Exposed
    // (like NewsService::cacheFileName) so a test can write a cache file by
    // hand in the exact shape MvwService itself would.
    static std::string cacheFileName(const std::string& key);
    // The fixed key the generated A-Z window is cached under.
    static const char* azCacheKey();

private:
    bool fetchFavorite(size_t index);
    bool fetchAzWindow();
    void publish();
    void runLoop();

    void loadCache();
    void saveCache(const std::string& key, const std::vector<MvwItem>& items, std::time_t fetchedAt) const;
    bool loadCacheEntry(const std::string& key, MvwShowData& data) const;
    std::string cachePath(const std::string& key) const;

    MvwConfig config_;
    std::string cacheDir_;
    std::vector<MvwShowData> favoritesData_;  // worker thread only, once started
    MvwShowData azData_;
    std::shared_ptr<const PageStore> snapshot_;

    int intervalOverrideSeconds_ = 0;
    std::thread worker_;
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> refreshRequested_{false};
    std::mutex wakeMutex_;
    std::condition_variable wake_;
};

}  // namespace teletext
