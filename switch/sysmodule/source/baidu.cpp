#include "baidu.hpp"
#include <switch.h>
#include <curl/curl.h>
#include <mbedtls/md5.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <utility>
#include <malloc.h>
#include "gamelex/form_http.hpp"

// The pinned Switch curl backend discards this native verification result
// when returning CURLE_PEER_FAILED_VERIFICATION. Observe it without changing
// the return value or disabling any verification checks.
static Result last_verify_result = 0;
extern "C" Result __real_sslConnectionGetVerifyCertError(SslConnection* connection);
extern "C" Result __wrap_sslConnectionGetVerifyCertError(SslConnection* connection) {
    last_verify_result = __real_sslConnectionGetVerifyCertError(connection);
    return last_verify_result;
}

namespace gamelex {
namespace {
BaiduTransfer transfer_stats;
unsigned long long request_count = 0;
void sample_transfer(form::Stage stage) {
    const auto heap = mallinfo();
    const auto used = static_cast<unsigned long long>(heap.uordblks);
    transfer_stats.peak_heap = std::max(transfer_stats.peak_heap, used);
    if (stage == form::Stage::Prepared) transfer_stats.prepared_heap = used;
    if (stage == form::Stage::Performed) transfer_stats.performed_heap = used;
    if (stage == form::Stage::Released) transfer_stats.released_heap = used;
}
Status post_form(const Config& config, const std::string& url,
                 std::initializer_list<form::Field> fields, std::string& body, const Cancelled& cancelled) {
    transfer_stats = {};
    transfer_stats.request = ++request_count;
    transfer_stats.before_heap = mallinfo().uordblks;
    form::Stream stream(fields);
    transfer_stats.form_bytes = stream.length();
    form::Options options{url.c_str(), config.ca_bundle.c_str(), config.timeout_seconds, &cancelled, sample_transfer};
    last_verify_result = 0;
    auto response = form::post(options, stream);
    const auto native_result = last_verify_result;
    transfer_stats.uploaded_bytes = response.uploaded_bytes;
    transfer_stats.curl_code = response.code;
    transfer_stats.http_code = response.http;
    const auto rc = response.code;
    if (response.cancelled) return Status::error("cancelled", "已取消本次请求。");
    if (response.too_large) return Status::error("baidu_response_too_large", "返回内容过大，已停止读取。");
    if (rc == CURLE_OUT_OF_MEMORY) return Status::error("out_of_memory", "后台可用内存不足，已结束本次请求。请返回后重试。");
    if (rc == CURLE_OPERATION_TIMEDOUT) return Status::error("timeout", "联网超时，请检查网络后手动重试。");
    if (rc == CURLE_PEER_FAILED_VERIFICATION || rc == CURLE_SSL_CACERT_BADFILE || rc == CURLE_SSL_CERTPROBLEM) {
        char native[96] = {};
        std::snprintf(native, sizeof(native), " curl=%d native=0x%08X (%u/%u)",
            static_cast<int>(rc), native_result, R_MODULE(native_result), R_DESCRIPTION(native_result));
        std::string detail = response.detail;
        // A backend message may contain a URL: strip our request query and all
        // configured credential values before passing it to the UI or log.
        std::vector<std::string> secrets{config.ocr_api_key, config.ocr_secret,
            config.translate_appid, config.translate_key, config.api_key};
        const auto query = url.find('?');
        if (query != std::string::npos) secrets.push_back(url.substr(query+1));
        const auto token = url.find("access_token=");
        if (token != std::string::npos) secrets.push_back(url.substr(token+13));
        for (const auto& secret : secrets) if (!secret.empty()) {
            std::size_t pos=0;
            while ((pos=detail.find(secret,pos))!=std::string::npos) { detail.replace(pos,secret.size(),"[redacted]"); pos+=10; }
        }
        return Status::error("tls_failed", std::string("Baidu TLS failed:") + native +
            (detail.empty() ? "" : " " + detail));
    }
    if (rc != CURLE_OK) return Status::error("baidu_network", std::string("联网失败：") + curl_easy_strerror(rc));
    if (response.http != 200) return Status::error("baidu_http", "服务返回 HTTP " + to_text(static_cast<unsigned long long>(response.http)));
    body = std::move(response.body);
    return {};
}

Status access_token(const Config& config, std::string& token, const Cancelled& cancelled) {
    static std::string cached;
    if (!cached.empty()) { token = cached; return {}; }
    std::string body;
    auto status = post_form(config, "https://aip.baidubce.com/oauth/2.0/token",
        {{"grant_type", "client_credentials"}, {"client_id", config.ocr_api_key},
         {"client_secret", config.ocr_secret}}, body, cancelled);
    if (!status.ok()) return status;
    Json json;
    if (!parse_json(body, json).ok())
        return Status::error("ocr_auth_failed", "Baidu token response is not JSON");
    if (json.contains("error")) {
        const auto error = json["error"].is_string() ? json["error"].get<std::string>() : json["error"].dump();
        const auto description = json.contains("error_description") && json["error_description"].is_string()
            ? json["error_description"].get<std::string>() : "";
        return Status::error("ocr_auth_failed", "Baidu token " + error + (description.empty() ? "" : ": " + description));
    }
    if (!json.contains("access_token") || !json["access_token"].is_string())
        return Status::error("ocr_auth_failed", "Baidu token response has no access_token");
    cached = json["access_token"].get<std::string>();
    token = cached;
    return {};
}

Status ocr_once(const Config& config, const Image& image, BaiduScan& result, const Cancelled& cancelled) {
    result={};
    const auto started=armTicksToNs(svcGetSystemTick())/1000000;
    std::string token;
    auto status = access_token(config, token, cancelled);
    const auto authorized=armTicksToNs(svcGetSystemTick())/1000000;
    result.auth_ms=authorized-started;
    if (!status.ok()) return status;
    std::string body;
    status = post_form(config,
        "https://aip.baidubce.com/rest/2.0/ocr/v1/general?access_token=" + token,
        {form::Field::jpeg("image", image.jpeg.data(), image.jpeg.size()), {"language_type", "ENG"},
         {"recognize_granularity", "big"}, {"detect_direction", "true"}}, body, cancelled);
    result.ocr_ms=armTicksToNs(svcGetSystemTick())/1000000-authorized;
    if (!status.ok()) return status;
    if (config.save_last_capture) {
        // Match the explicitly enabled last-image diagnostic: one bounded
        // raw OCR reply, overwritten each scan, never logged or packaged.
        if (FILE* file=std::fopen("sdmc:/config/gamelex/runtime/last-ocr.json","wb")) {
            unsigned long long fingerprint=14695981039346656037ULL;
            for (const auto byte:image.jpeg) { fingerprint^=byte;fingerprint*=1099511628211ULL; }
            std::fprintf(file,"{\"image_bytes\":%zu,\"image_fingerprint\":\"%016llX\",\"response\":",image.jpeg.size(),fingerprint);
            std::fwrite(body.data(),1,body.size(),file);std::fputs("}\n",file);std::fclose(file);
        }
    }
    Json json;
    if (!parse_json(body, json).ok()) return Status::error("ocr_invalid_response", "Baidu OCR returned malformed JSON");
    if (json.contains("error_code")) {
        const auto code = json["error_code"].is_string() ? json["error_code"].get<std::string>() : json["error_code"].dump();
        const auto message = json.contains("error_msg") && json["error_msg"].is_string() ? json["error_msg"].get<std::string>() : "";
        return Status::error("ocr_failed", "Baidu OCR " + code + (message.empty() ? "" : ": " + message));
    }
    if (!json.contains("words_result") || !json["words_result"].is_array())
        return Status::error("ocr_invalid_response", "Baidu OCR response has no words_result");
    result.width = image.width;
    result.height = image.height;
    if (result.width <= 0 || result.height <= 0) return Status::error("ocr_invalid_image", "Screenshot dimensions are invalid");
    const auto& lines = json["words_result"];
    std::vector<reading::Region> regions;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const auto& line = lines[index];
        if (!line.is_object()) continue;
        std::string line_text;
        if (line.contains("words") && line["words"].is_string()) line_text = line["words"].get<std::string>();
        result.lines.push_back(line_text);
        reading::Region region{line_text};
        if (line.contains("location") && line["location"].is_object()) {
            const auto& loc=line["location"];
            if (loc.contains("left") && loc["left"].is_number() && loc.contains("top") && loc["top"].is_number() &&
                loc.contains("width") && loc["width"].is_number() && loc.contains("height") && loc["height"].is_number()) {
                region.x=loc["left"].get<double>()/result.width; region.y=loc["top"].get<double>()/result.height;
                region.w=loc["width"].get<double>()/result.width; region.h=loc["height"].get<double>()/result.height;
            }
        }
        regions.push_back(region);
        if (region.w>0 && region.h>0 && !region.text.empty() && result.tokens.size()<400)
            result.tokens.push_back({region.text,region.x,region.y,region.w,region.h,static_cast<int>(index)});
    }
    result.groups=reading::groups(std::move(regions),static_cast<double>(result.height)/result.width);
    for (const auto& group:result.groups) result.paragraphs.push_back(group.text);
    return {};
}

Status translate_once(const Config& config, const std::string& query,
                      std::vector<std::string>& translations, const Cancelled& cancelled) {
    char salt[32];
    std::snprintf(salt, sizeof(salt), "%llu", static_cast<unsigned long long>(svcGetSystemTick() % 1000000000ULL));
    const std::string signed_text = config.translate_appid + query + salt + config.translate_key;
    unsigned char digest[16];
    mbedtls_md5(reinterpret_cast<const unsigned char*>(signed_text.data()), signed_text.size(), digest);
    char sign[33];
    for (int i = 0; i < 16; ++i) std::snprintf(sign + i * 2, 3, "%02x", digest[i]);
    std::string body;
    auto status = post_form(config, "https://fanyi-api.baidu.com/api/trans/vip/translate",
        {{"q", query}, {"from", "en"}, {"to", "zh"}, {"appid", config.translate_appid},
         {"salt", salt}, {"sign", sign}}, body, cancelled);
    if (!status.ok()) return status;
    Json json;
    if (!parse_json(body, json).ok()) return Status::error("translate_invalid_response", "Baidu translate returned malformed JSON");
    if (json.contains("error_code")) {
        const auto code = json["error_code"].is_string() ? json["error_code"].get<std::string>() : json["error_code"].dump();
        return Status::error("translate_failed", "Baidu translate " + code);
    }
    if (!json.contains("trans_result") || !json["trans_result"].is_array())
        return Status::error("translate_invalid_response", "Baidu translate response has no trans_result");
    translations.clear();
    for (const auto& item : json["trans_result"]) {
        if (item.is_object() && item.contains("dst") && item["dst"].is_string())
            translations.push_back(item["dst"].get<std::string>());
    }
    if (translations.empty()) return Status::error("translate_invalid_response", "Baidu translate returned no text");
    return {};
}
}

