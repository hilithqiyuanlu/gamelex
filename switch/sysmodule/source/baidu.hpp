#pragma once
#include "gamelex/core.hpp"
#include "gamelex/reading.hpp"
#include <string>
#include <vector>

namespace gamelex {
struct BaiduToken {
    std::string text;
    double x = 0, y = 0, w = 0, h = 0; // normalized 0..1
    int line = 0;
};
struct BaiduScan {
    std::vector<BaiduToken> tokens;
    std::vector<std::string> lines;
    std::vector<std::string> paragraphs;
    std::vector<reading::Group> groups;
    unsigned long long auth_ms=0,ocr_ms=0;
    int width = 0, height = 0;
};

// Scalar-only diagnostics; sampling must not allocate during a transfer.
struct BaiduTransfer {
    unsigned long long request=0, form_bytes=0, uploaded_bytes=0;
    unsigned long long before_heap=0, prepared_heap=0, performed_heap=0, released_heap=0, peak_heap=0;
    int curl_code=0;
    long http_code=0;
};
const BaiduTransfer& baidu_transfer_stats();

// Full-screen OCR with line positions and locally reconstructed paragraphs.
Status baidu_scan(const Config& config, const Image& image, BaiduScan& result, const Cancelled& cancelled = {});
// One text (word/phrase) to Chinese.
Status baidu_translate(const Config& config, const std::string& text, std::string& translation, const Cancelled& cancelled = {});
// Many reconstructed paragraphs in one request; never fan out per visual line.
Status baidu_translate_lines(const Config& config, const std::vector<std::string>& lines,
                             std::vector<std::string>& translations, const Cancelled& cancelled = {});
}
