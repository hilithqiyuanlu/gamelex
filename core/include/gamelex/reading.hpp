#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace gamelex::reading {
// The blank frames are submitted using the existing double-buffered renderer.
// A request is published only in the update AFTER all three frames were drawn.
struct CaptureGate {
    bool active=false;
    unsigned frames=0;
    std::string nonce;
    void begin(std::string value) { active=true; frames=0; nonce=std::move(value); }
    void drawn() { if (active && frames<3) ++frames; }
    bool ready() const { return active && frames>=3 && !nonce.empty(); }
    void finish() { active=false; frames=0; nonce.clear(); }
};
inline bool clean_capture(bool visible,const std::string& actual,const std::string& expected,
                          std::uint64_t tick,std::uint64_t now) {
    return !visible && !expected.empty() && actual==expected && tick<=now && now-tick<=5000;
}

struct Region { std::string text; double x=0,y=0,w=0,h=0; };
struct Group : Region { unsigned lines=1; bool located=false; };
inline bool positioned(const Region& r) {
    return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.w) && std::isfinite(r.h) &&
        r.x>=0 && r.y>=0 && r.x<1 && r.y<1 && r.w>0 && r.h>0 && r.x+r.w<=1.02 && r.y+r.h<=1.02;
}
inline void append_text(std::string& first,const std::string& next) {
    if (!first.empty() && first.back()!='-') first+=' ';
    first+=next;
}
inline void encompass(Region& a,const Region& b) {
    const auto right=std::max(a.x+a.w,b.x+b.w),bottom=std::max(a.y+a.h,b.y+b.h);
    a.x=std::min(a.x,b.x); a.y=std::min(a.y,b.y); a.w=right-a.x; a.h=bottom-a.y;
}
// First join nearby fragments on the same baseline, then join aligned rows.
// Coordinates are normalized; aspect converts vertical glyph size to x units.
inline std::vector<Group> groups(std::vector<Region> input,double aspect=720.0/1280) {
    if (!std::isfinite(aspect) || aspect<=0) aspect=720.0/1280;
    std::vector<Region> rows,unknown;
    std::stable_sort(input.begin(),input.end(),[](const auto& a,const auto& b) {
        return positioned(a) && positioned(b) ? (a.y==b.y ? a.x<b.x : a.y<b.y) : positioned(a)>positioned(b);
    });
    for (auto& r:input) {
        if (r.text.empty()) continue;
        if (!positioned(r)) { unknown.push_back(std::move(r)); continue; }
        int best=-1; double score=1e9;
        for (std::size_t i=0;i<rows.size();++i) {
            const auto& p=rows[i];
            const double height=std::max(p.h,r.h);
            const double vertical=std::min(p.y+p.h,r.y+r.h)-std::max(p.y,r.y);
            const double gap=std::max(p.x,r.x)-std::min(p.x+p.w,r.x+r.w);
            if (std::min(p.h,r.h)<height*.65 || vertical<std::min(p.h,r.h)*.65 ||
                gap<0 || gap>height*aspect*1.25) continue;
            const auto distance=gap+std::abs(p.y-r.y);
            if (distance<score) { best=static_cast<int>(i);score=distance; }
        }
        if (best<0) rows.push_back(std::move(r));
        else {
            auto& row=rows[best];
            if (r.x<row.x) { append_text(r.text,row.text);row.text=std::move(r.text); }
            else append_text(row.text,r.text);
            encompass(row,r);
        }
    }
    std::stable_sort(rows.begin(),rows.end(),[](const auto& a,const auto& b) { return a.y==b.y ? a.x<b.x : a.y<b.y; });
    struct Block { Group group; Region last; };
    std::vector<Block> blocks;
    for (const auto& row:rows) {
        int best=-1;double score=1e9;
        for (std::size_t i=0;i<blocks.size();++i) {
            const auto& p=blocks[i].last;
            const double height=std::max(p.h,row.h),gap=row.y-(p.y+p.h);
            const double overlap=std::min(p.x+p.w,row.x+row.w)-std::max(p.x,row.x);
            const double indent=std::max(.012,height*aspect*1.8);
            if (row.y<p.y+p.h*.5 || gap>height*.85 || gap< -height*.3 ||
                std::min(p.h,row.h)<height*.65 ||
                (overlap<std::min(p.w,row.w)*.5 && std::abs(p.x-row.x)>indent)) continue;
            const double candidate=std::abs(gap)/height+std::abs(p.x-row.x);
            if (candidate<score) { best=static_cast<int>(i);score=candidate; }
        }
        if (best<0) { Group g;static_cast<Region&>(g)=row;g.located=true;blocks.push_back({std::move(g),row}); }
        else { auto& b=blocks[best];append_text(b.group.text,row.text);encompass(b.group,row);++b.group.lines;b.last=row; }
    }
    // Keep each column contiguous so extending a paragraph does not select a
    // menu row from the opposite side of the screen between two paragraphs.
    struct Column { Region anchor; std::vector<Group> items; };
    std::vector<Column> columns;
    std::stable_sort(blocks.begin(),blocks.end(),[](const auto& a,const auto& b) { return a.group.w>b.group.w; });
    for (auto& block:blocks) {
        auto& g=block.group;int best=-1;double score=1e9;
        for (std::size_t i=0;i<columns.size();++i) {
            const auto& c=columns[i].anchor;
            if ((c.w>.55)!=(g.w>.55)) continue; // A spanning heading must not bridge columns.
            const auto overlap=std::min(c.x+c.w,g.x+g.w)-std::max(c.x,g.x);
            const auto centres=std::abs(c.x+c.w/2-g.x-g.w/2);
            if (overlap<std::min(c.w,g.w)*.65 || (std::abs(c.x-g.x)>.06 && centres>.15)) continue;
            if (centres<score) { best=static_cast<int>(i);score=centres; }
        }
        if (best<0) columns.push_back({static_cast<const Region&>(g),{std::move(g)}});
        else columns[best].items.push_back(std::move(g));
    }
    std::stable_sort(columns.begin(),columns.end(),[](const auto& a,const auto& b) { return a.anchor.x<b.anchor.x; });
    std::vector<Group> result;
    for (auto& c:columns) {
        std::stable_sort(c.items.begin(),c.items.end(),[](const auto& a,const auto& b) { return a.y==b.y ? a.x<b.x : a.y<b.y; });
        for (auto& g:c.items) result.push_back(std::move(g));
    }
    for (auto& row:unknown) { Group g;static_cast<Region&>(g)=std::move(row);result.push_back(std::move(g)); }
    return result;
}
inline std::vector<std::string> paragraphs(std::vector<Region> lines) {
    std::vector<std::string> result;
    for (auto& g:groups(std::move(lines))) result.push_back(std::move(g.text));
    return result;
}
inline std::string document(const std::vector<std::string>& paragraphs) {
    std::string result;
    for (const auto& text:paragraphs) if (!text.empty()) {
        if (!result.empty()) result+="\n\n";
        result+=text;
    }
    return result;
}
struct Word { std::size_t begin,end; std::string text; };
inline bool letter(unsigned char c) { return (c>='a' && c<='z') || (c>='A' && c<='Z'); }
inline std::vector<Word> words(const std::string& text) {
    std::vector<Word> result;
    for (std::size_t i=0;i<text.size();) {
        if (!letter(text[i])) { ++i; continue; }
        const auto start=i++;
        while (i<text.size()) {
            if (letter(text[i]) || (text[i]>='0' && text[i]<='9')) { ++i; continue; }
            std::size_t join=0;
            if (text[i]=='\'' || text[i]=='-') join=1;
            else if (text.compare(i,3,"’")==0) join=3;
            if (join && i+join<text.size() && letter(text[i+join])) { i+=join; continue; }
            break;
        }
        result.push_back({start,i,text.substr(start,i-start)});
    }
    return result;
}
struct Row { std::size_t begin,end; std::string text; };
inline std::size_t utf8_size(unsigned char c) { return c<0x80 ? 1 : c<0xe0 ? 2 : c<0xf0 ? 3 : 4; }
template<class Measure>
std::vector<Row> layout(const std::string& text,int width,Measure measure) {
    std::vector<Row> result;
    std::size_t start=0,i=0,space=std::string::npos;
    auto emit=[&](std::size_t end) { result.push_back({start,end,text.substr(start,end-start)}); };
    while (i<text.size()) {
        if (text[i]=='\n') { emit(i); start=++i; space=std::string::npos; continue; }
        const auto next=std::min(text.size(),i+utf8_size(text[i]));
        if (i>start && measure(text.substr(start,next-start))>width) {
            if (space!=std::string::npos && space>start) {
                emit(space); start=space+1;
                while (start<text.size() && text[start]==' ') ++start;
                i=start;
            } else { emit(i); start=i; }
            space=std::string::npos; continue;
        }
        if (text[i]==' ') space=i;
        i=next;
    }
    if (start<text.size()) emit(text.size());
    if (result.empty()) result.push_back({0,0,""});
    return result;
}
struct Mark { int word,row,x,w; };
template<class Measure>
std::vector<Mark> marks(const std::vector<Row>& rows,const std::vector<Word>& words,Measure measure) {
    std::vector<Mark> result;
    for (std::size_t r=0;r<rows.size();++r) for (std::size_t i=0;i<words.size();++i) {
        const auto first=std::max(rows[r].begin,words[i].begin),last=std::min(rows[r].end,words[i].end);
        if (first>=last) continue;
        result.push_back({static_cast<int>(i),static_cast<int>(r),
            measure(rows[r].text.substr(0,first-rows[r].begin)),
            measure(rows[r].text.substr(first-rows[r].begin,last-first))});
    }
    return result;
}
inline int visible_rows(int height,int line_height=34) { return std::max(1,height/line_height); }
inline int scroll(int first,int delta,int total,int visible) { return std::clamp(first+delta,0,std::max(0,total-visible)); }
inline int reveal(int first,int row,int total,int visible) {
    if (row<first) first=row;
    if (row>=first+visible) first=row-visible+1;
    return scroll(first,0,total,visible);
}
// Expansion stays inside the same sentence/paragraph. It is a user-selected
// range, not a claim that every adjacent pair is a fixed phrase.
inline bool may_extend(const std::string& text,const Word& a,const Word& b) {
    if (a.end>b.begin) return false;
    return text.substr(a.end,b.begin-a.end).find_first_not_of(" \t")==std::string::npos;
}
}
