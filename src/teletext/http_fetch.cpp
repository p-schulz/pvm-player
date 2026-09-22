#include "http_fetch.h"

#include <curl/curl.h>

#include <mutex>

namespace teletext {

namespace {

void ensureCurlGlobalInit() {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

struct WriteContext {
    std::string* body;
    size_t maxBytes;
    bool overflowed = false;
};

size_t writeCallback(char* data, size_t size, size_t count, void* userdata) {
    auto* ctx = static_cast<WriteContext*>(userdata);
    const size_t bytes = size * count;
    if (ctx->body->size() + bytes > ctx->maxBytes) {
        ctx->overflowed = true;
        return 0;  // tells libcurl to abort the transfer
    }
    ctx->body->append(data, bytes);
    return bytes;
}

int progressCallback(void* userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    const auto* cancel = static_cast<const std::atomic<bool>*>(userdata);
    return cancel && cancel->load() ? 1 : 0;  // nonzero aborts the transfer
}

// Shared setup for both httpGet() and httpPostJson(); `postBody` is null for
// a GET, pointing at the JSON body otherwise.
FetchResult httpRequest(const std::string& url, const FetchOptions& options, const std::string* postBody) {
    ensureCurlGlobalInit();

    FetchResult result;
    CURL* curl = curl_easy_init();
    if (!curl) {
        result.error = "curl_easy_init failed";
        return result;
    }

    WriteContext ctx{&result.body, options.maxBodyBytes};
    char errorBuffer[CURL_ERROR_SIZE] = {0};
    curl_slist* headers = nullptr;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, options.userAgent.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
#if LIBCURL_VERSION_NUM >= 0x075500  // CURLOPT_PROTOCOLS_STR arrived in 7.85.0
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");  // all encodings libcurl was built with
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, options.connectTimeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, options.totalTimeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);  // required when called off the main thread
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
    if (options.cancel) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, const_cast<std::atomic<bool>*>(options.cancel));
    }
    if (postBody) {
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postBody->data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(postBody->size()));
    }

    const CURLcode code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status);
    curl_easy_cleanup(curl);
    if (headers) {
        curl_slist_free_all(headers);
    }

    if (ctx.overflowed) {
        result.error = "response larger than the configured size limit";
    } else if (code == CURLE_ABORTED_BY_CALLBACK) {
        result.error = "cancelled";
    } else if (code != CURLE_OK) {
        result.error = errorBuffer[0] ? errorBuffer : curl_easy_strerror(code);
    } else if (result.status < 200 || result.status >= 300) {
        result.error = "HTTP status " + std::to_string(result.status);
    } else {
        result.ok = true;
    }
    if (!result.ok) {
        result.body.clear();
    }
    return result;
}

}  // namespace

FetchResult httpGet(const std::string& url, const FetchOptions& options) {
    return httpRequest(url, options, nullptr);
}

FetchResult httpPostJson(const std::string& url, const std::string& jsonBody, const FetchOptions& options) {
    return httpRequest(url, options, &jsonBody);
}

}  // namespace teletext
