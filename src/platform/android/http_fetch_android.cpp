// HTTP transport for Android. Until the JNI-backed implementation
// (HttpURLConnection, called from the service threads) lands, every request
// fails cleanly: the teletext sections then show whatever is cached, exactly
// as they do on desktop when offline.

#include "teletext/http_fetch.h"

namespace teletext {

namespace {

FetchResult unavailable() {
    FetchResult result;
    result.error = "HTTP is not available in this build yet";
    return result;
}

}  // namespace

FetchResult httpGet(const std::string& /*url*/, const FetchOptions& /*options*/) {
    return unavailable();
}

FetchResult httpPostJson(const std::string& /*url*/, const std::string& /*jsonBody*/,
                         const FetchOptions& /*options*/) {
    return unavailable();
}

}  // namespace teletext
