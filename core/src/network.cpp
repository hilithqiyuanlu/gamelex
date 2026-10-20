#include "gamelex/core.hpp"
#include <algorithm>
#include <curl/curl.h>
#include <memory>

namespace gamelex {
namespace {
struct Transfer {
    std::string response;
    const Cancelled* cancelled;
    bool too_large = false;
    bool callback_failed = false;
};
size_t receive(char* bytes, size_t size, size_t count, void* data) {
    auto& t = *static_cast<Transfer*>(data);
    if (count > MaxResponseBytes || size > MaxResponseBytes ||
        (size && count > MaxResponseBytes / size)) { t.too_large = true; return 0; }
    const auto length = size * count;
    if (length > MaxResponseBytes - t.response.size()) { t.too_large = true; return 0; }
    t.response.append(bytes, length);
    return length;
}
int progress(void* data, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto& t = *static_cast<Transfer*>(data);
    return t.cancelled && *t.cancelled && (*t.cancelled)() ? 1 : 0;
}
}
Status query_image(const Config& c, const Image& image, Translation& result, const Cancelled& cancelled) {
    Json check{{"schema_version", 1}, {"endpoint", c.endpoint}, {"api_key", c.api_key},
               {"model", c.model}, {"ca_bundle", c.ca_bundle}, {"timeout_seconds", c.timeout_seconds}};
    Config validated;
    auto status = parse_config(check, validated);
    if (!status.ok()) return status;
    if (image.jpeg.empty() || image.jpeg.size() > MaxImageBytes)
        return Status::error("invalid_image", "No valid cropped image to upload");
    if (cancelled && cancelled()) return Status::error("cancelled", "Request cancelled");
    const std::string body = image_request(c, image).dump();
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl) return Status::error("network_init_failed", "Unable to allocate HTTPS client");
    curl_slist* raw_headers = nullptr;
    raw_headers = curl_slist_append(raw_headers, "Content-Type: application/json");
    const auto authorization = "Authorization: Bearer " + c.api_key;
    auto* appended = curl_slist_append(raw_headers, authorization.c_str());
    if (!appended) { curl_slist_free_all(raw_headers); return Status::error("out_of_memory", "Unable to allocate request headers"); }
    raw_headers = appended;
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(raw_headers, curl_slist_free_all);
    Transfer transfer{{}, &cancelled, false};
    curl_easy_setopt(curl.get(), CURLOPT_URL, c.endpoint.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &transfer);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, progress);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &transfer);
    curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, static_cast<long>(c.timeout_seconds));
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, static_cast<long>(std::min(c.timeout_seconds, 10)));
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS_STR, "https");
#else
    curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
#endif
    if (!c.ca_bundle.empty()) curl_easy_setopt(curl.get(), CURLOPT_CAINFO, c.ca_bundle.c_str());
    const auto rc = curl_easy_perform(curl.get());
    if (transfer.callback_failed) return Status::error("callback_failed", "Request stopped because a progress or receive callback failed");
    if (cancelled && cancelled()) return Status::error("cancelled", "Request cancelled");
    if (transfer.too_large) return Status::error("response_too_large", "Service response exceeds limit");
    if (rc == CURLE_OPERATION_TIMEDOUT) return Status::error("timeout", "Request timed out; retry manually");
    if (rc == CURLE_PEER_FAILED_VERIFICATION || rc == CURLE_SSL_CACERT_BADFILE)
        return Status::error("tls_failed", "Certificate validation failed; check CA bundle and system clock");
    if (rc != CURLE_OK) return Status::error("network_failed", "HTTPS request failed; check network and service configuration");
    long http = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &http);
    if (http == 401 || http == 403) return Status::error("auth_failed", "Service rejected credentials or access");
    if (http == 429) return Status::error("rate_limited", "Service quota or rate limit reached");
    if (http != 200) return Status::error("http_failed", "Service returned HTTP " + to_text(static_cast<unsigned long long>(http)));
    return parse_translation(transfer.response, result);
}
}
