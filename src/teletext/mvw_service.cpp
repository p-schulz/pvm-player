#include "mvw_service.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "mvw_api.h"

namespace fs = std::filesystem;

namespace teletext {

namespace {

// Pause between consecutive requests within one refresh round, so a round
// is a trickle of requests rather than a burst -- same politeness pattern
// as NewsService.
constexpr auto kPauseBetweenRequests = std::chrono::milliseconds(750);

// A favorite's own per-show query only needs recent-enough episodes to fill
// its page budget with room to spare; MediathekViewWeb is fast even at much
// larger sizes (measured), so this is a comfortable margin, not a limit.
constexpr int kFavoriteQuerySize = 300;

constexpr const char* kCacheMagic = "PVMMVW1";
constexpr const char* kAzCacheKey = "__all_shows__";

std::string hashHex(const std::string& s) {
    std::uint64_t h = 1469598103934665603ULL;  // FNV-1a
    for (unsigned char c : s) {
        h = (h ^ c) * 1099511628211ULL;
    }
    char buf[20];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

nlohmann::json itemToJson(const MvwItem& item) {
    return {
        {"title", item.title},
        {"topic", item.topic},
        {"channel", item.channel},
        {"description", item.description},
        {"timestamp", static_cast<long long>(item.timestamp)},
        {"duration", item.durationSeconds},
        {"url_video", item.videoUrl},
        {"season", item.season},
        {"episode", item.episode},
        {"accessibility_variant", item.accessibilityVariant},
    };
}

// Reconstructs an item from a cached entry rather than re-deriving
// season/episode/accessibilityVariant from the title (cheap either way, but
// this keeps the cache format self-contained if that parsing ever changes).
MvwItem itemFromJson(const nlohmann::json& j) {
    MvwItem item;
    item.title = j.value("title", "");
    item.topic = j.value("topic", "");
    item.channel = j.value("channel", "");
    item.description = j.value("description", "");
    item.timestamp = static_cast<std::time_t>(j.value<long long>("timestamp", 0));
    item.durationSeconds = j.value("duration", 0);
    item.videoUrl = j.value("url_video", "");
    item.season = j.value("season", 0);
    item.episode = j.value("episode", 0);
    item.accessibilityVariant = j.value("accessibility_variant", false);
    return item;
}

}  // namespace

MvwService::MvwService(MvwConfig config, std::string cacheDir)
    : config_(std::move(config)), cacheDir_(std::move(cacheDir)), favoritesData_(config_.favorites.size()) {
    loadCache();
    publish();
}

MvwService::~MvwService() {
    stop();
}

void MvwService::publish() {
    std::atomic_store(&snapshot_, buildMvwPageStore(config_, favoritesData_, azData_));
}

// ---- disk cache -------------------------------------------------------
// One JSON file per query (a favorite's topic, or the fixed "__all_shows__"
// key for the A-Z window): {"fetchedAt": <epoch>, "items": [...]}. Caching
// the parsed items (unlike NewsService, which caches raw feed XML) is fine
// here -- MediathekViewWeb's JSON shape is already this app's own
// long-term storage shape, there's no upstream markup to reprocess.

std::string MvwService::cacheFileName(const std::string& key) {
    return hashHex(key) + ".mvw";
}

const char* MvwService::azCacheKey() {
    return kAzCacheKey;
}

std::string MvwService::cachePath(const std::string& key) const {
    return cacheDir_ + "/" + cacheFileName(key);
}

void MvwService::saveCache(const std::string& key, const std::vector<MvwItem>& items, std::time_t fetchedAt) const {
    if (cacheDir_.empty()) {
        return;
    }
    std::error_code ec;
    fs::create_directories(cacheDir_, ec);
    const std::string finalPath = cachePath(key);
    const std::string tempPath = finalPath + ".tmp";
    {
        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            std::fprintf(stderr, "[%s] cannot write cache file %s\n", config_.channel.c_str(), tempPath.c_str());
            return;
        }
        nlohmann::json items_json = nlohmann::json::array();
        for (const MvwItem& item : items) {
            items_json.push_back(itemToJson(item));
        }
        const nlohmann::json doc = {
            {"magic", kCacheMagic}, {"key", key}, {"fetchedAt", static_cast<long long>(fetchedAt)}, {"items", items_json}};
        out << doc.dump();
        if (!out) {
            std::fprintf(stderr, "[%s] failed writing cache file %s\n", config_.channel.c_str(), tempPath.c_str());
            fs::remove(tempPath, ec);
            return;
        }
    }
    fs::rename(tempPath, finalPath, ec);  // atomic-ish: a crash mid-write can't leave a torn cache
    if (ec) {
        std::fprintf(stderr, "[%s] cannot replace cache file %s: %s\n", config_.channel.c_str(), finalPath.c_str(),
                     ec.message().c_str());
        fs::remove(tempPath, ec);
    }
}

bool MvwService::loadCacheEntry(const std::string& key, MvwShowData& data) const {
    if (cacheDir_.empty()) {
        return false;
    }
    std::ifstream in(cachePath(key), std::ios::binary);
    if (!in) {
        return false;
    }
    std::stringstream body;
    body << in.rdbuf();
    try {
        const nlohmann::json doc = nlohmann::json::parse(body.str());
        if (doc.value("magic", "") != kCacheMagic || doc.value("key", "") != key) {
            return false;  // foreign or damaged file: behave as if there were no cache
        }
        std::vector<MvwItem> items;
        for (const auto& entry : doc.at("items")) {
            items.push_back(itemFromJson(entry));
        }
        if (items.empty()) {
            return false;
        }
        data.items = std::move(items);
        data.fetchedAt = static_cast<std::time_t>(doc.value<long long>("fetchedAt", 0));
        data.status = MvwFetchStatus::Ok;
        return true;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

void MvwService::loadCache() {
    for (size_t i = 0; i < config_.favorites.size(); ++i) {
        if (loadCacheEntry("favorite:" + config_.favorites[i].topic, favoritesData_[i])) {
            std::fprintf(stdout, "[%s] %s: %zu items from cache\n", config_.channel.c_str(),
                         config_.favorites[i].topic.c_str(), favoritesData_[i].items.size());
        }
    }
    if (loadCacheEntry(kAzCacheKey, azData_)) {
        std::fprintf(stdout, "[%s] all shows: %zu items from cache\n", config_.channel.c_str(), azData_.items.size());
    }
}

// ---- fetching -----------------------------------------------------------

bool MvwService::fetchFavorite(size_t index) {
    const MvwFavorite& fav = config_.favorites[index];
    MvwShowData& data = favoritesData_[index];

    const MvwQueryResult fetched = queryMediathek(fav.channel, fav.topic, kFavoriteQuerySize, /*newestFirst=*/true);
    if (stopRequested_) {
        return false;  // shutting down mid-transfer: not a real failure, nothing to report
    }
    if (!fetched.ok) {
        std::fprintf(stderr, "[%s] %s: %s\n", config_.channel.c_str(), fav.topic.c_str(), fetched.error.c_str());
        data.status = MvwFetchStatus::Failed;
        return false;
    }

    data.items = dropAccessibilityDuplicates(fetched.items);
    data.fetchedAt = std::time(nullptr);
    data.status = MvwFetchStatus::Ok;
    std::fprintf(stdout, "[%s] %s: %zu items\n", config_.channel.c_str(), fav.topic.c_str(), data.items.size());
    saveCache("favorite:" + fav.topic, data.items, data.fetchedAt);
    return true;
}

bool MvwService::fetchAzWindow() {
    // Deliberately scoped to this section's own channel regardless of any
    // favorite's own channel override -- the generated index stays a
    // single, manageable feed rather than pulling in every regional
    // broadcaster's entire catalog. See mvw_config.h's comment.
    MvwQueryResult fetched = queryMediathek(config_.channel, "", config_.azQuerySize, /*newestFirst=*/true);
    if (stopRequested_) {
        return false;
    }
    if (!fetched.ok) {
        std::fprintf(stderr, "[%s] all shows: %s\n", config_.channel.c_str(), fetched.error.c_str());
        azData_.status = MvwFetchStatus::Failed;
        return false;
    }

    const std::time_t cutoff = std::time(nullptr) - static_cast<std::time_t>(config_.azWindowDays) * 86400;
    std::vector<MvwItem> recent;
    for (MvwItem& item : fetched.items) {
        if (item.timestamp == 0 || item.timestamp >= cutoff) {
            recent.push_back(std::move(item));
        }
    }

    azData_.items = dropAccessibilityDuplicates(std::move(recent));
    azData_.fetchedAt = std::time(nullptr);
    azData_.status = MvwFetchStatus::Ok;
    std::fprintf(stdout, "[%s] all shows: %zu items (last %d days)\n", config_.channel.c_str(), azData_.items.size(),
                 config_.azWindowDays);
    saveCache(kAzCacheKey, azData_.items, azData_.fetchedAt);
    return true;
}

void MvwService::refreshAll() {
    for (size_t i = 0; i < config_.favorites.size(); ++i) {
        fetchFavorite(i);
    }
    fetchAzWindow();
    publish();
}

// ---- background loop ------------------------------------------------------

void MvwService::start() {
    if (worker_.joinable()) {
        return;
    }
    stopRequested_ = false;
    worker_ = std::thread([this] { runLoop(); });
}

void MvwService::stop() {
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

void MvwService::requestRefresh() {
    {
        std::lock_guard<std::mutex> lock(wakeMutex_);
        refreshRequested_ = true;
    }
    wake_.notify_all();
}

void MvwService::runLoop() {
    const auto sleepFor = [this](std::chrono::milliseconds duration) {
        std::unique_lock<std::mutex> lock(wakeMutex_);
        wake_.wait_for(lock, duration, [this] { return stopRequested_.load() || refreshRequested_.load(); });
        refreshRequested_ = false;
        return !stopRequested_;
    };

    const auto interval = intervalOverrideSeconds_ > 0
                              ? std::chrono::seconds(intervalOverrideSeconds_)
                              : std::chrono::minutes(std::max(config_.refreshMinutes, kMvwMinRefreshMinutes));
    const auto retryInterval = intervalOverrideSeconds_ > 0
                                   ? interval
                                   : std::min<std::chrono::seconds>(interval, std::chrono::minutes(kMvwMinRefreshMinutes));

    while (!stopRequested_) {
        bool anyFailed = false;
        for (size_t i = 0; i < config_.favorites.size() && !stopRequested_; ++i) {
            anyFailed |= !fetchFavorite(i);
            publish();
            if (!sleepFor(kPauseBetweenRequests)) {
                return;
            }
        }
        if (!stopRequested_) {
            anyFailed |= !fetchAzWindow();
            publish();
        }
        if (!sleepFor(std::chrono::duration_cast<std::chrono::milliseconds>(anyFailed ? retryInterval : interval))) {
            break;
        }
    }
}

}  // namespace teletext
