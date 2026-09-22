#pragma once

#include <atomic>
#include <string>

namespace teletext {

struct FetchOptions {
    long connectTimeoutSeconds = 10;
    long totalTimeoutSeconds = 20;
    // Responses larger than this are aborted (a feed is tens to hundreds of
    // KB; anything near this size is not a feed we want).
    size_t maxBodyBytes = 8 * 1024 * 1024;
    std::string userAgent = "PVMPlayer-Teletext/1.0 (personal news reader)";
    // If set and it becomes true mid-transfer, the request is aborted
    // promptly (checked about once a second by libcurl) -- lets a background
    // thread shut down without waiting out a slow server.
    const std::atomic<bool>* cancel = nullptr;
};

struct FetchResult {
    bool ok = false;      // transport succeeded AND status is 2xx
    long status = 0;      // HTTP status, 0 if the transfer never got that far
    std::string body;
    std::string error;    // human-readable reason when !ok
};

// Blocking HTTP(S) GET via libcurl: follows redirects (http/https only),
// accepts gzip/deflate, and enforces the timeouts/size cap above. Never
// throws; safe to call from any thread (libcurl's global init is done once,
// on first use).
FetchResult httpGet(const std::string& url, const FetchOptions& options = {});

// Same, but POSTs `jsonBody` with a `Content-Type: application/json` header
// (the shape the MediathekViewWeb API expects) -- everything else (timeouts,
// size cap, cancel flag, User-Agent, redirect handling) is shared with
// httpGet() via the same curl-easy setup.
FetchResult httpPostJson(const std::string& url, const std::string& jsonBody, const FetchOptions& options = {});

}  // namespace teletext
