#pragma once
#include "gamelex/core.hpp"
#include "gamelex/protocol.hpp"
#include <switch.h>
#include <cstdio>
#include <sys/stat.h>

namespace gamelex::probe {
// Functions, not global std::string objects: dynamic initialization of these
// ran before main()/__appInit and was a suspect in the sysmodule boot crashes.
inline std::string root_dir() { return "sdmc:/config/gamelex"; }
inline std::string runtime_dir() { return root_dir() + "/runtime"; }
inline std::string reports_dir() { return root_dir() + "/reports"; }
inline void directories() {
    const std::string root = root_dir();
    const std::string runtime = root + "/runtime";
    const std::string reports = root + "/reports";
    mkdir("sdmc:/config", 0777); mkdir(root.c_str(), 0777);
    mkdir(runtime.c_str(), 0777); mkdir(reports.c_str(), 0777);
}
inline std::string request_path(const std::string& client) { return runtime_dir() + "/" + client + ".request.json"; }
inline std::string response_path(const std::string& client) { return runtime_dir() + "/" + client + ".response.json"; }
inline std::string field(const Json& j, const char* key, const std::string& fallback = "") {
    return j.is_object() && j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : fallback;
}
inline Status request(const std::string& client, const std::string& command, std::string& id,
                      const std::string& capture_id = "", const Json& extra = Json::object()) {
    Json ready;
    if (!read_json(runtime_dir() + "/backend.json", ready).ok())
        return Status::error("backend_unavailable", "后台尚未启动");
    const u64 now = armTicksToNs(svcGetSystemTick()) / 1000000;
    const auto compatible=backend_ready(ready,now);
    if (!compatible.ok()) return compatible;
    id = unique_id();
    Json payload = {{"schema_version", 1}, {"id", id}, {"epoch", ready["epoch"]},
                    {"command", command}, {"capture_id", capture_id},
                    {"client_version",ReleaseVersion},{"protocol_version",ProtocolVersion}};
    if (extra.is_object()) {
        for (auto it = extra.begin(); it != extra.end(); ++it) payload[it.key()] = it.value();
    }
    return atomic_json(request_path(client), payload);
}
inline Json keyboard_probe(bool may_show) {
    Json result{{"status", "unsupported"}, {"context", may_show ? "application" : "overlay"}};
    // Tesla runs as AppletType_None. Do not impersonate a system applet or try
    // to launch a keyboard without an actual LibraryAppletCreator session.
    auto* creator = appletGetServiceSession_LibraryAppletCreator();
    if (!creator || !serviceIsActive(creator)) {
        result["reason"] = "no_library_applet_creator";
        return result;
    }
    if (!may_show) {
        result["status"] = "creator_available_not_launched";
        return result;
    }
    SwkbdConfig config{};
    Result rc = swkbdCreate(&config, 0);
    result["create_result"] = rc;
    if (R_FAILED(rc)) { result["reason"] = "create_failed"; return result; }
    swkbdConfigMakePresetDefault(&config);
    swkbdConfigSetStringLenMax(&config, 128);
    swkbdConfigSetHeaderText(&config, "GameLex 输入测试");
    char buffer[512]{};
    rc = swkbdShow(&config, buffer, sizeof(buffer));
    swkbdClose(&config);
    result["show_result"] = rc;
    result["status"] = R_SUCCEEDED(rc) ? "shown" : "cancelled_or_failed";
    // Deliberately do not persist what the tester typed.
    return result;
}
inline Json device_info() {
    Json j{{"hardware_validation", "pending"}};
    SetSysFirmwareVersion firmware{};
    auto rc = setsysInitialize();
    if (R_SUCCEEDED(rc)) {
        rc = setsysGetFirmwareVersion(&firmware);
        if (R_SUCCEEDED(rc)) {
            j["firmware"] = to_text(firmware.major) + "." + to_text(firmware.minor) + "." + to_text(firmware.micro);
            j["firmware_display"] = std::string(firmware.display_version, strnlen(firmware.display_version, sizeof(firmware.display_version)));
        }
        setsysExit();
    }
    j["firmware_result"] = rc;
    rc = splInitialize();
    if (R_SUCCEEDED(rc)) {
        u64 packed = 0;
        rc = splGetConfig(static_cast<SplConfigItem>(65000), &packed);
        if (R_SUCCEEDED(rc)) {
            j["atmosphere_packed"] = packed;
            j["atmosphere"] = to_text((packed >> 56) & 0xff) + "." +
                to_text((packed >> 48) & 0xff) + "." + to_text((packed >> 40) & 0xff);
        }
        splExit();
    }
    j["atmosphere_result"] = rc;
    return j;
}
}
