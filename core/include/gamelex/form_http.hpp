#pragma once
#include "gamelex/core.hpp"
#include "gamelex/form.hpp"
#include <curl/curl.h>

namespace gamelex::form {
enum class Stage { Prepared, Performed, Released };
struct Options {
    const char* url = nullptr;
    const char* ca_bundle = nullptr;
    long timeout_seconds = 20;
    const Cancelled* cancelled = nullptr;
    void (*sample)(Stage) = nullptr;
};
struct Response {
    CURLcode code = CURLE_OK;
    long http = 0;
    bool too_large = false, cancelled = false;
    std::size_t uploaded_bytes = 0;
    char detail[CURL_ERROR_SIZE]{};
    std::string body;
};
// Fresh handle per call; all connection/TLS resources are released before
// returning. HTTPS verification is always enabled. No retries or redirects.
Response post(const Options& options, Stream& stream);
}
