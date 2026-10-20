#include "gamelex/form_http.hpp"
#include <algorithm>
#include <limits>
#include <memory>

namespace gamelex::form {
namespace {
constexpr std::size_t MaxResponse = 256 * 1024;
struct Transfer {
    Stream& stream;
    const Options& options;
    Response& response;
};
bool cancelled(Transfer& t) {
    if (t.options.cancelled && *t.options.cancelled && (*t.options.cancelled)()) t.response.cancelled = true;
    return t.response.cancelled;
}
size_t send(char* bytes, size_t size, size_t count, void* data) {
    auto& t = *static_cast<Transfer*>(data);
    if (cancelled(t) || (size && count > std::numeric_limits<size_t>::max() / size)) return CURL_READFUNC_ABORT;
    const auto length = t.stream.read(bytes, size * count);
    t.response.uploaded_bytes += length;
    return length;
}
size_t receive(char* bytes, size_t size, size_t count, void* data) {
    auto& t = *static_cast<Transfer*>(data);
    if (cancelled(t)) return 0;
    if (size && count > MaxResponse / size) { t.response.too_large = true; return 0; }
    const auto length = size * count;
    if (length > MaxResponse - t.response.body.size()) { t.response.too_large = true; return 0; }
    t.response.body.append(bytes, length);
    return length;
}
int progress(void* data, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    return cancelled(*static_cast<Transfer*>(data)) ? 1 : 0;
}
}
Response post(const Options& options, Stream& stream) {
    Response result;
    Transfer transfer{stream, options, result};
    if (cancelled(transfer)) { result.code = CURLE_ABORTED_BY_CALLBACK; return result; }
    if (!stream.valid() || !options.url || stream.length() > static_cast<std::size_t>(std::numeric_limits<curl_off_t>::max())) {
        result.code = CURLE_BAD_FUNCTION_ARGUMENT; return result;
    }
    // One-time library initialization; no persistent easy/multi handle or
    // connection/session cache. A failed request follows the same cleanup path.
    static bool ready = false;
    if (!ready) {
        result.code = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (result.code != CURLE_OK) return result;
        ready = true;
    }
    {
        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
        if (!curl) { result.code = CURLE_OUT_OF_MEMORY; return result; }
        curl_slist* raw = curl_slist_append(nullptr, "Content-Type: application/x-www-form-urlencoded");
        auto* next = raw ? curl_slist_append(raw, "Expect:") : nullptr;
        if (!next) { curl_slist_free_all(raw); result.code = CURLE_OUT_OF_MEMORY; return result; }
        std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(next, curl_slist_free_all);
        stream.rewind();
        // Check every setup result: an allocation failure must not accidentally
        // send a different request or fall back to unchecked TLS.
        auto set = [&](CURLcode rc) { if (result.code == CURLE_OK) result.code = rc; };
        set(curl_easy_setopt(curl.get(), CURLOPT_URL, options.url));
        set(curl_easy_setopt(curl.get(), CURLOPT_ERRORBUFFER, result.detail));
        set(curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get()));
        set(curl_easy_setopt(curl.get(), CURLOPT_POST, 1L));
        set(curl_easy_setopt(curl.get(), CURLOPT_READFUNCTION, send));
        set(curl_easy_setopt(curl.get(), CURLOPT_READDATA, &transfer));
        set(curl_easy_setopt(curl.get(), CURLOPT_UPLOAD_BUFFERSIZE, 16L * 1024));
        set(curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(stream.length())));
        set(curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, receive));
        set(curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &transfer));
        set(curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, progress));
        set(curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &transfer));
        set(curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L));
        set(curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, options.timeout_seconds));
        set(curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, std::min(options.timeout_seconds, 10L)));
        set(curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L));
        set(curl_easy_setopt(curl.get(), CURLOPT_FORBID_REUSE, 1L));
        set(curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 0L));
        set(curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L));
        set(curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L));
#if LIBCURL_VERSION_NUM >= 0x075500
        set(curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS_STR, "https"));
#else
        set(curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS, CURLPROTO_HTTPS));
#endif
        if (options.ca_bundle && options.ca_bundle[0]) set(curl_easy_setopt(curl.get(), CURLOPT_CAINFO, options.ca_bundle));
        if (options.sample) options.sample(Stage::Prepared);
        if (result.code == CURLE_OK) result.code = curl_easy_perform(curl.get());
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &result.http);
        if (options.sample) options.sample(Stage::Performed);
    }
    if (options.sample) options.sample(Stage::Released);
    return result;
}
}
