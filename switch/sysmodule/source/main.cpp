#include "gamelex/core.hpp"
#include "probe_common.hpp"
#include "baidu.hpp"
#include <switch.h>
#include <curl/curl.h>
#include <malloc.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <new>

using namespace gamelex;
using namespace gamelex::probe;
// Deliberately C stdio: this must work before any C++ object exists.
static void probe_log(const char* stage) {
    FILE* file = std::fopen("sdmc:/gamelex-boot.log", "a");
    if (!file) return;
    std::fprintf(file, "%s\n", stage);
    std::fflush(file);
    std::fclose(file);
    // The console can be power-cycled at the fatal screen; make the log durable.
    fsdevCommitDevice("sdmc");
}
extern "C" {
void __libnx_init_time(void);
u32 __nx_applet_type = AppletType_None;
extern char* fake_heap_start;
extern char* fake_heap_end;
alignas(0x1000) static char probe_heap[4 * 1024 * 1024];
// libstdc++/nlohmann static constructors allocate (locale tables, JSON state).
// On this target they can run before the libnx heap hook; install our heap as
// the earliest init-array entry so those allocations have somewhere to go.
__attribute__((constructor(101))) static void early_heap_init() {
    fake_heap_start = probe_heap;
    fake_heap_end = probe_heap + sizeof(probe_heap);
}
void __libnx_initheap() {
    if (fake_heap_start == probe_heap) return;
    fake_heap_start = probe_heap;
    fake_heap_end = probe_heap + sizeof(probe_heap);
}
void __appInit() {
    // SD is the only service required before main. Network/service failures
    // are recorded as diagnostics instead of taking the console down.
    if (R_FAILED(smInitialize()) || R_FAILED(fsInitialize())) svcExitProcess();
    // boot2 can launch us before the SD mount is ready. Do not silently exit
    // after one transient failure; an on-demand launch is also supported.
    Result mounted=0;
    unsigned attempts=0;
    do {
        mounted=fsdevMountSdmc();
        ++attempts;
        if (R_SUCCEEDED(mounted)) break;
        svcSleepThread(250000000);
    } while (attempts<20);
    if (R_FAILED(mounted)) svcExitProcess();
    mkdir("sdmc:/config", 0777);
    mkdir("sdmc:/config/gamelex", 0777);
    char startup[96];
    std::snprintf(startup,sizeof(startup),"appInit version=%s sd_attempts=%u",ReleaseVersion,attempts);
    probe_log(startup);
}
void __appExit() { fsdevUnmountAll(); fsExit(); smExit(); }
}
namespace {
// No dynamic initialization here: this module crashed before main() when these
// globals allocated during C++ static construction. They are filled in main().
std::string epoch;
Image frame;
std::string frame_id, frame_game;
u64 frame_tick = 0;
Json telemetry;
bool sockets_ready = false;
bool network_services_ready = false;
bool visible = true;
u64 visible_checked = 0;
const char* clients[] = {"overlay", "app"};
std::string handled[2];
Config config;
bool config_ready = false;

u64 ms() { return armTicksToNs(svcGetSystemTick()) / 1000000; }
void sample(const char* stage) {
    u64 used = 0, total = 0;
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    auto heap = mallinfo();
    const u64 previous_peak = telemetry.contains("sampled_heap_peak_bytes") && telemetry["sampled_heap_peak_bytes"].is_number_unsigned() ? telemetry["sampled_heap_peak_bytes"].get<u64>() : 0;
    const auto& transfer=baidu_transfer_stats();
    telemetry["sampled_heap_peak_bytes"] = std::max({previous_peak, static_cast<u64>(heap.uordblks), static_cast<u64>(transfer.peak_heap)});
    telemetry["last_transfer"]={{"request",transfer.request},{"form_bytes",transfer.form_bytes},
        {"uploaded_bytes",transfer.uploaded_bytes},{"before_heap",transfer.before_heap},
        {"prepared_heap",transfer.prepared_heap},{"performed_heap",transfer.performed_heap},
        {"released_heap",transfer.released_heap},{"sampled_peak_heap",transfer.peak_heap},
        {"curl_code",transfer.curl_code},{"http_code",transfer.http_code}};
    telemetry[stage] = {{"process_used_bytes", used}, {"process_total_bytes", total},
                        {"heap_used_bytes", heap.uordblks}, {"heap_budget_bytes", sizeof(probe_heap)}};
}
std::string game_id() {
    u64 pid = 0, program = 0;
    auto rc = pmdmntGetApplicationProcessId(&pid);
    telemetry["application_pid_result"] = rc;
    if (R_FAILED(rc)) return "unknown";
    rc = pminfoGetProgramId(&program, pid);
    telemetry["application_program_result"] = rc;
    if (R_FAILED(rc)) return "unknown";
    char text[17]; std::snprintf(text, sizeof(text), "%016llX", static_cast<unsigned long long>(program));
    return text;
}
void clear_frame() { frame = {}; frame_id.clear(); frame_game.clear(); frame_tick = 0; }
bool overlay_visible() {
    // Cache to limit SD polling during curl progress callbacks.
    if (ms() - visible_checked < 100) return visible;
    visible_checked = ms();
    Json state;
    visible = true; // Fail closed: a missing/crashed UI must not cause a screenshot upload.
    if (read_json(runtime_dir() + "/overlay.json", state).ok() && state.is_object() &&
        state.contains("visible") && state["visible"].is_boolean()) visible = state["visible"].get<bool>();
    return visible;
}
void respond(const std::string& client, const std::string& id, const Status& status, const Json& data = Json::object()) {
    atomic_json(response_path(client), {{"schema_version", 1}, {"epoch", epoch}, {"id", id},
        {"ok", status.ok()}, {"code", status.code}, {"message", status.message}, {"data", data}});
}
Status load_config(bool service = false) {
    Json j;
    auto status = read_json(root_dir() + "/config.json", j);
    if (status.ok()) status = parse_config(j, config, service);
    config_ready = status.ok();
    return status;
}
bool current(const std::string& client, const std::string& id) {
    Json request;
    return read_json(request_path(client), request).ok() && field(request, "id") == id && field(request, "epoch") == epoch;
}
Status capture(const Cancelled& cancelled) {
    clear_frame();
    auto status = load_config();
    if (!status.ok()) return status;
    // The UI hides before submitting a capture. Leave time for its framebuffer
    // to clear, then verify that it stayed hidden throughout the capture.
    for (int i = 0; i < 15; ++i) {
        if (cancelled()) return Status::error("cancelled", "Capture cancelled");
        svcSleepThread(50000000);
    }
    if (overlay_visible()) return Status::error("overlay_visible", "Close the GameLex overlay before capture");
    const auto before_game = game_id();
    if (before_game == "unknown") return Status::error("game_unavailable", "Open a game before capturing");
    auto rc = capsscInitialize();
    if (R_FAILED(rc)) return Status::error("capture_init_failed", "caps:sc initialization failed: " + to_text(rc));
    std::vector<unsigned char> jpeg(CAPSSC_JPEG_BUFFER_SIZE);
    u64 length = 0;
    sample("before_capture");
    const auto started = ms();
    rc = capsscCaptureJpegScreenShot(&length, jpeg.data(), jpeg.size(), ViLayerStack_Screenshot, 1000000000LL);
    capsscExit();
    if (R_FAILED(rc) || length == 0 || length > jpeg.size())
        return Status::error("capture_failed", "Game screenshot unavailable: " + to_text(rc));
    if (cancelled() || overlay_visible()) return Status::error("cancelled", "Capture discarded because focus changed");
    jpeg.resize(length);
    sample("jpeg_captured");
    status = crop_jpeg(jpeg, config.probe_crop, frame, [] { sample("crop_sample"); });
    sample("jpeg_cropped");
    telemetry["capture_ms"] = ms() - started;
    telemetry["input_jpeg_bytes"] = length;
    telemetry["cropped_jpeg_bytes"] = frame.jpeg.size();
    if (!status.ok()) return status;
    if (game_id() != before_game) { clear_frame(); return Status::error("game_changed", "Game changed during capture"); }
    frame_id = unique_id(); frame_game = before_game; frame_tick = ms();
    return {};
}
Status ensure_network() {
    if (!sockets_ready) {
        const auto rc = socketInitializeDefault();
        if (R_FAILED(rc)) return Status::error("network_init_failed", "Socket initialization failed");
        sockets_ready = true;
    }
    if (!network_services_ready) {
        const auto clock_result=timeInitialize();
        telemetry["clock_init_result"]=clock_result;
        if (R_SUCCEEDED(clock_result)) {
            u64 stamp=0;
            const auto rc=timeGetCurrentTime(TimeType_UserSystemClock,&stamp);
            telemetry["user_clock_result"]=rc;
            if (R_SUCCEEDED(rc)) telemetry["user_clock_utc"]=stamp;
            const auto network_clock=timeGetCurrentTime(TimeType_NetworkSystemClock,&stamp);
            telemetry["network_clock_result"]=network_clock;
            if (R_SUCCEEDED(network_clock)) telemetry["network_clock_utc"]=stamp;
            __libnx_init_time(); timeExit();
        }
        auto rc=sslInitialize(0x3);
        telemetry["ssl_init_result"]=rc;
        if (R_FAILED(rc)) return Status::error("network_init_failed","系统 TLS 初始化失败，代码 "+to_text(rc));
        rc=csrngInitialize();
        telemetry["rng_init_result"]=rc;
        if (R_FAILED(rc)) {
            sslExit();
            return Status::error("network_init_failed","系统随机数服务初始化失败，代码 "+to_text(rc));
        }
        network_services_ready=true;
    }
    return {};
}
bool clean_for(const std::string& nonce) {
    Json state;
    if (!read_json(runtime_dir()+"/overlay.json",state).ok() || !state.is_object() ||
        !state.contains("visible") || !state["visible"].is_boolean() ||
        !state.contains("tick_ms") || !state["tick_ms"].is_number_unsigned()) return false;
    return reading::clean_capture(state["visible"].get<bool>(),field(state,"capture_nonce"),nonce,
        state["tick_ms"].get<u64>(),ms());
}
Status scan_capture(const std::string& nonce,const Cancelled& cancelled) {
    clear_frame();
    telemetry["capture_blank_confirmed"]=false;
    if (!valid_id(nonce)) return Status::error("capture_not_ready","截图请求无效，请重试。");
    auto status = load_config();
    if (!status.ok()) return status;
    if (!clean_for(nonce)) return Status::error("capture_not_ready","工具画面尚未清空，已停止截图，请重试。");
    // The UI has submitted three transparent frames. Leave the compositor a
    // further 150 ms, then validate the matching per-request signal again.
    for (int i=0;i<3;++i) {
        svcSleepThread(50000000);
        if (cancelled() || !clean_for(nonce)) return Status::error("cancelled","截图已取消。");
    }
    const auto before_game = game_id();
    if (before_game == "unknown") return Status::error("game_unavailable", "Open a game before scanning");
    auto rc = capsscInitialize();
    if (R_FAILED(rc)) return Status::error("capture_init_failed", "caps:sc initialization failed");
    std::vector<unsigned char> jpeg(CAPSSC_JPEG_BUFFER_SIZE);
    u64 length = 0;
    sample("before_scan");
    const auto started = ms();
    // GameLex explicitly removes its VI layer from screenshot/recording
    // stacks. Do not fall back to a debug stack that can include tool UI.
    rc = capsscCaptureJpegScreenShot(&length, jpeg.data(), jpeg.size(), ViLayerStack_Screenshot, 1000000000LL);
    capsscExit();
    if (cancelled() || !clean_for(nonce)) return Status::error("cancelled","截图时工具画面发生变化，已丢弃截图。");
    if (R_FAILED(rc) || length == 0 || length > jpeg.size()) {
        probe_log((std::string("scan_capture_failed rc=") + to_text(rc) + " bytes=" + to_text(length)).c_str());
        return Status::error("capture_failed", "Game screenshot unavailable");
    }
    jpeg.resize(length);
    sample("scan_captured");
    // capssc reserves 512 KiB. Release its unused tail before keeping the image
    // across HTTPS requests; this is a small byte copy, not JPEG re-encoding.
    std::vector<unsigned char>(jpeg.begin(), jpeg.end()).swap(jpeg);
    status = adopt_jpeg(std::move(jpeg),frame);
    sample("scan_prepared");
    telemetry["scan_capture_ms"] = ms() - started;
    telemetry["scan_jpeg_bytes"] = length;
    telemetry["scan_jpeg_capacity"] = frame.jpeg.capacity();
    if (!status.ok()) return status;
    if (game_id() != before_game) { clear_frame(); return Status::error("game_changed", "Game changed during scan"); }
    telemetry["capture_blank_confirmed"]=true;
    telemetry["scan_width"]=frame.width; telemetry["scan_height"]=frame.height;
    frame_id = unique_id(); frame_game = before_game; frame_tick = ms();
    if (config.save_last_capture) {
        // Explicit diagnostic setting: overwrite only this one image. Never
        // accumulate screenshots or include them in installation/source zips.
        FILE* file=std::fopen((runtime_dir()+"/last-capture.jpg").c_str(),"wb");
        bool saved=false;
        if (file) { saved=std::fwrite(frame.jpeg.data(),1,frame.jpeg.size(),file)==frame.jpeg.size();
                    saved=std::fclose(file)==0 && saved; }
        telemetry["diagnostic_image_saved"]=saved;
    }
    return {};
}
void handle(int client_index, const Json& request) {
    const std::string client = clients[client_index];
    const auto id = field(request, "id");
    const auto command = field(request, "command");
    handled[client_index] = id;
    if (field(request, "epoch") != epoch) return; // Never replay requests across reboots.
    auto status = validate_request(request);
    if (!status.ok()) { respond(client, id, status); return; }
    if (!matching_protocol(request)) {
        respond(client,id,Status::error("version_mismatch","浮层与后台版本不一致，请完整更新安装包并重启。"));
        return;
    }
    // libcurl invokes progress/read callbacks often. Poll the SD mailbox at
    // most every 100 ms rather than parsing two files for every upload chunk.
    u64 last_cancel_check=0;
    u64 busy_heartbeat=ms();
    bool request_cancelled=false;
    const Cancelled interrupted=[&] {
        const auto now=ms();
        if (now-busy_heartbeat>=2000) {
            busy_heartbeat=now;
            // Long OCR/auth waits still count as a live backend. Publish a
            // small heartbeat, rather than bypassing stale-state validation
            // or allocating the full diagnostic tree inside the transfer.
            atomic_json(runtime_dir()+"/backend.json",{{"version",ReleaseVersion},{"protocol_version",ProtocolVersion},{"epoch",epoch},
                {"tick_ms",now},{"state","request"}});
        }
        if (!last_cancel_check || now-last_cancel_check>=100) {
            last_cancel_check=now;
            request_cancelled=!current(client,id);
        }
        return request_cancelled;
    };
    if (command == "clear" || command == "cancel") { clear_frame(); respond(client, id, {}); return; }
    if (command == "status") {
        sample("idle");
        respond(client, id, {}, {{"metrics", telemetry}, {"capture_id", frame_id}}); return;
    }
    if (command == "capture") {
        respond(client, id, {}, {{"state", "capturing"}});
        status = capture([&] { return !current(client, id); });
        if (current(client, id)) respond(client, id, status, {{"state", status.ok() ? "captured" : "error"},
            {"capture_id", frame_id}, {"width", frame.width}, {"height", frame.height},
            {"game_id", frame_game}, {"metrics", telemetry}});
        return;
    }
    if (command == "scan") {
        const auto started=ms();
        status = scan_capture(field(request,"capture_nonce"),[&] { return !current(client,id); });
        if (!status.ok()) probe_log((std::string("scan_capture_error ") + status.code + " " + status.message).c_str());
        if (status.ok() && current(client,id))
            respond(client,id,{},{{"state","captured"},{"width",frame.width},{"height",frame.height}});
        if (!current(client,id)) { clear_frame(); return; }
        if (status.ok()) status = ensure_network();
        if (status.ok() && (config.ocr_api_key.empty() || config.ocr_secret.empty()))
            status = Status::error("ocr_not_configured", "尚未配置文字识别服务。请在 config/gamelex/config.json 填写 OCR 的两项密钥，按随包说明设置。");
        BaiduScan scan;
        if (status.ok()) status = baidu_scan(config, frame, scan, interrupted);
        telemetry["scan_group_count"]=scan.groups.size();
        telemetry["scan_line_count"]=scan.lines.size();
        telemetry["scan_auth_ms"]=scan.auth_ms; telemetry["scan_ocr_ms"]=scan.ocr_ms;
        telemetry["scan_total_ms"]=ms()-started;
        clear_frame();
        sample("scan_finished");
        atomic_json(runtime_dir()+"/scan-metrics.json",telemetry);
        if (status.ok()) probe_log((std::string("scan_ok tokens=") + to_text(static_cast<unsigned long long>(scan.tokens.size()))).c_str());
        else probe_log((std::string("scan_error ") + status.code + " " + status.message).c_str());
        if (status.ok()) {
            Json tokens = Json::array();
            for (const auto& token : scan.tokens) {
                tokens.push_back({{"text", token.text}, {"x", token.x}, {"y", token.y},
                                  {"w", token.w}, {"h", token.h}, {"line", token.line}});
            }
            Json groups=Json::array();
            for (const auto& group:scan.groups) groups.push_back({{"text",group.text},{"x",group.x},{"y",group.y},
                {"w",group.w},{"h",group.h},{"lines",group.lines}});
            Json lines = Json::array();
            for (const auto& line : scan.lines) lines.push_back(line);
            if (current(client, id))
                respond(client, id, {}, {{"state","complete"},{"tokens", tokens}, {"lines", lines},{"paragraphs",scan.paragraphs},{"groups",groups},
                                         {"width", scan.width}, {"height", scan.height}});
        } else if (current(client, id)) {
            respond(client, id, status);
        }
        clear_frame();
        return;
    }
    if (command == "translate") {
        status = ensure_network();
        if (status.ok()) status = load_config();
        std::string translation;
        const auto text = field(request, "text");
        if (status.ok()) status = baidu_translate(config, text, translation, interrupted);
        if (!status.ok()) probe_log((std::string("translate_error ") + status.code + " " + status.message).c_str());
        if (current(client, id)) {
            if (status.ok()) respond(client, id, {}, {{"translation", translation}});
            else respond(client, id, status);
        }
        return;
    }
    if (command == "translate_all") {
        const auto started=ms();
        status = ensure_network();
        if (status.ok()) status = load_config();
        std::vector<std::string> lines;
        if (status.ok() && request.contains("lines") && request["lines"].is_array()) {
            for (const auto& line : request["lines"]) if (line.is_string()) lines.push_back(line.get<std::string>());
        }
        if (status.ok() && (lines.empty() || lines.size() > 80))
            status = Status::error("translate_invalid_text", "Line list is empty or too large");
        std::vector<std::string> translations;
        if (status.ok()) status = baidu_translate_lines(config, lines, translations, interrupted);
        telemetry["translate_ms"]=ms()-started;
        sample("translation_finished");
        atomic_json(runtime_dir()+"/scan-metrics.json",telemetry);
        if (!status.ok()) probe_log((std::string("translate_all_error ") + status.code + " " + status.message).c_str());
        if (!current(client, id)) return;
        if (status.ok()) {
            Json result = Json::array();
            for (const auto& line : translations) result.push_back(line);
            respond(client, id, {}, {{"translations", result}});
        } else {
            respond(client, id, status);
        }
        return;
    }
    if (command == "recognize") {
        if (frame_id.empty() || field(request, "capture_id") != frame_id || ms() - frame_tick > 60000) {
            clear_frame(); respond(client, id, Status::error("capture_expired", "Capture again before querying")); return;
        }
        const auto current_game = game_id();
        if (frame_game == "unknown" || current_game != frame_game) {
            clear_frame(); respond(client, id, Status::error("game_changed", "Game identity unavailable or changed; capture again")); return;
        }
        status = load_config(true);
        if (status.ok() && !sockets_ready) {
            auto rc = socketInitializeDefault();
            if (R_FAILED(rc)) status = Status::error("network_init_failed", "Socket initialization failed: " + to_text(rc));
            else sockets_ready = true;
        }
        if (status.ok() && curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
            status = Status::error("network_init_failed", "curl initialization failed");
        Translation result;
        const auto started = ms();
        const auto captured_game = frame_game;
        if (status.ok()) {
            respond(client, id, {}, {{"state", "querying"}});
            u64 checked = 0;
            bool request_cancelled = false;
            status = query_image(config, frame, result, [&] {
                if (!checked || ms() - checked >= 100) {
                    checked = ms();
                    sample("network_sample");
                    request_cancelled = !current(client, id);
                }
                return request_cancelled;
            });
            curl_global_cleanup();
        }
        telemetry["network_ms"] = ms() - started;
        sample("after_network");
        if (status.ok() && game_id() != captured_game)
            status = Status::error("game_changed", "Game changed; query result discarded");
        clear_frame(); // No automatic retry and no screenshot persisted on the SD card.
        Json data{{"state", status.ok() ? "complete" : "error"}, {"metrics", telemetry}};
        if (status.ok()) { data["source_text"] = result.source_text; data["translation"] = result.translation; }
        if (current(client, id)) respond(client, id, status, data);
    }
}
u64 binding(const std::vector<std::string>& keys) {
    u64 mask = 0;
    for (const auto& key : keys) {
        if (key == "L") mask |= HidNpadButton_L;
        else if (key == "R") mask |= HidNpadButton_R;
        else if (key == "ZL") mask |= HidNpadButton_ZL;
        else if (key == "ZR") mask |= HidNpadButton_ZR;
        else if (key == "UP") mask |= HidNpadButton_Up;
        else if (key == "DOWN") mask |= HidNpadButton_Down;
        else if (key == "LEFT") mask |= HidNpadButton_Left;
        else if (key == "RIGHT") mask |= HidNpadButton_Right;
        else if (key == "LSTICK") mask |= HidNpadButton_StickL;
        else if (key == "RSTICK") mask |= HidNpadButton_StickR;
    }
    return mask;
}
}
int main() {
    probe_log("main");
        directories();
        epoch = unique_id();
        telemetry = Json::object();
        probe_log("globals");
        (void)pmdmntInitialize();
        (void)pminfoInitialize();
        probe_log("before-device");
        {
            // No JSON in the startup path: device_info() aborted here on
            // hardware. The diagnostics app still reports the full versions.
            SetSysFirmwareVersion firmware{};
            if (R_SUCCEEDED(setsysInitialize())) {
                if (R_SUCCEEDED(setsysGetFirmwareVersion(&firmware)))
                    hosversionSet(MAKEHOSVERSION(firmware.major, firmware.minor, firmware.micro));
                setsysExit();
            }
        }
        Json device = Json::object();
        probe_log("device");
    // Baseline stale files before enabling input; captured frames are never restored.
    for (int i = 0; i < 2; ++i) { Json stale; if (read_json(request_path(clients[i]), stale).ok()) handled[i] = field(stale, "id"); }
    const auto hid_result = hidInitialize();
    const bool hid_ready = R_SUCCEEDED(hid_result);
    probe_log("hid");
    device["hid_init_result"] = hid_result;
    PadState pad{};
    if (hid_ready) { padConfigureInput(8, HidNpadStyleSet_NpadStandard | HidNpadStyleTag_NpadSystemExt); padInitializeAny(&pad); }
    probe_log("before-config");
    load_config();
    probe_log("config");
    u64 previous = 0, heartbeat = 0, config_checked = 0;
    bool logged_heartbeat = false;
    probe_log("loop");
    while (true) {
            if (ms() - heartbeat >= 2000 || !heartbeat) {
                heartbeat = ms();
                sample("idle");
                atomic_json(runtime_dir() + "/backend.json", {{"version",ReleaseVersion},{"protocol_version",ProtocolVersion},{"epoch", epoch}, {"tick_ms", heartbeat}, {"device", device}, {"metrics", telemetry}, {"phase", 1}});
                if (!logged_heartbeat) { logged_heartbeat = true; probe_log("heartbeat"); }
            }
            for (int i = 0; i < 2; ++i) {
                Json request;
                if (read_json(request_path(clients[i]), request).ok() && field(request, "id") != handled[i]) handle(i, request);
            }
            if (frame_tick && ms() - frame_tick > 60000) clear_frame();
            if (ms() - config_checked > 2000) { config_checked = ms(); load_config(); }
            if (hid_ready) {
                padUpdate(&pad);
                const u64 held = padGetButtons(&pad);
                Json armed;
                const bool enabled = read_json(runtime_dir() + "/armed.json", armed).ok() && field(armed, "epoch") == epoch;
                if (enabled && config_ready && !overlay_visible()) {
                    const u64 mask = binding(config.capture_combo);
                    const u64 input = binding(config.input_combo);
                    if (mask && (held & mask) == mask && (previous & mask) != mask) {
                        std::string id;
                        if (gamelex::probe::request("overlay", "capture", id).ok()) {
                            Json request;
                            if (read_json(request_path("overlay"), request).ok()) handle(0, request);
                        }
                    } else if (input && (held & input) == input && (previous & input) != input) {
                        std::string id;
                        if (gamelex::probe::request("overlay", "status", id).ok()) {
                            handled[0] = id;
                            respond("overlay", id, Status::error("input_deferred", "System keyboard is unavailable in the standard overlay context"));
                        }
                    }
                }
                previous = held;
            }
        svcSleepThread(50000000);
    }
    return 0;
}
