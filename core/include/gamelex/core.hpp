#pragma once
#include "json.hpp"
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace gamelex {
using Json = nlohmann::json;
constexpr std::size_t MaxJsonBytes = 128 * 1024;
constexpr std::size_t MaxImageBytes = 4 * 1024 * 1024;
constexpr std::size_t MaxResponseBytes = 128 * 1024;

struct Status {
    std::string code;
    std::string message;
    bool ok() const { return code.empty(); }
    static Status error(std::string code, std::string message) {
        return {std::move(code), std::move(message)};
    }
};
struct Rect { double x = 0, y = 0, width = 1, height = 1; };
struct PixelRect { int x, y, width, height; };
struct Config {
    std::string endpoint, api_key, model, ca_bundle;
    // Baidu OCR / translate credentials (phase 2 mode). Empty in the old probe.
    std::string ocr_api_key, ocr_secret, translate_appid, translate_key;
    int timeout_seconds = 20;
    bool save_last_capture = false;
    Rect probe_crop{0, .65, 1, .3};
    // Empty by default: the sysmodule keeps Config as a global, and dynamic
    // vector initialization there runs before main. parse_config supplies the
    // defaults when the fields are absent.
    std::vector<std::string> capture_combo;
    std::vector<std::string> input_combo;
};
struct Translation { std::string source_text, translation; };
struct Image { std::vector<unsigned char> jpeg; int width = 0, height = 0; };

Status parse_json(const std::string& text, Json& value);
Status parse_config(const Json& value, Config& config, bool require_service = true);
Status validate_crop(const Rect& rect);
Status pixel_crop(const Rect& rect, int width, int height, PixelRect& pixels);
std::string base64(const std::vector<unsigned char>& bytes);
// Formatting helper: avoids std::to_string and the libstdc++ locale static
// initializer that comes with it on the sysmodule build.
std::string to_text(unsigned long long value);
Json image_request(const Config& config, const Image& image);
Status parse_translation(const std::string& body, Translation& result);
Status read_json(const std::string& path, Json& value);
Status atomic_json(const std::string& path, const Json& value);
// Remove a logical message and both publication slots (not a user config).
Status remove_json(const std::string& path);
bool valid_id(const std::string& id);
std::string unique_id();
Status validate_request(const Json& value);
Status crop_jpeg(const std::vector<unsigned char>& jpeg, const Rect& roi, Image& image,
                 const std::function<void()>& sample_memory = {});
// A trusted system screenshot needs only header validation, not re-encoding.
Status adopt_jpeg(std::vector<unsigned char>&& jpeg, Image& image);

// The callback must remain non-blocking. Returning true aborts the transfer.
using Cancelled = std::function<bool()>;
Status query_image(const Config& config, const Image& image, Translation& result,
                   const Cancelled& cancelled = {});
}
