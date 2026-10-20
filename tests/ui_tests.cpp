#include "gamelex/ui.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace gamelex::ui;
static int checks=0;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " #x "\n"; std::exit(1); } } while(0)
struct RecordingRenderer {
    int x=0,y=0,w=0,h=0,font=0,radius=0,clears=0;
    void clearScreen() { ++clears; }
    void drawRect(int a,int b,int c,int d,int) { x=a;y=b;w=c;h=d; }
    void drawRoundedRect(int a,int b,int c,int d,int r,int) { drawRect(a,b,c,d,0);radius=r; }
    void drawString(const std::string&,bool,int a,int b,int size,int) { x=a;y=b;font=size; }
    std::pair<int,int> getTextDimensions(const std::string&,bool,int size) { font=size; return {101,25}; }
};
int main() {
    RecordingRenderer native; RasterCanvas<RecordingRenderer> canvas(&native);
    canvas.drawRect(0,0,CanvasWidth,CanvasHeight,0);
    CHECK(native.w==1280 && native.h==720);
    canvas.drawString("中文",false,24,36,16,0);
    CHECK(native.font==32 && native.x==48 && native.y==72);
    const auto dimensions=canvas.getTextDimensions("中文",false,16);
    CHECK(native.font==32 && dimensions.first==51 && dimensions.second==13);
    canvas.drawRoundedRect(10,20,30,40,4,0);
    CHECK(native.x==20 && native.y==40 && native.w==60 && native.h==80 && native.radius==8);
    // Regression: a word on the right of the game must stay on the right,
    // and tapping the same physical position must select it (not hide a sidebar).
    std::vector<Box> boxes{{0.10,0.25,0.12,0.06},{0.80,0.75,0.10,0.05}};
    auto b=project(boxes[1],CanvasWidth,CanvasHeight);
    CHECK(b.x==512 && b.y==270 && b.w==64 && b.h==18);
    CHECK(hit_test(boxes,touch_x(1088),touch_y(558),CanvasWidth,CanvasHeight)==1);
    CHECK(hit_test(boxes,touch_x(205),touch_y(201),CanvasWidth,CanvasHeight)==0);
    CHECK(hit_test(boxes,touch_x(640),touch_y(360),CanvasWidth,CanvasHeight)==-1);
    // Header/footer corners are valid game coordinates too.
    std::vector<Box> edges{{0,0,0.02,0.02},{0.98,0.98,0.02,0.02}};
    CHECK(hit_test(edges,touch_x(4),touch_y(4),CanvasWidth,CanvasHeight)==0);
    CHECK(hit_test(edges,touch_x(1276),touch_y(716),CanvasWidth,CanvasHeight)==1);
    CHECK(project({0.99,0.99,0.2,0.2},640,360).x+project({0.99,0.99,0.2,0.2},640,360).w==640);
    CHECK(project({-0.02,0.1,0.1,0.1},640,360).x==0);
    CHECK(project({2,2,0.1,0.1},640,360).w==0);
    CHECK(project({0,0,-1,0.1},640,360).w==0);
    CHECK(project({std::numeric_limits<double>::quiet_NaN(),0,1,1},640,360).w==0);
    // Near adjacent boxes the actual containing box wins over padded neighbours.
    CHECK(hit_test({{0.1,0.1,0.1,0.1},{0.2,0.1,0.1,0.1}},129,45,640,360)==1);
    auto measure=[](const std::string& s) {
        int width=0; for (unsigned char c:s) { if (c<128) width+=6; else if ((c&0xC0)!=0x80) width+=12; } return width;
    };
    const std::string chinese="已获得新的任务。\n前往银色城堡寻找线索。";
    const auto rows=wrap(chinese,72,measure);
    std::string joined;
    for (const auto& row:rows) { CHECK(measure(row)<=72); joined+=row; }
    CHECK(joined=="已获得新的任务。前往银色城堡寻找线索。");
    const auto english=wrap("short extraordinarylongword end",60,measure);
    for (const auto& row:english) CHECK(measure(row)<=60);
    CHECK(wrap("a\n\nb",60,measure).size()==3);
    CHECK(translation_document({"获得任务","","前往城堡"})=="获得任务\n前往城堡");
    CHECK(page_count(0,11)==1); CHECK(page_count(25,11)==3);
    CHECK(clamp_page(99,25,11)==2); CHECK(clamp_page(-3,25,11)==0);
    CHECK(clamp_page(2,1,11)==0);
    RequestTracker pending;
    CHECK(!pending.accepts("")); pending.id="old"; pending.key="first";
    pending.reset(); pending.id="new"; pending.key="second";
    CHECK(!pending.accepts("old")); CHECK(pending.accepts("new"));
    pending.reset(); CHECK(!pending.accepts("new"));
    const auto vocabulary=lookup_words({"Give up? Give UP!", "don't, well-known; don’t; 123."});
    CHECK(vocabulary.size()==4);
    CHECK(vocabulary[0]=="Give" && vocabulary[1]=="up");
    CHECK(vocabulary[2]=="don't" && vocabulary[3]=="well-known");
    CHECK(lookup_words({"--123--",std::string(65,'x')}).empty());
    std::string large;
    for(int i=0;i<600;++i) large+="word"+std::to_string(i)+" ";
    CHECK(lookup_words({large}).size()==400);
    const auto demo=demo_session();
    CHECK(demo.demo && !demo.translation.empty());
    for(const auto& word:demo.words) CHECK(demo.cache.count(word)==1);
    CHECK(translation_document(demo.lines)=="Find the silver key.\nReturn to the castle.");
    std::cout << checks << " UI checks passed\n";
}
