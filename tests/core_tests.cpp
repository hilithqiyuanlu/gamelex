#include "gamelex/core.hpp"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <limits>
#include <unistd.h>
#include <jpeglib.h>

using namespace gamelex;
static int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { std::cerr << "FAIL line " << __LINE__ << ": " #condition "\n"; std::exit(1); } } while (0)

static std::vector<unsigned char> fixture(bool progressive = false) {
    jpeg_compress_struct c{};
    jpeg_error_mgr e{};
    c.err = jpeg_std_error(&e);
    jpeg_create_compress(&c);
    unsigned char* bytes = nullptr; unsigned long size = 0;
    jpeg_mem_dest(&c, &bytes, &size);
    c.image_width = 80; c.image_height = 60;
    c.input_components = 3; c.in_color_space = JCS_RGB;
    jpeg_set_defaults(&c); jpeg_set_quality(&c, 95, TRUE);
    if (progressive) jpeg_simple_progression(&c);
    jpeg_start_compress(&c, TRUE);
    std::vector<unsigned char> row(80 * 3);
    for (int x = 0; x < 80; ++x) { row[x * 3] = x < 40 ? 255 : 0; row[x * 3 + 1] = 0; row[x * 3 + 2] = x < 40 ? 0 : 255; }
    while (c.next_scanline < c.image_height) { JSAMPROW p = row.data(); jpeg_write_scanlines(&c, &p, 1); }
    jpeg_finish_compress(&c);
    std::vector<unsigned char> result(bytes, bytes + size);
    std::free(bytes); jpeg_destroy_compress(&c);
    return result;
}
static void verify_blue(const Image& image) {
    jpeg_decompress_struct d{}; jpeg_error_mgr e{}; d.err = jpeg_std_error(&e);
    jpeg_create_decompress(&d); jpeg_mem_src(&d, image.jpeg.data(), image.jpeg.size());
    jpeg_read_header(&d, TRUE); d.out_color_space = JCS_RGB; jpeg_start_decompress(&d);
    CHECK(d.output_width == 20 && d.output_height == 30);
    std::vector<unsigned char> row(d.output_width * 3);
    while (d.output_scanline < d.output_height) {
        JSAMPROW p = row.data(); jpeg_read_scanlines(&d, &p, 1);
        CHECK(row[0] < 20 && row[2] > 230);
    }
    jpeg_finish_decompress(&d); jpeg_destroy_decompress(&d);
}
static Json config_json() {
    return {{"schema_version", 1}, {"endpoint", "https://localhost:9999/v1/chat/completions"},
            {"api_key", "test-secret"}, {"model", "test-vision"}, {"ca_bundle", ""}};
}
static std::string response(const Json& content) {
    return Json{{"choices", Json::array({{{"message", {{"content", content.dump()}}}}})}}.dump();
}
int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--fixture") {
        auto bytes = fixture(); std::ofstream f(argv[2], std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()); return f ? 0 : 1;
    }
    Json j;
    CHECK(parse_json("{}", j).ok());
    CHECK(!parse_json("{", j).ok());
    CHECK(!parse_json(std::string(MaxJsonBytes + 1, ' '), j).ok());
    Config c;
    auto config = config_json();
    CHECK(parse_config(config, c).ok()); CHECK(c.timeout_seconds == 20);
    CHECK(!c.save_last_capture);
    auto diagnostics=config; diagnostics["save_last_capture"]=true;
    CHECK(parse_config(diagnostics,c).ok() && c.save_last_capture);
    diagnostics["save_last_capture"]="true"; CHECK(!parse_config(diagnostics,c).ok());
    for (const auto& wrong : {Json(nullptr), Json::array(), Json("wrong"), Json(1)}) CHECK(!parse_config(wrong, c).ok());
    for (const auto* field : {"endpoint", "api_key", "model", "ca_bundle"}) {
        auto bad = config; bad[field] = 5; CHECK(!parse_config(bad, c).ok());
    }
    for (const auto* url : {"http://localhost/test", "https://user:secret@localhost/test", "https://localhost/test?key=secret", "https://localhost/test\n"}) {
        auto bad = config; bad["endpoint"] = url; CHECK(!parse_config(bad, c).ok());
    }
    for (auto timeout : {Json(0), Json(61), Json("20"), Json(2.5), Json(18446744073709551615ull)}) {
        auto bad = config; bad["timeout_seconds"] = timeout; CHECK(!parse_config(bad, c).ok());
    }
    auto bad = config; bad["api_key"] = "secret\r\nX-Other: injected"; CHECK(!parse_config(bad, c).ok());
    bad = config; bad["capture_combo"] = {"L", "L"}; CHECK(!parse_config(bad, c).ok());
    bad = config; bad["capture_combo"] = {"L", "UP", "DOWN"}; CHECK(!parse_config(bad, c).ok());
    bad = config; bad["input_combo"] = {"L", "DOWN", "LSTICK"}; CHECK(!parse_config(bad, c).ok());
    bad = config; bad["input_combo"] = {"L", "DOWN"}; CHECK(!parse_config(bad, c).ok());
    bad = config; bad["model"] = ""; CHECK(parse_config(bad, c).code == "service_not_configured");
    CHECK(parse_config(bad, c, false).ok());
    CHECK(!validate_crop({0, 0, 0, 1}).ok()); CHECK(!validate_crop({0, 0, 2, 1}).ok());
    CHECK(!validate_crop({std::numeric_limits<double>::quiet_NaN(), 0, 1, 1}).ok());
    PixelRect p{};
    CHECK(pixel_crop({.5, .5, .5, .5}, 1280, 720, p).ok());
    CHECK(p.x == 640 && p.y == 360 && p.width == 640 && p.height == 360);
    CHECK(!pixel_crop({}, 0, 720, p).ok()); CHECK(!pixel_crop({}, 9000, 9000, p).ok());
    CHECK(base64({}).empty()); CHECK(base64({'f'}) == "Zg=="); CHECK(base64({'f', 'o'}) == "Zm8="); CHECK(base64({'f', 'o', 'o'}) == "Zm9v");
    Translation out{"keep", "保留"};
    CHECK(parse_translation(response({{"source_text", "Don't give up.\nGo on!"}, {"translation", "别放弃。\n继续！"}}), out).ok());
    CHECK(out.source_text == "Don't give up.\nGo on!");
    CHECK(parse_translation(response({{"source_text", nullptr}, {"translation", nullptr}}), out).code == "no_text");
    const auto previous = out.source_text;
    for (const auto& body : {std::string("not json"), std::string("{\"choices\":null}"), response({{"source_text", "invented"}}), response({{"source_text", 4}, {"translation", "译文"}})}) CHECK(!parse_translation(body, out).ok());
    CHECK(out.source_text == previous);
    CHECK(parse_translation(response({{"source_text", "Hello"}, {"translation", " \n"}}), out).code == "invalid_response");
    Image crop;
    CHECK(crop_jpeg(fixture(), {.625, .25, .25, .5}, crop).ok());
    CHECK(crop.width == 20 && crop.height == 30); verify_blue(crop);
    CHECK(!crop_jpeg(fixture(true), {}, crop).ok());
    CHECK(!crop_jpeg({0, 1, 2}, {}, crop).ok());
    const auto original_width = crop.width;
    CHECK(!crop_jpeg({}, {}, crop).ok()); CHECK(crop.width == original_width);
    auto jpeg=fixture(); const auto exact=jpeg; const auto* allocation=jpeg.data();
    Image unchanged;
    CHECK(adopt_jpeg(std::move(jpeg),unchanged).ok());
    CHECK(unchanged.width==80 && unchanged.height==60 && unchanged.jpeg==exact);
    CHECK(unchanged.jpeg.data()==allocation); // No re-encode or second image copy.
    CHECK(!adopt_jpeg({0xff,0xd8,0xff,0xd9},unchanged).ok());
    auto incomplete=fixture(); incomplete.pop_back();
    CHECK(!adopt_jpeg(std::move(incomplete),unchanged).ok());
    CHECK(!adopt_jpeg(fixture(true),unchanged).ok());
    CHECK(unchanged.jpeg==exact); // A rejected header must not replace a result.
    const auto request = image_request(c, crop);
    CHECK(request["stream"] == false); CHECK(request["messages"][0]["content"][1]["image_url"]["url"].get<std::string>().rfind("data:image/jpeg;base64,", 0) == 0);
    CHECK(request.dump().find("test-secret") == std::string::npos);
    CHECK(valid_id(unique_id())); CHECK(unique_id() != unique_id()); CHECK(!valid_id("../escape"));
    CHECK(validate_request({{"schema_version", 1}, {"id", "abc-1"}, {"command", "capture"}}).ok());
    CHECK(!validate_request({{"schema_version", 1}, {"id", "abc-1"}, {"command", "delete-anything"}}).ok());
    const auto path = std::string("/tmp/gamelex-") + unique_id() + ".json";
    CHECK(atomic_json(path, {{"id", "one"}, {"text", "中文\n\"quotes\""}}).ok());
    CHECK(read_json(path, j).ok()); CHECK(j["text"] == "中文\n\"quotes\"");
    CHECK(atomic_json(path, {{"id", "two"}}).ok()); CHECK(read_json(path, j).ok()); CHECK(j["id"] == "two");
    CHECK(!atomic_json("/nonexistent/gamelex.json", j).ok());
    // Horizon-style no-replace semantics: at least one valid record survives
    // a torn inactive slot, and a restart continues the publication sequence.
    CHECK(atomic_json(path, {{"id", "three"}}).ok());
    CHECK(read_json(path, j).ok()); CHECK(j["id"] == "three");
    const auto a_path = path + ".a", b_path = path + ".b";
    Json a, b;
    auto a_good = read_json(a_path, a).ok();
    auto b_good = read_json(b_path, b).ok();
    CHECK(a_good && b_good);
    const auto newest = a["sequence"].get<std::uint64_t>() > b["sequence"].get<std::uint64_t>() ? a_path : b_path;
    { std::ofstream broken(newest); broken << "{torn"; }
    CHECK(read_json(path, j).ok()); CHECK(j["id"] == "two");
    CHECK(atomic_json(path, {{"id", "recovered"}}).ok());
    CHECK(read_json(path, j).ok()); CHECK(j["id"] == "recovered");
    CHECK(remove_json(path).ok()); CHECK(!read_json(path, j).ok());
    std::cout << "PASS " << checks << " checks\n";
}
