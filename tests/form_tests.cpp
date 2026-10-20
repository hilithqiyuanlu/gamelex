#include "gamelex/core.hpp"
#include "gamelex/form.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <string>

static bool track = false;
static std::size_t allocations = 0;
void* operator new(std::size_t size) {
    if (track) ++allocations;
    if (auto* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
static int checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL form line %d\n",__LINE__); std::exit(1); } } while(0)
static std::string escape(const std::string& value) {
    std::string result;
    for (unsigned char ch : value) {
        if ((ch>='A' && ch<='Z') || (ch>='a' && ch<='z') || (ch>='0' && ch<='9') || ch=='-' || ch=='_' || ch=='.' || ch=='~') result+=ch;
        else { char hex[4]; std::snprintf(hex,sizeof(hex),"%%%02X",ch); result+=hex; }
    }
    return result;
}
static void verify(gamelex::form::Stream& stream,const std::string& expected) {
    CHECK(stream.valid()); CHECK(stream.length()==expected.size());
    for (std::size_t chunk : {1,2,3,7,4096,16384}) {
        std::string actual(expected.size()+16,'\0');
        stream.rewind();
        std::size_t size=0;
        allocations=0; track=true;
        while (auto count=stream.read(actual.data()+size,std::min(chunk,actual.size()-size))) size+=count;
        track=false;
        CHECK(allocations==0); CHECK(size==expected.size());
        actual.resize(size); CHECK(actual==expected);
        CHECK(stream.read(actual.data(),0)==0);
    }
}
int main() {
    using namespace gamelex::form;
    const std::string text="中文\nA + B &= /?~_'\"", name="q &=";
    Stream query{{name,text},{"empty",""}};
    verify(query,escape(name)+"="+escape(text)+"&empty=");
    for (std::size_t size : {0,1,2,3,4,5,255,256,257,137877,524288}) {
        std::vector<unsigned char> image(size);
        for (std::size_t i=0;i<size;++i) image[i]=static_cast<unsigned char>(i*73+255);
        allocations=0; track=true;
        Stream stream{Field::jpeg("image",image.data(),image.size()),{"language_type","ENG"}};
        track=false; CHECK(allocations==0);
        verify(stream,"image="+escape(gamelex::base64(image))+"&language_type=ENG");
    }
    Stream empty{}; verify(empty,"");
    Stream bad{Field::jpeg("image",nullptr,1)}; CHECK(!bad.valid());
    unsigned char byte=0;
    Stream overflow{Field::jpeg("image",&byte,std::numeric_limits<std::size_t>::max())}; CHECK(!overflow.valid());
    Stream too_many{{"a",""},{"b",""},{"c",""},{"d",""},{"e",""},{"f",""},{"g",""},
        {"h",""},{"i",""},{"j",""},{"k",""},{"l",""},{"m",""}};
    CHECK(!too_many.valid());
    std::printf("PASS %d streaming form checks; no encoder allocations\n",checks);
}
