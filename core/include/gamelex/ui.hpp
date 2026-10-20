#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <utility>
#include <set>
#include <map>
#include <memory>

namespace gamelex::ui {
// Logical layout units. The renderer draws at twice this resolution, including
// font rasterization; physical touch coordinates map back to these units.
constexpr int CanvasWidth = 640, CanvasHeight = 360;
constexpr int RenderScale = 2;
constexpr int TouchWidth = 1280, TouchHeight = 720;
// Scale geometry AND font size before rasterization. Scaling a low-resolution
// bitmap afterwards would blur text and is deliberately not done here.
template<class NativeRenderer> class RasterCanvas {
public:
    explicit RasterCanvas(NativeRenderer* renderer) : raw(renderer) {}
    void clearScreen() { raw->clearScreen(); }
    template<class Color> void drawRect(int x,int y,int w,int h,Color c) {
        raw->drawRect(x*RenderScale,y*RenderScale,w*RenderScale,h*RenderScale,c);
    }
    template<class Color> void drawRoundedRect(int x,int y,int w,int h,int radius,Color c) {
        raw->drawRoundedRect(x*RenderScale,y*RenderScale,w*RenderScale,h*RenderScale,radius*RenderScale,c);
    }
    template<class Color> void drawString(const std::string& value,bool mono,int x,int y,int size,Color c) {
        raw->drawString(value,mono,x*RenderScale,y*RenderScale,size*RenderScale,c);
    }
    std::pair<int,int> getTextDimensions(const std::string& value,bool mono,int size) {
        const auto d=raw->getTextDimensions(value,mono,size*RenderScale);
        return {(d.first+RenderScale-1)/RenderScale,(d.second+RenderScale-1)/RenderScale};
    }
private:
    NativeRenderer* raw;
};
struct Box { double x = 0, y = 0, w = 0, h = 0; };
struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool contains(int px, int py, int pad = 0) const {
        return w > 0 && h > 0 && px >= x - pad && py >= y - pad &&
            px < x + w + pad && py < y + h + pad;
    }
};
inline Rect project(Box box, int width, int height) {
    if (width <= 0 || height <= 0 || !std::isfinite(box.x) || !std::isfinite(box.y) ||
        !std::isfinite(box.w) || !std::isfinite(box.h) || box.w <= 0 || box.h <= 0) return {};
    const double right = std::clamp(box.x + box.w, 0.0, 1.0);
    const double bottom = std::clamp(box.y + box.h, 0.0, 1.0);
    const int x = static_cast<int>(std::floor(std::clamp(box.x, 0.0, 1.0) * width));
    const int y = static_cast<int>(std::floor(std::clamp(box.y, 0.0, 1.0) * height));
    return {x, y, std::max(0, static_cast<int>(std::ceil(right * width)) - x),
        std::max(0, static_cast<int>(std::ceil(bottom * height)) - y)};
}
inline int touch_x(int x) { return x * CanvasWidth / TouchWidth; }
inline int touch_y(int y) { return y * CanvasHeight / TouchHeight; }
inline int hit_test(const std::vector<Box>& boxes, int x, int y, int width, int height) {
    int best = -1;
    double bestScore = 1e30;
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        const auto r = project(boxes[i], width, height);
        if (!r.contains(x, y, 3)) continue;
        const double dx = x - (r.x + r.w / 2.0), dy = y - (r.y + r.h / 2.0);
        // Prefer a box actually under the finger, then its nearest centre.
        const double score = (r.contains(x, y) ? 0 : 1e12) + dx * dx + dy * dy;
        if (score < bestScore) { bestScore = score; best = static_cast<int>(i); }
    }
    return best;
}
template<class Measure>
std::vector<std::string> wrap(const std::string& text, int width, Measure measure) {
    std::vector<std::string> rows;
    std::string row;
    auto split = [&] {
        auto space = row.find_last_of(' ');
        if (space != std::string::npos && space > 0) {
            rows.push_back(row.substr(0, space)); row.erase(0, space + 1);
        } else { rows.push_back(row); row.clear(); }
    };
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '\r') { ++i; continue; }
        if (text[i] == '\n') { rows.push_back(row); row.clear(); ++i; continue; }
        const unsigned char c = text[i];
        std::size_t n = c < 0x80 ? 1 : (c < 0xE0 ? 2 : (c < 0xF0 ? 3 : 4));
        if (i + n > text.size()) n = 1;
        const auto ch = text.substr(i, n);
        while (!row.empty() && measure(row + ch) > width) split();
        if (ch != " " || !row.empty()) row += ch;
        i += n;
    }
    if (!row.empty()) rows.push_back(row);
    return rows;
}
inline std::string translation_document(const std::vector<std::string>& translations) {
    std::string text;
    for (const auto& item : translations) {
        if (item.empty()) continue;
        if (!text.empty()) text += '\n';
        text += item;
    }
    return text;
}
// A text list must not reuse screenshot coordinates. Build selectable English
// words from the original lines; retain source lines separately for reading.
inline std::vector<std::string> lookup_words(const std::vector<std::string>& lines) {
    std::vector<std::string> result;
    std::set<std::string> seen;
    for (const auto& original : lines) {
        std::string line=original;
        std::size_t pos=0;
        while ((pos=line.find("’",pos))!=std::string::npos) { line.replace(pos,3,"'"); ++pos; }
        std::string word;
        auto flush=[&] {
            while (!word.empty() && (word.back()=='\'' || word.back()=='-')) word.pop_back();
            bool letters=false;
            std::string key=word;
            for (auto& c:key) {
                if (c>='A' && c<='Z') c+=('a'-'A');
                if (c>='a' && c<='z') letters=true;
            }
            if (letters && word.size()<=64 && seen.insert(key).second && result.size()<400) result.push_back(word);
            word.clear();
        };
        for (unsigned char c:line) {
            if ((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') ||
                ((c=='\'' || c=='-') && !word.empty())) word+=static_cast<char>(c);
            else flush();
        }
        flush();
        if (result.size()>=400) break;
    }
    return result;
}
struct LookupSession {
    std::vector<std::string> lines,words;
    std::map<std::string,std::string> cache;
    std::shared_ptr<std::map<std::string,std::string>> shared_cache;
    std::string translation,translation_error;
    bool demo=false;
    std::map<std::string,std::string>& word_cache() { return shared_cache ? *shared_cache : cache; }
};
inline void remember_lookup(LookupSession& session,const std::string& word,const std::string& value) {
    if (word.size()+value.size()>8192) return;
    auto& cache=session.word_cache();cache.erase(word);
    std::size_t bytes=word.size()+value.size();
    for (const auto& item:cache) bytes+=item.first.size()+item.second.size();
    while (!cache.empty() && (cache.size()>=64 || bytes>16384)) {
        bytes-=cache.begin()->first.size()+cache.begin()->second.size();cache.erase(cache.begin());
    }
    cache[word]=value;
}
inline LookupSession demo_session() {
    LookupSession result;
    result.demo=true;
    result.lines={"Find the silver key.","Return to the castle."};
    result.words=lookup_words(result.lines);
    result.translation="找到银色钥匙。\n返回城堡。\n这是界面自检的示例译文，没有上传截图或调用网络服务。";
    result.cache={{"Find","找到；寻找"},{"the","定冠词"},{"silver","银；银色的"},{"key","钥匙；关键"},
        {"Return","返回"},{"to","向；到"},{"castle","城堡"}};
    return result;
}
inline int page_count(int rows, int perPage) { return std::max(1, (rows + perPage - 1) / perPage); }
inline int clamp_page(int page, int rows, int perPage) {
    return std::clamp(page, 0, page_count(rows, perPage) - 1);
}
// A mailbox can contain late or duplicate replies; only the current request
// may update the screen, and a completed response is consumed once.
struct RequestTracker {
    std::string id, key;
    void reset() { id.clear(); key.clear(); }
    bool accepts(const std::string& value) const { return !id.empty() && value == id; }
};
}