const BaiduTransfer& baidu_transfer_stats() { return transfer_stats; }

Status baidu_scan(const Config& config, const Image& image, BaiduScan& result, const Cancelled& cancelled) {
    if (config.ocr_api_key.empty() || config.ocr_secret.empty())
        return Status::error("ocr_not_configured", "尚未配置文字识别服务，请按随包说明填写 OCR 的两项密钥。");
    if (image.jpeg.empty() || image.width <= 0 || image.height <= 0)
        return Status::error("ocr_invalid_image", "No screenshot to recognize");
    return ocr_once(config, image, result, cancelled);
}

Status baidu_translate(const Config& config, const std::string& text, std::string& translation, const Cancelled& cancelled) {
    if (config.translate_appid.empty() || config.translate_key.empty())
        return Status::error("translate_not_configured", "尚未配置翻译服务，请在 config/gamelex/config.json 填写翻译应用编号和密钥。");
    if (text.empty() || text.size() > 2000) return Status::error("translate_invalid_text", "Text is empty or too long");
    std::vector<std::string> result;
    const auto status = translate_once(config, text, result, cancelled);
    if (!status.ok()) return status;
    translation = result.front();
    return {};
}

Status baidu_translate_lines(const Config& config, const std::vector<std::string>& lines,
                             std::vector<std::string>& translations, const Cancelled& cancelled) {
    if (config.translate_appid.empty() || config.translate_key.empty())
        return Status::error("translate_not_configured", "尚未配置翻译服务，请在 config/gamelex/config.json 填写翻译应用编号和密钥。");
    std::string query;
    for (const auto& line : lines) {
        if (!query.empty()) query.push_back('\n');
        query += line;
    }
    if (query.empty() || query.size() > 5000) return Status::error("translate_invalid_text", "Lines are empty or too long");
    static std::string previous;
    static std::vector<std::string> cached;
    if (query==previous && !cached.empty()) { translations=cached; return {}; }
    const auto status = translate_once(config, query, translations, cancelled);
    if (!status.ok()) return status;
    previous=query; cached=translations;
    // The reader displays a translated document, not source/target row pairs.
    // Accept the provider's paragraph boundaries; never fan out a batch into
    // one paid request per OCR line just because its segmentation differs.
    return {};
}
}
