#include "gamelex/group_reader.hpp"
#include "gamelex/core.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace gamelex;
using namespace gamelex::reading;
using namespace gamelex::ui;
static int checks=0;
#define CHECK(x) do { ++checks;if (!(x)) { std::cerr<<"FAIL group line "<<__LINE__<<": " #x "\n";std::exit(1); } } while(0)
int main(int argc,char** argv) {
    if (argc==3 && std::string(argv[1])=="--fixture") {
        Json json;CHECK(read_json(argv[2],json).ok());std::vector<Region> regions;
        if (json.contains("response")) { Json response=json["response"];json=std::move(response); }
        for (const auto& line:json["words_result"]) {
            const auto& l=line["location"];
            regions.push_back({line["words"].get<std::string>(),l["left"].get<double>()/1280,
                l["top"].get<double>()/720,l["width"].get<double>()/1280,l["height"].get<double>()/720});
        }
        GroupBook book;book.replace(groups(std::move(regions)));
        for (std::size_t i=0;i<book.groups.size();++i) {
            const auto& g=book.groups[i];
            std::cout<<(i==book.selected?"* ":"  ")<<i+1<<" lines="<<g.lines<<" x="<<g.x<<" y="<<g.y<<" "<<g.text<<"\n";
        }
        return 0;
    }
    // Same-baseline fragments arrive in reverse order, then an indented row.
    auto result=groups({{"Heading",.1,.08,.2,.05},{"second half",.21,.2,.14,.03},
        {"first half",.1,.202,.1,.03},{"a continuation",.12,.245,.25,.03},
        {"Right menu one",.67,.21,.22,.022},{"Right menu two",.67,.265,.22,.022}});
    CHECK(result.size()==4);CHECK(result[0].text=="Heading");
    CHECK(result[1].text=="first half second half a continuation" && result[1].lines==2);
    CHECK(result[1].located && result[1].x==.1);CHECK(result[2].text=="Right menu one");
    CHECK(result[3].text=="Right menu two");
    // The actual game's menu rows are 35–38 px apart with 13–19 px glyphs;
    // prose rows are 25 px apart. A loose vertical rule joins the whole menu.
    const auto menu=groups({{"First item",858.0/1280,112.0/720,175.0/1280,17.0/720},
        {"Second item",858.0/1280,147.0/720,71.0/1280,19.0/720},
        {"Third item",858.0/1280,185.0/720,149.0/1280,17.0/720}});
    CHECK(menu.size()==3);
    auto pixel=[](const std::string& text,int x,int y,int w,int h) {
        return Region{text,x/1280.0,y/720.0,w/1280.0,h/720.0};
    };
    const auto note=groups({pixel("NOTE TITLE",366,131,157,18),
        pixel("First paragraph begins",220,217,408,18),pixel("and continues here",220,242,432,17),
        pixel("with a shorter last line.",219,268,325,13),
        pixel("Second paragraph begins",220,316,441,18),pixel("and has another line",220,341,425,17),
        pixel("plus a third long line",219,365,427,17),pixel("and an end.",221,391,110,13),
        pixel("Author",218,439,108,18),pixel("Other menu",858,223,162,17)});
    CHECK(note.size()==5);CHECK(note[1].lines==3 && note[2].lines==4);
    CHECK(note[2].text.find("Second paragraph")==0 && note[3].text=="Author");
    CHECK(note[4].text=="Other menu");
    // Separate paragraphs remain selectable. Reading order finishes the left
    // column first, so R can join paragraphs without importing a right menu.
    result=groups({{"First left paragraph",.1,.2,.35,.025},{"continuation",.1,.235,.3,.025},
        {"Second left paragraph",.1,.35,.35,.025},{"continuation again",.1,.385,.3,.025},
        {"Menu row A",.68,.25,.2,.022},{"Menu row B",.68,.305,.2,.022}});
    CHECK(result.size()==4);CHECK(result[0].lines==2 && result[1].lines==2);
    CHECK(result[1].text.find("Second left")==0);CHECK(result[2].text=="Menu row A");
    // Short last rows and first-line indents do not fragment normal prose.
    CHECK(groups({{"Indented first",.13,.2,.24,.025},{"Full width next",.1,.235,.34,.025},
        {"end.",.1,.27,.04,.025}}).size()==1);
    CHECK(groups({{"small",.1,.1,.2,.02},{"BIG TITLE",.1,.13,.2,.05}}).size()==2);
    CHECK(groups({{"left",.05,.1,.2,.03},{"right",.6,.1,.2,.03}}).size()==2);
    CHECK(groups({{"first",.1,.1,.2,.02},{"gap",.1,.4,.2,.02}}).size()==2);
    auto invalid=groups({{"A",0,0,0,0},{"I",0,0,0,0},{"0.2.4",0,0,0,0}});
    CHECK(invalid.size()==3 && invalid[0].text=="A" && invalid[2].text=="0.2.4");
    CHECK(!invalid[0].located);
    CHECK(!positioned({"invalid",0,std::numeric_limits<double>::quiet_NaN(),.1,.1}));
    CHECK(groups({{"",0,0,.2,.02}}).empty());
    GroupBook book;
    CHECK(book.take_initial_scan());CHECK(!book.take_initial_scan());
    book.replace(result);CHECK(book.revision==1);
    CHECK(!book.cached(0,1));book.choose(0);
    auto first=book.open();CHECK(first && first->translation.empty());
    CHECK(first->lines.size()==1 && first->lines[0]==result[0].text);
    first->translation="第一段译文";remember_lookup(*first,"first","第一");
    book.move(1);CHECK(book.selected==1 && book.extent==1);
    auto second=book.open();CHECK(second!=first && second->translation.empty());
    CHECK(second->word_cache()["first"]=="第一");
    book.choose(0);CHECK(book.open()==first);CHECK(book.open()->word_cache()["first"]=="第一");
    book.expand();CHECK(book.extent==2);
    auto combined=book.open();CHECK(combined!=first && combined->lines.size()==2);
    CHECK(combined->lines[1]==result[1].text);
    combined->translation="合并译文";
    book.shrink();CHECK(book.extent==1 && book.open()==first);
    book.expand();CHECK(book.open()==combined && combined->translation=="合并译文");
    book.choose(3);book.expand();CHECK(book.extent==1);
    book.move(100);CHECK(book.selected==3);book.move(-100);CHECK(book.selected==0);
    // A new screenshot invalidates every old translation, including identical
    // index ranges. Cancelled refreshes never call replace in the UI.
    book.replace(result);book.choose(0);CHECK(book.revision==2 && !book.cached(0,1));
    CHECK(book.open()!=first && book.open()->translation.empty());
    CHECK(book.open()->word_cache().empty());
    for (int i=0;i<100;++i) remember_lookup(*book.open(),std::to_string(i),std::string(500,'x'));
    std::size_t word_bytes=0;
    for (const auto& item:book.open()->word_cache()) word_bytes+=item.first.size()+item.second.size();
    CHECK(word_bytes<=16384 && book.open()->word_cache().size()<=64);
    auto huge=result;huge[0].text=std::string(4900,'a');huge[1].text=std::string(200,'b');
    book.replace(huge);book.choose(0);book.expand();CHECK(book.extent==1);
    auto many=std::vector<Group>(20,result[0]);book.replace(many);
    for(int i=0;i<30;++i) book.expand();CHECK(book.extent==8);
    // Scroll, selection reveal and touch all use the same card geometry.
    CardViewport view;view.set({112,141,83,141},250);
    CHECK(view.offset==0 && view.total==477);
    CHECK(view.hit(0)==0 && view.hit(103)==0 && view.hit(108)==-1);
    CHECK(view.hit(112)==1 && view.hit(244)==1 && view.hit(249)==-1);
    CHECK(view.hit(-1)==-1 && view.hit(250)==-1);
    view.reveal(2);CHECK(view.offset==86 && view.hit(167)==2);
    view.reveal(0);CHECK(view.offset==0);
    view.scroll(10000);CHECK(view.offset==227);view.scroll(-10000);CHECK(view.offset==0);
    view.set({100},250);view.reveal(0);CHECK(view.offset==0);CHECK(view.hit(120)==-1);
    std::cout<<checks<<" grouping and selected-reader checks passed\n";
}
