#include "gamelex/core.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <curl/curl.h>

using namespace gamelex;
int main(int argc, char** argv) {
    if (argc < 3 || argc > 4) {
        std::cerr << "Usage: gamelex-probe CONFIG_JSON JPEG [CANCEL_AFTER_MS]\n";
        return 2;
    }
    Json j;
    Config config;
    auto status = read_json(argv[1], j);
    if (status.ok()) status = parse_config(j, config);
    Image crop;
    if (status.ok()) {
        std::ifstream input(argv[2], std::ios::binary);
        if (!input) status = Status::error("read_failed", "Cannot read input JPEG");
        else {
            std::vector<unsigned char> bytes(MaxImageBytes + 1);
            input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
            bytes.resize(input.gcount());
            status = crop_jpeg(bytes, config.probe_crop, crop);
        }
    }
    Translation result;
    const auto started = std::chrono::steady_clock::now();
    int cancel_after = -1;
    if (argc == 4) {
        char* end = nullptr;
        const auto n = std::strtol(argv[3], &end, 10);
        if (!end || *end || n < 0 || n > 60000) return 2;
        cancel_after = static_cast<int>(n);
    }
    if (status.ok()) {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) status = Status::error("network_init_failed", "curl initialization failed");
        else {
            status = query_image(config, crop, result, [&] {
                return cancel_after >= 0 && std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count() >= cancel_after;
            });
            curl_global_cleanup();
        }
    }
    Json out{{"ok", status.ok()}, {"code", status.code}, {"message", status.message},
             {"crop_width", crop.width}, {"crop_height", crop.height}};
    if (status.ok()) { out["source_text"] = result.source_text; out["translation"] = result.translation; }
    std::cout << out.dump() << '\n';
    return status.ok() ? 0 : 1;
}
