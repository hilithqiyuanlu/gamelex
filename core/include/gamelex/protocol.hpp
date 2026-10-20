#pragma once
#include "gamelex/core.hpp"
#include "gamelex/release.hpp"

namespace gamelex {
// Validate freshness before compatibility: an old SD file is not evidence of
// a live incompatible module. Unknown/mixed builds never receive scan work.
inline Status backend_ready(const Json& ready, std::uint64_t now) {
    if (!ready.is_object() || !ready.contains("epoch") || !ready["epoch"].is_string() ||
        ready["epoch"].get_ref<const std::string&>().empty() ||
        !ready.contains("tick_ms") || !ready["tick_ms"].is_number_unsigned())
        return Status::error("backend_unavailable", "后台尚未就绪，请稍候；首次安装后需完整重启。");
    const auto tick=ready["tick_ms"].get<std::uint64_t>();
    if (tick>now || now-tick>30000)
        return Status::error("backend_unavailable", "后台无响应，请完整重启；仍失败时请检查安装文件。");
    if (!ready.contains("version") || ready["version"]!=ReleaseVersion ||
        !ready.contains("protocol_version") || ready["protocol_version"]!=ProtocolVersion)
        return Status::error("version_mismatch", "浮层与后台版本不一致。请用同一安装包完整更新，并重启主机。");
    return {};
}
inline bool matching_protocol(const Json& request) {
    return request.is_object() && request.contains("client_version") && request["client_version"]==ReleaseVersion &&
        request.contains("protocol_version") && request["protocol_version"]==ProtocolVersion;
}
}
