#include "gamelex/reading.hpp"
#include <cstdlib>
#include <iostream>
using namespace gamelex::reading;
static int checks=0;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr<<"FAIL "<<__LINE__<<": " #x "\n"; std::exit(1); } } while(0)
int main() {
    CaptureGate gate;
    CHECK(!gate.ready()); gate.begin("this-scan");
    CHECK(!gate.ready()); gate.drawn(); gate.drawn(); CHECK(!gate.ready());
    gate.drawn(); CHECK(gate.ready()); gate.drawn(); CHECK(gate.frames==3);
    CHECK(clean_capture(false,gate.nonce,"this-scan",100,250));
    CHECK(!clean_capture(true,gate.nonce,"this-scan",100,250));
    CHECK(!clean_capture(false,"older-scan","this-scan",100,250));
    CHECK(!clean_capture(false,"","",100,250));
    CHECK(!clean_capture(false,gate.nonce,"this-scan",251,250));
    CHECK(!clean_capture(false,gate.nonce,"this-scan",100,5101));
    gate.finish(); CHECK(!gate.active && !gate.ready() && gate.nonce.empty());
    gate.begin("new-scan"); CHECK(!gate.ready()); // Must clear both buffers again.

    // Regression from the user's game: three visual rows are one dialogue,
    // with a separate game label above. Do not treat line wraps as paragraphs.
    auto p=paragraphs({{"PHONE",.18,.38,.05,.02},
        {"Points to a cable car station somewhere in",.39,.70,.48,.025},
        {"Downtown Silver Pines. I'm looking into it.",.39,.73,.49,.025},
        {"Hopefully it leads us closer to Eddie.",.39,.76,.43,.025}});
    CHECK(p.size()==2);
    CHECK(p[0]=="PHONE");
    CHECK(p[1]=="Points to a cable car station somewhere in Downtown Silver Pines. I'm looking into it. Hopefully it leads us closer to Eddie.");
    CHECK(document(p).find("in Downtown")!=std::string::npos);
    CHECK(document(p).find("PHONE\n\nPoints")==0);
    // Two columns at the same height stay separate; interleaved service order
    // must not mingle a quest label with a dialogue continuation.
    p=paragraphs({{"Left one",.05,.1,.2,.03},{"Right one",.6,.1,.25,.03},
        {"Right two",.6,.14,.25,.03},{"Left two",.05,.14,.2,.03}});
    CHECK(p.size()==2); CHECK(p[0]=="Left one Left two"); CHECK(p[1]=="Right one Right two");
    CHECK(paragraphs({{"one",0,0,.2,.02},{"two",0,.3,.2,.02}}).size()==2);
    CHECK(paragraphs({{"one",0,0,.2,.02},{"TITLE",0,.025,.2,.06}}).size()==2);
    CHECK(paragraphs({{"A",0,0,0,0},{"I",0,0,0,0}}).size()==2);
    p=paragraphs({{"well-",0,.1,.2,.02},{"known",0,.125,.2,.02}}); CHECK(p[0]=="well-known");
    // No blacklist: a game may legitimately contain single letters or versions.
    CHECK(paragraphs({{"0.2.4",0,0,.2,.02}})[0]=="0.2.4");

    const std::string text="Give up? Give UP!\n\nI’m well-known. Don't go.";
    auto w=words(text);
    CHECK(w.size()==8); CHECK(w[0].text=="Give" && w[2].text=="Give");
    CHECK(w[4].text=="I’m" && w[5].text=="well-known" && w[6].text=="Don't");
    for (const auto& word:w) CHECK(text.substr(word.begin,word.end-word.begin)==word.text);
    CHECK(may_extend(text,w[0],w[1])); CHECK(!may_extend(text,w[1],w[2]));
    CHECK(!may_extend(text,w[3],w[4])); CHECK(words("0.2.4 123 !").empty());
    auto measure=[](const std::string& s) { int n=0; for (unsigned char c:s) if ((c&0xc0)!=0x80) ++n; return n*10; };
    const auto rows=layout(text,100,measure);
    const auto positions=marks(rows,w,measure);
    for (const auto& row:rows) {
        CHECK(measure(row.text)<=100);
        CHECK(text.substr(row.begin,row.end-row.begin)==row.text);
    }
    for (std::size_t i=0;i<w.size();++i) {
        bool found=false; for (const auto& m:positions) if (m.word==static_cast<int>(i)) found=true;
        CHECK(found);
    }
    auto chineseMeasure=[](const std::string& s) {
        int n=0;
        for (unsigned char c:s) {
            if (c<128) n+=12; else if ((c&0xc0)!=0x80) n+=23;
        }
        return n;
    };
    const auto chinese=layout("指向银松市中心某处的缆车站。我正在调查。希望这能让我们更接近埃迪。",340,chineseMeasure);
    CHECK(static_cast<int>(chinese.size())<=visible_rows(451)); // The photographed dialogue fits one view.
    CHECK(visible_rows(451)==13);
    CHECK(scroll(0,1,3,13)==0); // Do not scroll/paginate a short document.
    CHECK(scroll(0,50,30,13)==17); CHECK(scroll(0,-1,30,13)==0);
    CHECK(reveal(0,14,30,13)==2); CHECK(reveal(10,5,30,13)==5);
    // Overlong words and multibyte strings still map their fragments exactly.
    const auto split=layout("extraordinary",40,measure);
    const auto splitmarks=marks(split,words("extraordinary"),measure);
    CHECK(split.size()==4 && splitmarks.size()==4);
    CHECK(layout("a\n\nb",100,measure).size()==3);
    CHECK(layout("",100,measure).size()==1);
    std::cout<<checks<<" reading checks passed\n";
}
