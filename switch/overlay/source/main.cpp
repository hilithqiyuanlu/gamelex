#define TESLA_INIT_IMPL
#include <tesla.hpp>
#include "probe_common.hpp"
#include "gamelex/ui.hpp"
#include "gamelex/reading.hpp"
#include "gamelex/group_reader.hpp"
#include "gamelex/release.hpp"
#include <cstdio>
#include <malloc.h>
#include <memory>

using namespace gamelex;
using namespace gamelex::probe;
namespace {
constexpr const char* Version=ReleaseVersion;
reading::CaptureGate captureGate;
u64 clockMs() { return armTicksToNs(svcGetSystemTick())/1000000; }
void trace(const char* stage) {
    FILE* f=std::fopen((runtime_dir()+"/overlay-safe.log").c_str(),"a");
    if (!f) return;
    const auto heap=mallinfo();
    std::fprintf(f,"%s %s tick=%llu heap_used=%llu heap_limit=%llu\n",Version,stage,
        static_cast<unsigned long long>(svcGetSystemTick()),static_cast<unsigned long long>(heap.uordblks),
        static_cast<unsigned long long>(envGetHeapOverrideSize()));
    std::fclose(f);
}
void restoreCapture() {
    if (!captureGate.active) return;
    captureGate.finish();
    gamelexCaptureBlanking.store(false,std::memory_order_release);
    atomic_json(runtime_dir()+"/overlay.json",{{"visible",true},{"tick_ms",clockMs()}});
    trace("capture_restored");
}
// Keep the standard frame, focus/input loop and double framebuffer. During
// capture, submit transparent frames from inside its existing draw cycle.
// Never call Overlay::clearScreen/startFrame or change the framebuffer count.
class LoggedFrame : public tsl::elm::OverlayFrame {
public:
    LoggedFrame(const std::string& title,const char* stage) : OverlayFrame(title,Version),stage(stage) {}
    void draw(tsl::gfx::Renderer* renderer) override {
        // Only this process's layer is excluded; the Ultrahand menu is a
        // separate process/layer and keeps its own screenshot preferences.
        if (!screenshotsAreForceDisabled.load(std::memory_order_acquire)) renderer->removeScreenshotStacks(true);
        if (captureGate.active) { renderer->clearScreen(); captureGate.drawn(); return; }
        tsl::elm::OverlayFrame::draw(renderer);
        if (!recorded) { trace(stage); recorded=true; }
    }
private:
    const char* stage;
    bool recorded=false;
};
void exitTool() {
    trace("exit_requested");
    envSetNextLoad(returnOverlayPath.c_str(),"ovlmenu.ovl");
    tsl::Overlay::get()->close(true);
}
using Session=ui::LookupSession;
using SessionPtr=std::shared_ptr<Session>;
using Book=ui::GroupBook;
using BookPtr=std::shared_ptr<Book>;

// A reading area inside the stock sidebar. It has no per-line menu entries.
// Pixel width/height determine the visible rows; text scrolls only if needed.
class Document : public tsl::elm::Element {
public:
    std::function<void()> toggle,select,retry;
    std::function<void(const std::string&)> lookup;
    explicit Document(std::string text,bool source=false) : source(source) { setText(std::move(text),source); }
    void setText(std::string value,bool english) {
        if (text==value && source==english) return;
        text=std::move(value); source=english; selecting=false; selected=0; extent=1; first=0; dirty=true;
    }
    void setStatus(std::string value) { status=std::move(value); }
    bool isSource() const { return source; }
    bool interacted=false;
    void selectWords() {
        interacted=true;
        rebuild(); selecting=true; extent=1;
        if (!tokens.empty()) {
            for (const auto& mark:positions) if (mark.row>=first) { selected=mark.word; break; }
            ensureSelection();
        }
    }
    void layout(u16,u16,u16,u16) override { dirty=true; }
    tsl::elm::Element* requestFocus(tsl::elm::Element*,tsl::FocusDirection) override { return this; }
    void drawFocusBackground(tsl::gfx::Renderer*) override {}
    void drawHighlight(tsl::gfx::Renderer*) override {}
    void drawClickFlash(tsl::gfx::Renderer*) override {}
    void draw(tsl::gfx::Renderer* r) override {
        rebuild();
        const auto white=tsl::Color(15,15,15,15),muted=tsl::Color(10,11,12,15),cyan=tsl::Color(0,14,13,15);
        r->drawString(status,false,getX()+6,getY()+24,20,muted);
        const int top=bodyTop(),bottom=top+visible()*LineHeight;
        r->enableScissoring(getX(),top,getWidth(),bottom-top);
        if (selecting) for (const auto& m:positions) {
            if (m.word<selected || m.word>=selected+extent || m.row<first || m.row>=first+visible()) continue;
            r->drawRect(getX()+4+m.x,top+(m.row-first)*LineHeight+2,m.w+4,LineHeight-2,tsl::Color(0,5,6,15));
        }
        for (int i=first;i<static_cast<int>(rows.size()) && i<first+visible();++i)
            r->drawString(rows[i].text,false,getX()+6,top+(i-first)*LineHeight+26,23,white);
        r->disableScissoring();
        if (static_cast<int>(rows.size())>visible()) {
            const int height=visible()*LineHeight;
            const int thumb=std::max(12,height*visible()/static_cast<int>(rows.size()));
            const int y=top+(height-thumb)*first/std::max(1,static_cast<int>(rows.size())-visible());
            r->drawRect(getX()+getWidth()-3,y,2,thumb,cyan);
        }
        const int bar=barTop();
        r->drawRect(getX(),bar,getWidth(),1,tsl::Color(5,6,6,15));
        if (toggle) r->drawString("X 中英对照",false,getX()+6,bar+29,20,cyan);
        if (select) r->drawString(selecting ? "L/R 扩选" : "Y 选词",false,getX()+getWidth()/2+6,bar+29,20,cyan);
        if (!toggle && !select) r->drawString(static_cast<int>(rows.size())>visible() ? "↑↓ 滚动 · B 返回" : "B 返回",false,getX()+6,bar+29,20,muted);
    }
    bool handleInput(u64 down,u64 held,const HidTouchState&,HidAnalogStickState stick,HidAnalogStickState) override {
        if ((down&HidNpadButton_X) && toggle) { interacted=true; toggle(); return true; }
        if ((down&HidNpadButton_Y) && select) { interacted=true; select(); return true; }
        if ((down&HidNpadButton_Plus) && retry) { retry(); return true; }
        rebuild();
        if ((down&HidNpadButton_A) && selecting) { query(); return true; }
        if (selecting && (down&(HidNpadButton_L|HidNpadButton_R))) {
            if (down&HidNpadButton_L) extent=std::max(1,extent-1);
            else if (extent<6 && selected+extent<static_cast<int>(tokens.size()) &&
                reading::may_extend(text,tokens[selected+extent-1],tokens[selected+extent])) ++extent;
            ensureSelection(); return true;
        }
        const u64 now=clockMs();
        u64 direction=down&(HidNpadButton_Up|HidNpadButton_Down|HidNpadButton_Left|HidNpadButton_Right);
        if (!direction && now-lastMove>=160) direction=held&(HidNpadButton_Up|HidNpadButton_Down|HidNpadButton_Left|HidNpadButton_Right);
        if (!direction && now-lastMove>=160) {
            if (stick.y>16000) direction=HidNpadButton_Up;
            else if (stick.y< -16000) direction=HidNpadButton_Down;
            else if (stick.x>16000) direction=HidNpadButton_Right;
            else if (stick.x< -16000) direction=HidNpadButton_Left;
        }
        if (!direction) return false;
        lastMove=now; interacted=true;
        const int delta=(direction&(HidNpadButton_Up|HidNpadButton_Left)) ? -1 : 1;
        if (selecting && !tokens.empty()) {
            if (direction&(HidNpadButton_Left|HidNpadButton_Right))
                selected=std::clamp(selected+delta,0,static_cast<int>(tokens.size())-1);
            else {
                int row=-1,x=0;
                for (const auto& m:positions) if (m.word==selected) { row=m.row; x=m.x; break; }
                int closest=-1,distance=10000000;
                for (const auto& m:positions) if ((m.row-row)*delta>0 &&
                    std::abs(m.row-row)*10000+std::abs(m.x-x)<distance) {
                    closest=m.word; distance=std::abs(m.row-row)*10000+std::abs(m.x-x);
                }
                if (closest>=0) selected=closest;
                else first=reading::scroll(first,delta,rows.size(),visible());
            }
            extent=1; ensureSelection();
        } else first=reading::scroll(first,delta*((direction&(HidNpadButton_Left|HidNpadButton_Right)) ? visible() : 1),rows.size(),visible());
        return true;
    }
    bool onTouch(tsl::elm::TouchEvent event,s32 x,s32 y,s32,s32,s32,s32) override {
        using Event=tsl::elm::TouchEvent;
        if (event==Event::Touch) {
            if (!inBounds(x,y)) return false;
            interacted=true; // Do not switch languages beneath a held finger.
            touching=true; touchX=x; touchY=y; touchFirst=first; dragged=false; return true;
        }
        if (!touching) return false;
        const int dy=touchY-y;
        if (std::abs(dy)>12 || std::abs(x-touchX)>12) dragged=true;
        if (event!=Event::Release) {
            if (dragged) { interacted=true; first=reading::scroll(touchFirst,dy/LineHeight,rows.size(),visible()); }
            return true;
        }
        touching=false;
        if (dragged || !inBounds(x,y)) return true;
        interacted=true;
        if (y>=barTop()) {
            if (x<getX()+getWidth()/2 && toggle) toggle();
            else if (select) select();
            return true;
        }
        if (y<bodyTop()) { if (retry) retry(); return true; }
        if (y>=bodyTop()+visible()*LineHeight) return true;
        if (!source) return true;
        const int row=first+(y-bodyTop())/LineHeight;
        for (const auto& m:positions) if (m.row==row && x>=getX()+4+m.x && x<getX()+m.x+m.w+9) {
            selected=m.word; extent=1; selecting=true; query(); return true;
        }
        return true;
    }
private:
    static constexpr int LineHeight=34;
    std::string text,status;
    bool source=false,dirty=true,selecting=false,touching=false,dragged=false;
    int first=0,selected=0,extent=1,layoutWidth=0,touchX=0,touchY=0,touchFirst=0;
    u64 lastMove=0;
    std::vector<reading::Row> rows;
    std::vector<reading::Word> tokens;
    std::vector<reading::Mark> positions;
    int bodyTop() { return getY()+40; }
    int barTop() { return getY()+getHeight()-45; }
    int visible() { return reading::visible_rows(getHeight()-91,LineHeight); }
    void rebuild() {
        if (!dirty && layoutWidth==getWidth()) return;
        layoutWidth=getWidth(); dirty=false;
        auto& r=tsl::gfx::Renderer::get();
        auto measure=[&](const std::string& value) { return r.getTextDimensions(value,false,23).first; };
        rows=reading::layout(text,std::max(20,getWidth()-18),measure);
        tokens=source ? reading::words(text) : std::vector<reading::Word>{};
        positions=reading::marks(rows,tokens,measure);
        first=reading::scroll(first,0,rows.size(),visible());
    }
    void ensureSelection() {
        for (const auto& m:positions) if (m.word==selected+extent-1) {
            first=reading::reveal(first,m.row,rows.size(),visible()); break;
        }
    }
    void query() {
        if (!lookup || selected<0 || selected>=static_cast<int>(tokens.size())) return;
        const auto end=tokens[std::min(selected+extent-1,static_cast<int>(tokens.size())-1)].end;
        const auto value=text.substr(tokens[selected].begin,end-tokens[selected].begin);
        lookup(value);
    }
};
// One focusable reading surface, rather than a menu entry per OCR line.
class GroupCards : public tsl::elm::Element {
public:
    std::function<void()> open,retry;
    explicit GroupCards(BookPtr book) : book(std::move(book)) {}
    void layout(u16,u16,u16,u16) override { dirty=true; }
    tsl::elm::Element* requestFocus(tsl::elm::Element*,tsl::FocusDirection) override { return this; }
    void drawFocusBackground(tsl::gfx::Renderer*) override {}
    void drawHighlight(tsl::gfx::Renderer*) override {}
    void drawClickFlash(tsl::gfx::Renderer*) override {}
    void draw(tsl::gfx::Renderer* r) override {
        rebuild();
        const auto white=tsl::Color(15,15,15,15),muted=tsl::Color(10,11,12,15),cyan=tsl::Color(0,14,13,15);
        r->drawString("选择英文组，再翻译",false,getX()+5,getY()+23,23,white);
        const auto count=book->groups.size();
        std::string status=count ? "共 "+to_text(count)+" 组 · 选中 "+to_text(book->selected+1) : "尚未取得文字";
        if (book->extent>1) status+="–"+to_text(book->selected+book->extent);
        r->drawString(status,false,getX()+5,getY()+49,20,muted);
        const int top=bodyTop();
        r->enableScissoring(getX(),top,getWidth(),viewport.height);
        if (!count) {
            r->drawString("A 重新识别",false,getX()+14,top+45,23,white);
            r->drawString("B 返回游戏",false,getX()+14,top+82,20,muted);
        }
        for (std::size_t i=0;i<preview.size();++i) {
            const int y=top+viewport.tops[i]-viewport.offset;
            if (y+viewport.heights[i]<top || y>=top+viewport.height) continue;
            const bool selected=i>=book->selected && i<book->selected+book->extent;
            r->drawRoundedRect(getX()+2,y,getWidth()-10,viewport.heights[i]-8,7,
                selected ? tsl::Color(0,4,5,15) : tsl::Color(2,2,3,15));
            if (selected) r->drawRect(getX()+3,y+8,3,viewport.heights[i]-24,cyan);
            const auto& g=book->groups[i];
            const char* position=!g.located ? "位置未知" : g.x+g.w/2<.38 ? "左侧" : g.x+g.w/2>.62 ? "右侧" : "中间";
            std::string label=to_text(i+1)+" · "+position;
            const auto saved=book->cached(i,1);
            if (saved && !saved->translation.empty()) label+=" · 已译";
            r->drawString(label,false,getX()+14,y+24,20,selected ? cyan : muted);
            for (std::size_t row=0;row<preview[i].size();++row)
                r->drawString(preview[i][row],false,getX()+14,y+52+row*29,23,white);
        }
        r->disableScissoring();
        if (viewport.total>viewport.height) {
            const int thumb=std::max(12,viewport.height*viewport.height/viewport.total);
            const int y=top+(viewport.height-thumb)*viewport.offset/std::max(1,viewport.total-viewport.height);
            r->drawRect(getX()+getWidth()-3,y,2,thumb,cyan);
        }
        const int bar=barTop();
        r->drawRect(getX(),bar,getWidth(),1,tsl::Color(5,6,6,15));
        r->drawString("L 缩选          R 扩选",false,getX()+6,bar+26,20,cyan);
        r->drawString("+ 重新识别     B 返回游戏",false,getX()+6,bar+54,20,muted);
    }
    bool handleInput(u64 down,u64 held,const HidTouchState&,HidAnalogStickState stick,HidAnalogStickState) override {
        if (down&HidNpadButton_Plus) { if (retry) retry();return true; }
        if (down&HidNpadButton_A) { activate();return true; }
        rebuild();
        if (down&(HidNpadButton_L|HidNpadButton_R)) {
            if (down&HidNpadButton_L) book->shrink();else book->expand();
            viewport.reveal(book->selected+book->extent-1);return true;
        }
        const auto now=clockMs();
        u64 direction=down&(HidNpadButton_Up|HidNpadButton_Down);
        if (!direction && now-lastMove>=170) direction=held&(HidNpadButton_Up|HidNpadButton_Down);
        if (!direction && now-lastMove>=170) {
            if (stick.y>16000) direction=HidNpadButton_Up;
            if (stick.y< -16000) direction=HidNpadButton_Down;
        }
        if (!direction) return false;
        lastMove=now;book->move(direction&HidNpadButton_Up ? -1 : 1);viewport.reveal(book->selected);return true;
    }
    bool onTouch(tsl::elm::TouchEvent event,s32 x,s32 y,s32,s32,s32,s32) override {
        using Event=tsl::elm::TouchEvent;
        if (event==Event::Touch) {
            if (!inBounds(x,y)) return false;
            rebuild();touching=true;dragged=false;touchX=x;touchY=y;touchOffset=viewport.offset;return true;
        }
        if (!touching) return false;
        if (std::abs(y-touchY)>12 || std::abs(x-touchX)>12) dragged=true;
        if (event!=Event::Release) {
            if (dragged && touchY>=bodyTop() && touchY<barTop()) { viewport.offset=touchOffset+touchY-y;viewport.clamp(); }
            return true;
        }
        touching=false;
        if (dragged || !inBounds(x,y)) return true;
        if (y>=barTop()+31) {
            if (x<getX()+getWidth()/2) { if (retry) retry(); } else exitTool();
        } else if (y>=barTop()) {
            if (x<getX()+getWidth()/2) book->shrink();else book->expand();
            viewport.reveal(book->selected+book->extent-1);
        } else if (book->groups.empty()) { if (retry) retry(); }
        else {
            const int index=viewport.hit(y-bodyTop());
            if (index>=0) { book->choose(index);activate(); }
        }
        return true;
    }
private:
    BookPtr book;
    ui::CardViewport viewport;
    std::vector<std::vector<std::string>> preview;
    unsigned revision=~0U;
    int width=0,touchX=0,touchY=0,touchOffset=0;
    u64 lastMove=0;
    bool dirty=true,touching=false,dragged=false;
    int bodyTop() { return getY()+63; }
    int barTop() { return getY()+getHeight()-64; }
    void activate() { if (book->groups.empty()) { if (retry) retry(); } else if (open) open(); }
    void rebuild() {
        if (!dirty && width==getWidth() && revision==book->revision) return;
        const bool fresh=revision!=book->revision;
        dirty=false;width=getWidth();revision=book->revision;preview.clear();
        auto& renderer=tsl::gfx::Renderer::get();
        auto measure=[&](const std::string& value) { return renderer.getTextDimensions(value,false,23).first; };
        std::vector<int> heights;
        for (const auto& group:book->groups) {
            auto sample=group.text.substr(0,400);
            // Never leave a partial UTF-8 codepoint at the preview boundary.
            while (sample.size()<group.text.size() && !sample.empty() &&
                (static_cast<unsigned char>(group.text[sample.size()])&0xc0)==0x80) sample.pop_back();
            const auto rows=reading::layout(sample,std::max(20,getWidth()-36),measure);
            std::vector<std::string> visible;
            for (std::size_t i=0;i<std::min<std::size_t>(3,rows.size());++i) visible.push_back(rows[i].text);
            if (rows.size()>3 || sample.size()<group.text.size()) {
                auto& last=visible.back();
                while (!last.empty() && measure(last+"…")>getWidth()-36) {
                    auto end=last.size()-1;while (end>0 && (static_cast<unsigned char>(last[end])&0xc0)==0x80) --end;last.resize(end);
                }
                last+="…";
            }
            heights.push_back(40+static_cast<int>(visible.size())*29+14);preview.push_back(std::move(visible));
        }
        if (fresh) viewport.offset=0;
        viewport.set(std::move(heights),std::max(1,barTop()-bodyTop()-5));
        if (fresh) viewport.reveal(book->selected);
    }
};
class ReaderGui : public tsl::Gui {
public:
    ReaderGui(std::string title,std::string content,std::string status="中文释义")
        : title(std::move(title)),content(std::move(content)),status(std::move(status)) {}
    tsl::elm::Element* createUI() override {
        auto* frame=new LoggedFrame(title,"reader_drawn");
        auto* document=new Document(content); document->setStatus(status);
        frame->setContent(document); return frame;
    }
private:
    std::string title,content,status;
};
class RequestGui : public tsl::Gui {
public:
    RequestGui(std::string command,SessionPtr session={},std::string word={},BookPtr book={})
        : command(std::move(command)),word(std::move(word)),session(std::move(session)),book(std::move(book)) {}
    ~RequestGui() override {
        restoreCapture();
        if (pending) { std::string cancelled; gamelex::probe::request("overlay","cancel",cancelled); trace("request_cancelled"); }
    }
    tsl::elm::Element* createUI() override {
        auto* frame=new LoggedFrame("正在处理","request_drawn");
        auto* doc=new Document(command=="scan" ? "正在识别游戏文字。\n\n截图时工具界面会短暂消失，截图完成后自动恢复。\n\nB 取消并返回。" : "正在查询所选词语。\n\nB 取消并返回。");
        doc->setStatus(command=="scan" ? "截图 → 识别" : "词语查询"); frame->setContent(doc); return frame;
    }
    void update() override;
private:
    std::string command,id,word;
    SessionPtr session;
    BookPtr book;
    bool started=false,pending=false,backendChecked=false,captureSignalled=false,returning=false;
    u64 began=0,lastRead=0;
    Status startBackend();
    void error(const std::string& code,const std::string& message) {
        pending=false; restoreCapture(); trace("request_failed");
        tsl::swapTo<ReaderGui>(command=="scan" ? "识别失败" : "查询失败",message+"\n\n诊断代码："+code,"B 返回后可重试");
    }
};
class ResultGui : public tsl::Gui {
public:
    explicit ResultGui(SessionPtr session) : session(std::move(session)) {}
    ~ResultGui() override { cancelTranslation(); }
    tsl::elm::Element* createUI() override {
        auto* frame=new LoggedFrame("所选文字","result_drawn");
        document=new Document(session->translation.empty() ? sourceText() : session->translation,session->translation.empty());
        document->toggle=[this] {
            document->interacted=true;
            if (!document->isSource()) document->setText(sourceText(),true);
            else if (!session->translation.empty()) document->setText(session->translation,false);
            else if (!translationPending) beginTranslation();
            refreshStatus();
        };
        document->select=[this] {
            document->setText(sourceText(),true); document->selectWords(); refreshStatus();
        };
        document->lookup=[this](const std::string& word) {
            cancelTranslation();
            const auto found=session->word_cache().find(word);
            if (found!=session->word_cache().end()) tsl::changeTo<ReaderGui>("词语释义",word+"\n\n"+found->second);
            else tsl::changeTo<RequestGui>("translate",session,word);
        };
        document->retry=[this] { if (!translationPending && session->translation.empty()) beginTranslation(); };
        refreshStatus(); frame->setContent(document); return frame;
    }
    void update() override {
        if (!attempted && session->translation.empty()) { beginTranslation(); return; }
        if (!translationPending || clockMs()-lastRead<150) return;
        lastRead=clockMs();
        if (clockMs()-began>35000) {
            cancelTranslation(); failure="等待整段翻译超过 35 秒，英文已保留。";
            session->translation_error="翻译超时 · -详情 +重试"; refreshStatus(); return;
        }
        Json response;
        if (!read_json(response_path("overlay"),response).ok() || field(response,"id")!=translationId) return;
        translationPending=false;
        if (!response.contains("ok") || !response["ok"].is_boolean() || !response["ok"].get<bool>()) {
            failure=field(response,"message","服务返回异常")+"\n\n错误编号："+field(response,"code");
            session->translation_error="翻译失败 · -详情 +重试"; refreshStatus(); trace("translation_failed"); return;
        }
        std::vector<std::string> parts;
        if (response.contains("data") && response["data"].is_object()) {
            const auto& data=response["data"];
            if (data.contains("translations") && data["translations"].is_array()) {
                std::size_t bytes=0;
                for (const auto& item:data["translations"]) if (item.is_string()) {
                    const auto value=item.get<std::string>();
                    if (value.size()>16000-bytes) break;
                    bytes+=value.size(); parts.push_back(value);
                }
            }
        }
        session->translation=reading::document(parts);
        if (session->translation.empty()) session->translation_error="没有返回译文，按 + 重试";
        else {
            session->translation_error.clear();
            if (!document->interacted) document->setText(session->translation,false);
            trace("translation_succeeded");
        }
        refreshStatus();
    }
    bool handleInput(u64 keys,u64,const HidTouchState&,HidAnalogStickState,HidAnalogStickState) override {
        if ((keys&HidNpadButton_Minus) && !failure.empty()) {
            tsl::changeTo<ReaderGui>("翻译未完成",failure,"B 返回后可重试"); return true;
        }
        return false;
    }
private:
    SessionPtr session;
    Document* document=nullptr;
    bool attempted=false,translationPending=false;
    std::string translationId,failure;
    u64 began=0,lastRead=0;
    std::string sourceText() const { return reading::document(session->lines); }
    void refreshStatus() {
        if (!document) return;
        document->setStatus(translationPending ? "正在翻译所选文字…" : !session->translation_error.empty() ? session->translation_error :
            document->isSource() ? "英文原文 · 点词查询" : "中文译文 · 上下滚动");
    }
    void beginTranslation() {
        attempted=true; session->translation_error.clear(); failure.clear();
        if (!session->translation.empty()) return;
        const auto status=gamelex::probe::request("overlay","translate_all",translationId,"",{{"lines",session->lines}});
        if (!status.ok()) {
            failure=status.message+"\n\n错误编号："+status.code;
            session->translation_error="翻译未启动 · -详情 +重试"; refreshStatus(); return;
        }
        translationPending=true; began=clockMs(); lastRead=0; refreshStatus(); trace("translation_sent");
    }
    void cancelTranslation() {
        if (!translationPending) return;
        std::string cancelled; gamelex::probe::request("overlay","cancel",cancelled);
        translationPending=false; session->translation_error="已停止翻译，按 + 重试";
        refreshStatus();
    }
};
Status RequestGui::startBackend() {
    constexpr u64 program=ProgramId;
    u64 pid=0;
    const auto query=pmdmntGetProcessId(&pid,program);
    Json diagnostic={{"program_id","42000000474C4558"},{"query_result",query},{"pid",pid}};
    if (R_SUCCEEDED(query) && pid) {
        diagnostic["state"]="running";
        atomic_json(runtime_dir()+"/backend-launch.json",diagnostic);
        return {};
    }
    auto rc=pmshellInitialize();
    diagnostic["service_result"]=rc;
    if (R_SUCCEEDED(rc)) {
        const NcmProgramLocation location{program,NcmStorageId_None,{}};
        rc=pmshellLaunchProgram(0,&location,&pid);
        pmshellExit();
        diagnostic["launch_result"]=rc;
        diagnostic["pid"]=pid;
    }
    diagnostic["state"]=R_SUCCEEDED(rc) ? "launched" : "failed";
    atomic_json(runtime_dir()+"/backend-launch.json",diagnostic);
    if (R_FAILED(rc)) {
        char detail[96];
        std::snprintf(detail,sizeof(detail),"后台启动失败：0x%08X（%u/%u）。",rc,R_MODULE(rc),R_DESCRIPTION(rc));
        return Status::error("backend_start_failed",std::string(detail)+
            "请完整重启进入大气层后重试。安装文件或系统可用内存也可能导致启动失败。");
    }
    trace("backend_launched");
    return {};
}void RequestGui::update() {
    // Ultrahand intentionally rejects goBack while a finger is held. Retry
    // the transition after release instead of leaving a completed request stuck.
    if (returning) {
        if (!(ult::stillTouching.load(std::memory_order_acquire) && ult::currentForeground.load(std::memory_order_acquire))) tsl::goBack();
        return;
    }
    const u64 now=clockMs();
    if (!started) {
        if (!backendChecked) {
            backendChecked=true; began=now;
            const auto status=startBackend();
            if (!status.ok()) { error(status.code,status.message); return; }
        }
        if (command=="scan") {
            if (!captureGate.active) {
                gamelexCaptureBlanking.store(true,std::memory_order_release);
                captureGate.begin(unique_id());
            }
            if (!captureGate.ready()) return;
            if (!captureSignalled) {
                const auto status=atomic_json(runtime_dir()+"/overlay.json",{{"visible",false},
                    {"capture_nonce",captureGate.nonce},{"tick_ms",now}});
                if (!status.ok()) { error(status.code,status.message); return; }
                captureSignalled=true; trace("capture_blanked");
            }
        }
        if (now-lastRead<150) return;
        lastRead=now;
        Json extra=Json::object();
        if (command=="translate") extra["text"]=word;
        if (command=="scan") extra["capture_nonce"]=captureGate.nonce;
        const auto status=gamelex::probe::request("overlay",command,id,"",extra);
        if (!status.ok()) {
            if (status.code=="backend_unavailable" && now-began<8000) return;
            error(status.code,status.message); return;
        }
        started=true; began=now; pending=true; trace("request_sent");
    }
    if (!pending || now-lastRead<150) return;
    lastRead=now;
    if (now-began>(command=="scan" ? 90000ULL : 35000ULL)) {
        std::string cancelled; gamelex::probe::request("overlay","cancel",cancelled);
        error("timeout","等待超时，请检查网络后手动重试。"); return;
    }
    Json response;
    if (!read_json(response_path("overlay"),response).ok() || field(response,"id")!=id) return;
    if (!response.contains("ok") || !response["ok"].is_boolean() || !response["ok"].get<bool>()) {
        error(field(response,"code"),field(response,"message","请求失败")); return;
    }
    const Json empty=Json::object();
    const auto& data=response.contains("data") && response["data"].is_object() ? response["data"] : empty;
    if (command=="scan" && field(data,"state")=="captured") { restoreCapture(); return; }
    pending=false; restoreCapture(); trace("request_succeeded");
    if (command=="scan") {
        std::vector<reading::Group> groups;
        std::size_t bytes=0;
        if (data.contains("groups") && data["groups"].is_array()) for (const auto& item:data["groups"]) {
            const auto text=field(item,"text");
            if (text.empty()) continue;
            if (groups.size()>=400 || text.size()>16000-bytes) { error("too_much_text","当前画面文字过多，已停止读取。");return; }
            bytes+=text.size();reading::Group group;group.text=text;
            for (const auto key:{"x","y","w","h"}) if (item.contains(key) && item[key].is_number()) {
                const auto value=item[key].get<double>();
                if (key[0]=='x') group.x=value;else if (key[0]=='y') group.y=value;
                else if (key[0]=='w') group.w=value;else group.h=value;
            }
            group.located=reading::positioned(group);
            if (item.contains("lines") && item["lines"].is_number_unsigned()) group.lines=std::clamp(item["lines"].get<unsigned>(),1U,400U);
            groups.push_back(std::move(group));
        }
        if (groups.empty()) { error("no_text","没有识别到文字，请停留在清晰的英文画面后重试。");return; }
        if (!book) { error("invalid_session","识别会话已失效，请重新打开 GameLex。");return; }
        book->replace(std::move(groups));
        trace("groups_ready_no_translation");returning=true;
        if (!(ult::stillTouching.load(std::memory_order_acquire) && ult::currentForeground.load(std::memory_order_acquire))) tsl::goBack();
        return;
    }
    const auto value=field(data,"translation");
    if (value.empty() || value.size()>16000) { error("invalid_translation","服务没有返回可用的释义。"); return; }
    ui::remember_lookup(*session,word,value);
    tsl::swapTo<ReaderGui>("词语释义",word+"\n\n"+value);
}
class GroupsGui : public tsl::Gui {
public:
    GroupsGui() : book(std::make_shared<Book>()) {}
    tsl::elm::Element* createUI() override {
        auto* frame=new LoggedFrame("GameLex · 选择文字","groups_drawn");
        auto* cards=new GroupCards(book);
        cards->open=[this] { if (auto session=book->open()) tsl::changeTo<ResultGui>(std::move(session)); };
        cards->retry=[this] { scan(); };
        frame->setContent(cards);return frame;
    }
    void update() override { if (book->take_initial_scan()) scan(); }
    bool handleInput(u64 keys,u64,const HidTouchState&,HidAnalogStickState,HidAnalogStickState) override {
        if (keys&HidNpadButton_B) { exitTool();return true; }return false;
    }
private:
    BookPtr book;
    void scan() { tsl::changeTo<RequestGui>("scan",SessionPtr{},std::string{},book); }
};
class GameLexOverlay : public tsl::Overlay {
public:
    void initServices() override {
        directories(); ult::clearTranslationCache();
        ult::DefaultFramebufferWidth=448; ult::DefaultFramebufferHeight=720;
        ult::windowedLayerPixelPerfect=false; ult::layerEdge=0;
        trace("init_standard_448x720_double_buffer");
    }
    void onShow() override { atomic_json(runtime_dir()+"/overlay.json",{{"visible",true},{"tick_ms",clockMs()}}); trace("shown"); }
    void onHide() override {
        captureGate.finish(); gamelexCaptureBlanking.store(false,std::memory_order_release); atomic_json(runtime_dir()+"/overlay.json",{{"visible",false},{"tick_ms",clockMs()}}); trace("hidden");
    }
    void exitServices() override {
        captureGate.finish(); gamelexCaptureBlanking.store(false,std::memory_order_release); atomic_json(runtime_dir()+"/overlay.json",{{"visible",false},{"tick_ms",clockMs()}}); trace("exit_complete");
    }
    std::unique_ptr<tsl::Gui> loadInitialGui() override { return initially<GroupsGui>(); }
};
}
int main(int argc,char** argv) { return tsl::loop<GameLexOverlay>(argc,argv); }
