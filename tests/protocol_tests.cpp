#include "gamelex/protocol.hpp"
#include <cstdio>
#include <cstdlib>
using namespace gamelex;
static int checks=0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL protocol line %d\n",__LINE__);std::exit(1); } } while(0)
int main() {
    Json ready={{"version",ReleaseVersion},{"protocol_version",ProtocolVersion},{"epoch","boot-a"},{"tick_ms",1000ULL}};
    CHECK(backend_ready(ready,1000).ok());
    CHECK(backend_ready(ready,31000).ok());
    CHECK(backend_ready(ready,31001).code=="backend_unavailable");
    CHECK(backend_ready(ready,999).code=="backend_unavailable");
    for(const auto& bad:{Json(nullptr),Json::array(),Json("wrong"),Json::object()}) CHECK(backend_ready(bad,1000).code=="backend_unavailable");
    for(const char* key:{"epoch","tick_ms"}) { auto bad=ready;bad.erase(key);CHECK(backend_ready(bad,1000).code=="backend_unavailable"); }
    for(const auto& bad_value:{Json(-1),Json("1000"),Json(1000.0),Json(true)}) {
        auto bad=ready;bad["tick_ms"]=bad_value;CHECK(backend_ready(bad,1000).code=="backend_unavailable");
    }
    for(const char* key:{"version","protocol_version"}) {
        auto bad=ready;bad.erase(key);CHECK(backend_ready(bad,1000).code=="version_mismatch");
        bad=ready;bad[key]="unknown";CHECK(backend_ready(bad,1000).code=="version_mismatch");
    }
    auto old=ready;old["version"]="0.2.6";CHECK(backend_ready(old,1000).code=="version_mismatch");
    old["tick_ms"]=1ULL;CHECK(backend_ready(old,40000).code=="backend_unavailable");
    Json request={{"client_version",ReleaseVersion},{"protocol_version",ProtocolVersion}};
    CHECK(matching_protocol(request));
    CHECK(!matching_protocol(Json::object()));
    request["client_version"]="0.2.6";CHECK(!matching_protocol(request));
    request["client_version"]=ReleaseVersion;request["protocol_version"]=ProtocolVersion+1;CHECK(!matching_protocol(request));
    request["protocol_version"]=true;CHECK(!matching_protocol(request));
    std::printf("PASS %d protocol freshness and mixed-version checks\n",checks);
}
