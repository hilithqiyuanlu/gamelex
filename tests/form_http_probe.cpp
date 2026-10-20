#include "gamelex/form_http.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>

using namespace gamelex;
static int released=0;
static void sample(form::Stage stage) { if (stage==form::Stage::Released) ++released; }
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL form HTTPS line %d\n",__LINE__); std::exit(1); } } while(0)
int main(int argc,char** argv) {
    if (argc!=3) return 2;
    std::vector<unsigned char> image(512*1024);
    for (std::size_t i=0;i<image.size();++i) image[i]=static_cast<unsigned char>(i*73+255);
    const std::string query="PRIVATE-TEST-KEY 中文\nA+B & C=/~", base=argv[1];
    auto run=[&](const char* path, bool trusted=true, long timeout=5, int cancel_ms=-1) {
        const auto started=std::chrono::steady_clock::now();
        Cancelled cancel=[&] { return cancel_ms>=0 && std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count()>=cancel_ms; };
        const std::string url=base+path;
        form::Stream stream{form::Field::jpeg("image",image.data(),image.size()),{"q",query},{"language_type","ENG"}};
        form::Options options{url.c_str(),trusted?argv[2]:"",timeout,&cancel,sample};
        auto result=form::post(options,stream);
        if (result.code==CURLE_OK) CHECK(result.uploaded_bytes==stream.length());
        return result;
    };
    auto ok=[&] { auto result=run("/ok"); CHECK(result.code==CURLE_OK && result.http==200 && result.body=="OK"); };
    for(int i=0;i<50;++i) ok();
    auto r=run("/unauthorized"); CHECK(r.code==CURLE_OK && r.http==401); ok();
    r=run("/server-error"); CHECK(r.code==CURLE_OK && r.http==500); ok();
    r=run("/redirect"); CHECK(r.code==CURLE_OK && r.http==307); ok();
    r=run("/oversized"); CHECK(r.too_large && r.code!=CURLE_OK); ok();
    r=run("/slow",true,1); CHECK(r.code==CURLE_OPERATION_TIMEDOUT); ok();
    r=run("/slow",true,5,100); CHECK(r.cancelled && r.code!=CURLE_OK); ok();
    r=run("/ok",false); CHECK(r.code==CURLE_PEER_FAILED_VERIFICATION || r.code==CURLE_SSL_CACERT_BADFILE); ok();
    r=run("/ok",true,5,0); CHECK(r.cancelled && r.uploaded_bytes==0); ok();
    CHECK(released==65); // 50 + 7 dispatched failures + 8 recovery requests.
    std::puts("PASS 50 uploads and 8 failure/recovery cases in one process");
}
