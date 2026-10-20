// Execute the actual overlay controller and drawing code with a fake platform
// boundary. Native VI/TLS/input behavior still requires the Switch itself.
#define main gamelex_overlay_entry
#include "../switch/overlay/source/main.cpp"
#undef main
#include <filesystem>
#include <iostream>
#include <unistd.h>
static int checks=0;
#define CHECK(x) do { ++checks;if(!(x)) { std::cerr<<"FAIL overlay flow line "<<__LINE__<<": " #x "\n";std::exit(1); } } while(0)
static void frame(unsigned elapsed=180) {
 fake_ms+=elapsed;tsl::gfx::Renderer::get().text.clear();
 tsl::stack.back()->update();tsl::stack.back()->root->draw(&tsl::gfx::Renderer::get());
}
static tsl::elm::Element* content() { return static_cast<tsl::elm::OverlayFrame*>(tsl::stack.back()->root.get())->content.get(); }
static void press(u64 key) {
 auto* gui=tsl::stack.back().get();
 const bool handled=content()->handleInput(key,key,{}, {},{});
 if(!handled) {
  if(!gui->handleInput(key,key,{}, {},{}) && (key&HidNpadButton_B)) tsl::goBack();
 }
}
static void reply(const Json& data,bool ok=true) {
 const auto& call=gamelex::probe::calls.back();
 CHECK(atomic_json(response_path("overlay"),{{"id",call.id},{"ok",ok},{"code",ok?"":"fixture_error"},{"message",ok?"":"测试失败"},{"data",data}}).ok());
}
static bool visible(const std::string& word) {
 for(const auto& text:tsl::gfx::Renderer::get().text) if(text.find(word)!=std::string::npos) return true;
 return false;
}
static void finish_scan(const Json& groups,bool held_finger=false) {
 for(int i=0;i<5;++i) frame();
 CHECK(gamelex::probe::calls.back().command=="scan");
 CHECK(gamelexCaptureBlanking);
 reply({{"state","captured"}});frame();CHECK(!gamelexCaptureBlanking);
 ult::stillTouching=held_finger;
 reply({{"state","complete"},{"groups",groups}});frame();
 if(held_finger) {
  CHECK(dynamic_cast<RequestGui*>(tsl::stack.back().get()));
  frame();CHECK(dynamic_cast<RequestGui*>(tsl::stack.back().get()));
  ult::stillTouching=false;frame();
 }
 CHECK(dynamic_cast<GroupsGui*>(tsl::stack.back().get()));
}
int main() {
 char temp[]="/tmp/gamelex-overlay-flow-XXXXXX";CHECK(mkdtemp(temp));gamelex::probe::folder=temp;
 GameLexOverlay overlay;overlay.initServices();overlay.onShow();tsl::install(overlay.loadInitialGui());
 auto* root=tsl::stack.back().get();CHECK(dynamic_cast<GroupsGui*>(root));
 const Json groups=Json::array({{{"text","TITLE"},{"x",.1},{"y",.1},{"w",.2},{"h",.025},{"lines",1}},
  {{"text","First paragraph has enough English words to be selected as the main reading area."},{"x",.1},{"y",.2},{"w",.4},{"h",.08},{"lines",3}},
  {{"text","Second paragraph continues the same note."},{"x",.1},{"y",.4},{"w",.4},{"h",.06},{"lines",2}},
  {{"text","Right menu label"},{"x",.7},{"y",.2},{"w",.2},{"h",.025},{"lines",1}}});
 finish_scan(groups);CHECK(tsl::stack.size()==1);
 CHECK(gamelex::probe::calls.size()==1);CHECK(visible("选中 2"));
 for(int i=0;i<5;++i) frame();CHECK(gamelex::probe::calls.size()==1); // No eager translation.
 press(HidNpadButton_A);frame();CHECK(dynamic_cast<ResultGui*>(tsl::stack.back().get()));
 CHECK(gamelex::probe::calls.size()==2);
 CHECK(gamelex::probe::calls.back().command=="translate_all");
 CHECK(gamelex::probe::calls.back().extra["lines"].size()==1);
 CHECK(gamelex::probe::calls.back().extra["lines"][0]==groups[1]["text"]);
 reply({{"translations",{"第一段的中文译文"}}});frame();CHECK(visible("第一段的中文译文"));
 press(HidNpadButton_B);frame();CHECK(tsl::stack.back().get()==root && visible("选中 2"));
 const auto after_translation=gamelex::probe::calls.size();
 press(HidNpadButton_A);frame();CHECK(visible("第一段的中文译文"));
 CHECK(gamelex::probe::calls.size()==after_translation); // Cached reopen costs nothing.
 press(HidNpadButton_Y);frame();CHECK(visible("First paragraph"));
 press(HidNpadButton_A);frame();CHECK(gamelex::probe::calls.back().command=="translate");
 CHECK(gamelex::probe::calls.back().extra["text"]=="First");
 reply({{"translation","第一；首先"}});frame();CHECK(dynamic_cast<ReaderGui*>(tsl::stack.back().get()));
 CHECK(visible("第一；首先"));press(HidNpadButton_B);frame();CHECK(visible("First paragraph"));
 press(HidNpadButton_B);frame();CHECK(tsl::stack.back().get()==root && visible("选中 2"));
 // Touch the same content and footer regions used by the renderer. A drag
 // must not launch translation; a cached card tap must not bill again.
 const auto before_touch=gamelex::probe::calls.size();
 auto touch=[](tsl::elm::TouchEvent event,int x,int y) { return content()->onTouch(event,x,y,0,0,0,0); };
 CHECK(touch(tsl::elm::TouchEvent::Touch,60,210));
 CHECK(touch(tsl::elm::TouchEvent::Hold,60,150));
 CHECK(touch(tsl::elm::TouchEvent::Release,60,150));frame();
 CHECK(gamelex::probe::calls.size()==before_touch && tsl::stack.back().get()==root);
 CHECK(touch(tsl::elm::TouchEvent::Touch,300,580));
 CHECK(touch(tsl::elm::TouchEvent::Release,300,580));frame();CHECK(visible("选中 2–3"));
 CHECK(touch(tsl::elm::TouchEvent::Touch,60,580));
 CHECK(touch(tsl::elm::TouchEvent::Release,60,580));frame();CHECK(visible("选中 2"));
 CHECK(touch(tsl::elm::TouchEvent::Touch,60,260));
 CHECK(touch(tsl::elm::TouchEvent::Release,60,260));frame();
 CHECK(dynamic_cast<ResultGui*>(tsl::stack.back().get()));
 CHECK(gamelex::probe::calls.size()==before_touch && visible("第一段的中文译文"));
 press(HidNpadButton_B);frame();CHECK(tsl::stack.back().get()==root);
 press(HidNpadButton_R);frame();CHECK(visible("选中 2–3"));
 press(HidNpadButton_A);frame();CHECK(gamelex::probe::calls.back().extra["lines"].size()==2);
 const auto old_id=gamelex::probe::calls.back().id;
 press(HidNpadButton_B);frame();CHECK(gamelex::probe::calls.back().command=="cancel");
 CHECK(tsl::stack.back().get()==root && visible("选中 2–3"));
 press(HidNpadButton_A);frame();const auto active_id=gamelex::probe::calls.back().id;
 CHECK(active_id!=old_id);
 CHECK(atomic_json(response_path("overlay"),{{"id",old_id},{"ok",true},{"data",{{"translations",{"过期译文"}}}}}).ok());
 frame();CHECK(!visible("过期译文"));
 reply({{"translations",{"两段合并译文"}}});frame();CHECK(visible("两段合并译文"));
 press(HidNpadButton_B);frame();CHECK(visible("选中 2–3"));
 // Cancelled / failed refresh retains the previous document and selection.
 press(HidNpadButton_Plus);for(int i=0;i<5;++i) frame();
 press(HidNpadButton_B);frame();CHECK(visible("选中 2–3"));
 CHECK(gamelex::probe::calls.back().command=="cancel");
 press(HidNpadButton_A);frame();CHECK(visible("两段合并译文"));
 press(HidNpadButton_B);frame();
 press(HidNpadButton_Plus);for(int i=0;i<5;++i) frame();reply({},false);frame();
 CHECK(visible("识别失败"));press(HidNpadButton_B);frame();CHECK(visible("选中 2–3"));
 press(HidNpadButton_Plus);
 finish_scan(Json::array({{{"text","New screenshot only."},{"x",.1},{"y",.2},{"w",.2},{"h",.03},{"lines",1}}}),true);
 CHECK(visible("New screenshot only."));CHECK(!visible("TITLE"));CHECK(visible("选中 1"));
 press(HidNpadButton_A);frame();CHECK(gamelex::probe::calls.back().extra["lines"][0]=="New screenshot only.");
 reply({{"translations",{"新画面译文"}}});frame();press(HidNpadButton_B);frame();
 press(HidNpadButton_B);CHECK(tsl::closed && next_args=="ovlmenu.ovl");
 tsl::stack.clear();overlay.exitServices();std::filesystem::remove_all(temp);
 std::cout<<checks<<" actual overlay workflow checks passed\n";
}
