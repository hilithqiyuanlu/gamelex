#pragma once
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
using u64=std::uint64_t;using u32=std::uint32_t;using u16=std::uint16_t;using s32=std::int32_t;using Result=u32;
inline u64 fake_ms=1000;
inline u64 svcGetSystemTick() { return fake_ms*1000000; }
inline u64 armTicksToNs(u64 tick) { return tick; }
inline u64 envGetHeapOverrideSize() { return 3*1024*1024; }
inline bool R_SUCCEEDED(Result rc) { return rc==0; }
inline bool R_FAILED(Result rc) { return rc!=0; }
inline unsigned R_MODULE(Result rc) { return rc&511; }
inline unsigned R_DESCRIPTION(Result rc) { return rc>>9; }
inline Result pmdmntGetProcessId(u64* pid,u64) { *pid=42;return 0; }
inline Result pmshellInitialize() { return 0; }
inline void pmshellExit() {}
inline constexpr int NcmStorageId_None=0;
struct NcmProgramLocation { u64 id;int storage;int reserved; };
inline Result pmshellLaunchProgram(int,const NcmProgramLocation*,u64* pid) { *pid=42;return 0; }
struct HidTouchState {};
struct HidAnalogStickState { int x=0,y=0; };
inline constexpr u64 HidNpadButton_A=1,HidNpadButton_B=2,HidNpadButton_X=4,HidNpadButton_Y=8,
 HidNpadButton_L=16,HidNpadButton_R=32,HidNpadButton_Plus=64,HidNpadButton_Minus=128,
 HidNpadButton_Up=256,HidNpadButton_Down=512,HidNpadButton_Left=1024,HidNpadButton_Right=2048;
inline std::atomic<bool> gamelexCaptureBlanking{false},screenshotsAreForceDisabled{false};
inline std::string returnOverlayPath="sdmc:/switch/.overlays/ovlmenu.ovl",next_load,next_args;
inline void envSetNextLoad(const char* path,const char* args) { next_load=path;next_args=args; }
namespace ult {
inline int DefaultFramebufferWidth=448,DefaultFramebufferHeight=720,layerEdge=0;
inline bool windowedLayerPixelPerfect=false;
inline std::atomic<bool> stillTouching{false},currentForeground{true};
inline void clearTranslationCache() {}
}
namespace tsl {
struct Color { Color(int,int,int,int) {} };
namespace gfx {
struct Renderer {
    std::vector<std::string> text;
    int clears=0;
    static Renderer& get() { static Renderer r;return r; }
    void clearScreen() { ++clears; }
    void removeScreenshotStacks(bool) { screenshotsAreForceDisabled=true; }
    void drawString(const std::string& s,bool,int,int,int,Color) { text.push_back(s); }
    void drawRect(int,int,int,int,Color) {}
    void drawRoundedRect(int,int,int,int,int,Color) {}
    void enableScissoring(int,int,int,int) {}
    void disableScissoring() {}
    std::pair<int,int> getTextDimensions(const std::string& s,bool,int size) {
        int width=0;for(unsigned char c:s) { if(c<128) width+=size/2;else if((c&0xc0)!=0x80) width+=size; }
        return {width,size};
    }
};
}
enum class FocusDirection { None };
namespace elm {
enum class TouchEvent { Touch,Hold,Release };
class Element {
public:
    virtual ~Element()=default;
    virtual void layout(u16,u16,u16,u16) {}
    virtual void draw(gfx::Renderer*) {}
    virtual void drawFocusBackground(gfx::Renderer*) {}
    virtual void drawHighlight(gfx::Renderer*) {}
    virtual void drawClickFlash(gfx::Renderer*) {}
    virtual Element* requestFocus(Element*,FocusDirection) { return this; }
    virtual bool handleInput(u64,u64,const HidTouchState&,HidAnalogStickState,HidAnalogStickState) { return false; }
    virtual bool onTouch(TouchEvent,s32,s32,s32,s32,s32,s32) { return false; }
    int getX() const { return x; } int getY() const { return y; }
    int getWidth() const { return w; } int getHeight() const { return h; }
    bool inBounds(int px,int py) const { return px>=x && py>=y && px<x+w && py<y+h; }
    void setBoundaries(int a,int b,int c,int d) { x=a;y=b;w=c;h=d;layout(a,b,c,d); }
private:int x=0,y=0,w=448,h=720;
};
class OverlayFrame : public Element {
public:
    std::string title;
    std::unique_ptr<Element> content;
    OverlayFrame(const std::string& name,const std::string&) : title(name) {}
    void setContent(Element* value) { content.reset(value);layout(0,0,448,720); }
    void layout(u16,u16,u16,u16) override { if(content) content->setBoundaries(35,97,363,542); }
    void draw(gfx::Renderer* r) override { r->text.push_back(title);if(content) content->draw(r); }
};
}
class Gui {
public:
    std::unique_ptr<elm::Element> root;
    virtual ~Gui()=default;
    virtual elm::Element* createUI()=0;
    virtual void update() {}
    virtual bool handleInput(u64,u64,const HidTouchState&,HidAnalogStickState,HidAnalogStickState) { return false; }
};
inline std::vector<std::unique_ptr<Gui>> stack;
inline bool closed=false;
inline void install(std::unique_ptr<Gui> gui) { gui->root.reset(gui->createUI());stack.push_back(std::move(gui)); }
template<class T,class... Args> void changeTo(Args&&... args) { install(std::make_unique<T>(std::forward<Args>(args)...)); }
template<class T,class... Args> void swapTo(Args&&... args) { auto next=std::make_unique<T>(std::forward<Args>(args)...);stack.pop_back();install(std::move(next)); }
inline void goBack() {
 if(ult::stillTouching && ult::currentForeground) return;
 if(stack.size()>1) stack.pop_back();else closed=true;
}
class Overlay {
public:
    virtual ~Overlay()=default;
    virtual void initServices() {} virtual void exitServices() {} virtual void onShow() {} virtual void onHide() {}
    virtual std::unique_ptr<Gui> loadInitialGui() { return {}; }
    template<class T> std::unique_ptr<Gui> initially() { return std::make_unique<T>(); }
    static Overlay* get() { static Overlay o;return &o; }
    void close(bool) { closed=true; }
};
template<class T> int loop(int,char**) { return 0; }
}
