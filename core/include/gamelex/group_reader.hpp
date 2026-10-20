#pragma once
#include "gamelex/reading.hpp"
#include "gamelex/ui.hpp"
#include <memory>

namespace gamelex::ui {
// One screenshot owns its selection and completed translations. Merely
// browsing groups never constructs a translation request.
struct GroupBook {
    using SessionPtr=std::shared_ptr<LookupSession>;
    std::vector<reading::Group> groups;
    std::size_t selected=0,extent=1;
    unsigned revision=0;
    bool initial=true;
    bool take_initial_scan() { const bool value=initial;initial=false;return value; }
    void replace(std::vector<reading::Group> next) {
        groups=std::move(next);selected=0;extent=1;cache.clear();words->clear();initial=false;++revision;
        // Prefer the first substantial paragraph in reading order, with all
        // other groups still available. Do not auto-translate or discard labels.
        for (std::size_t i=0;i<groups.size();++i)
            if (groups[i].lines>=2 && groups[i].text.size()>=60) { selected=i;break; }
    }
    void choose(std::size_t index) { if (index<groups.size()) { selected=index;extent=1; } }
    void move(int delta) {
        if (!groups.empty()) choose(static_cast<std::size_t>(std::clamp(static_cast<int>(selected)+delta,0,static_cast<int>(groups.size())-1)));
    }
    void expand() {
        if (selected+extent>=groups.size() || extent>=8) return;
        std::size_t bytes=0;
        for (std::size_t i=selected;i<=selected+extent;++i) bytes+=groups[i].text.size()+2;
        if (bytes<=5000) ++extent;
    }
    void shrink() { if (extent>1) --extent; }
    SessionPtr cached(std::size_t first,std::size_t count) const {
        for (const auto& entry:cache) if (entry.first==first && entry.count==count) return entry.session;
        return {};
    }
    SessionPtr open() {
        if (groups.empty() || selected>=groups.size()) return {};
        if (const auto hit=cached(selected,extent)) return hit;
        auto session=std::make_shared<LookupSession>();
        session->shared_cache=words;
        for (std::size_t i=selected;i<std::min(groups.size(),selected+extent);++i) session->lines.push_back(groups[i].text);
        // Bound retained documents; an active reader holds its own shared_ptr.
        if (cache.size()>=8) cache.erase(cache.begin());
        cache.push_back({selected,extent,session});
        return session;
    }
private:
    struct Entry { std::size_t first,count;SessionPtr session; };
    std::vector<Entry> cache;
    std::shared_ptr<std::map<std::string,std::string>> words=std::make_shared<std::map<std::string,std::string>>();
};
// Pixel-based card scrolling shared by controller navigation and touch. Each
// card's English preview is truncated, while its reader always owns full text.
struct CardViewport {
    std::vector<int> tops,heights;
    int offset=0,total=0,height=1;
    void set(std::vector<int> sizes,int available) {
        heights=std::move(sizes);tops.clear();total=0;height=std::max(1,available);
        for (const auto size:heights) { tops.push_back(total);total+=size; }
        clamp();
    }
    void clamp() { offset=std::clamp(offset,0,std::max(0,total-height)); }
    void reveal(std::size_t index) {
        if (index>=tops.size()) return;
        if (tops[index]<offset) offset=tops[index];
        else if (tops[index]+heights[index]>offset+height) offset=tops[index]+heights[index]-height;
        clamp();
    }
    void scroll(int pixels) { offset+=pixels;clamp(); }
    int hit(int y) const {
        if (y<0 || y>=height) return -1;
        for (std::size_t i=0;i<tops.size();++i)
            if (y+offset>=tops[i] && y+offset<tops[i]+heights[i]-8) return static_cast<int>(i);
        return -1;
    }
};
}
