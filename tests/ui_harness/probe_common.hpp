#pragma once
#include "gamelex/core.hpp"
#include "tesla.hpp"
#include <filesystem>
namespace gamelex::probe {
inline std::string folder;
inline std::string runtime_dir() { return folder; }
inline void directories() { std::filesystem::create_directories(folder); }
inline std::string response_path(const std::string& client) { return folder+"/"+client+".response.json"; }
inline std::string field(const Json& j,const char* key,const std::string& fallback="") {
 return j.is_object() && j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : fallback;
}
struct Call { std::string command,id;Json extra; };
inline std::vector<Call> calls;
inline Status request(const std::string&,const std::string& command,std::string& id,const std::string& ="",const Json& extra=Json::object()) {
 id="request-"+to_text(calls.size()+1);calls.push_back({command,id,extra});return {};
}
}
