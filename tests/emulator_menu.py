"""Execute shared modal/chord and actual NES shutdown callback with host boundaries."""
from pathlib import Path
import subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
s=(ROOT/'src/apps/emulator/systemmenu.cpp').read_text()
body=s[s.index('namespace {'):]
osd=(ROOT/'src/apps/nes/osd.cpp').read_text()
modal=osd[osd.index('void openSystemMenu() {',osd.index('void osd_shutdown')):osd.index('void prepareRuntimeShutdown() {',osd.index('void osd_shutdown'))]
pre=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <string>
#include "apps/nes/preferences.h"
#include "keira/localizations/lang_uk.h"
using String=std::string;
#define pdMS_TO_TICKS(x) (x)
namespace lilka {
struct B {bool pressed=false;};struct State {B a,b,c,d,up,down,left,right,select,start;};
enum class Button {A,B};
namespace colors {constexpr int Black=0,White=1;}
struct Canvas {void fillScreen(int){}};
struct Controller {State state; State getState(){return state;} void resetState(){state=State();}} controller;
std::vector<int> choices; int step=0;
std::vector<Button> buttons; std::vector<String> titles;
struct Menu {
 std::vector<String> items;int cursor=0;
 explicit Menu(const String& title){titles.push_back(title);}
 void addItem(const String& x){items.push_back(x);}
 void addActivationButton(Button){}
 void update(){cursor=choices.at(step++);}
 void draw(Canvas*){
 if(items.size()==4 && items[0]==K_S_EMU_PRECISION){assert(items[1]==K_S_EMU_AXIS_X);assert(items[2]==K_S_EMU_AXIS_Y);assert(items[3]==K_S_EMU_BACK);}
 if(items.size()==3 && items[0]==K_S_EMU_DELAY_MINUS){assert(items[1]==K_S_EMU_DELAY_PLUS);assert(items[2]==K_S_EMU_BACK);}
 if(items.size()==5 && items[0]==K_S_EMU_SCREENSHOT) {assert(items[1]==K_S_EMU_RESET);assert(items[2]==K_S_EMU_CONTROLS);assert(items[3]==K_S_EMU_MORE);assert(items[4]==K_S_EMU_EXIT);}}
 bool isFinished(){return true;} int getCursor(){return cursor;} Button getButton(){return buttons.empty()?Button::A:buttons.at(step-1);}
 void setItem(int,const String&,void*,int,const String&){} void setTitle(const String&){}
};
struct {template<typename...T> void err(T...){}} serial;
}
String StringFormat(const char*,unsigned){return "delay";}
uint32_t now=0,releaseAt=0;bool releaseSelect=true;
uint32_t millis(){return now;}
void vTaskDelay(unsigned n){now+=n;if(uint32_t(now-releaseAt)>=10 && uint32_t(now-releaseAt)<10000){lilka::controller.state=lilka::State();return;}if(int32_t(now-releaseAt)>=0){if(releaseSelect)lilka::controller.state.select.pressed=false;else lilka::controller.state.start.pressed=false;}}
struct EmulatorMenuApp {
 enum class SystemAction {Resume,Exit,Reset,Save,Load,Screenshot};
 nesmenu::Preferences preferences;nesmenu::DirectionFilter directionFilter,verticalFilter;
 String romConfigPath,configFile,configWarning,menuTitle;
 bool automaticFrameskip=true,hasFrameskipSetting=false,screenshotOnNextFrame=false,audioPaused=false;
 lilka::Canvas a,b; lilka::Canvas* canvas=&a; lilka::Canvas* backCanvas=&b;
 void queueDraw(){} void loadPreferences();void savePreferences();void showNotice(const String&);
 void waitForRelease();bool holdExitRequested();SystemAction showSystemMenu();
 void clearGameCanvases(){directionFilter.reset();verticalFilter.reset();}
};
'''
# std::string boundary adaptations only; production logic is unmodified.
body=body.replace('configWarning.length()', '!configWarning.empty()').replace('configFile.length()', '!configFile.empty()')
post=r'''
using NesApp=EmulatorMenuApp;
struct Driver {static NesApp* app;}; NesApp app;NesApp* Driver::app=&::app;
using SemaphoreHandle_t=void*;
void* timer=nullptr;void* xSoundMutex=nullptr;bool soundInitialized=false;
constexpr int portMAX_DELAY=0;namespace esp_i2s {int I2S_NUM_0=0;}
void i2s_zero_dma_buffer(int){}struct Acquire {Acquire(void*){}};
void* xSemaphoreCreateBinary(){return &app;} void xSemaphoreGive(void*){}void xSemaphoreTake(void*,int){}void vSemaphoreDelete(void*){}
int stops=0,resets=0,quits=0,releases=0;
void xTimerStop(void*,int){++stops;}void xTimerReset(void*,int){++resets;}
void xTimerPendFunctionCall(void(*f)(void*,uint32_t),void* p,int,int){f(p,0);}
struct {bool exitRequested=false;} inputState;
void releaseJoypad(){++releases;}void resetInputState(){inputState.exitRequested=false;}
void prepareRuntimeShutdown(){++stops;}
enum {event_quit,event_soft_reset,event_state_save,event_state_load,INP_STATE_MAKE};
using event_t=void(*)(int);void quit(int){assert(stops);++quits;}event_t event_get(int){return quit;}void triggerStateEvent(int){}
'''
checks=r'''
int main(int argc,char** argv){
 assert(argc==2);
 auto& state=lilka::controller.state;
 for(bool selectRelease:{false,true}){
 now=0;releaseAt=1990;releaseSelect=selectRelease;state.select.pressed=state.start.pressed=true;
 assert(!app.holdExitRequested());state=lilka::State();app.waitForRelease();
 }
 now=UINT32_MAX-1000;releaseAt=3000;state.select.pressed=state.start.pressed=true;
 assert(app.holdExitRequested());assert(uint32_t(now-(UINT32_MAX-1000))==2000);
 state=lilka::State();
 for(int choice:{0,4}){lilka::choices={choice};lilka::step=0;
 auto action=app.showSystemMenu();assert(action==(choice==0?NesApp::SystemAction::Screenshot:NesApp::SystemAction::Exit));}
 // Controls -> precision -> X minus -> Y plus -> back -> root B resumes.
 app.romConfigPath="/tmp/unused.nes";
 app.configFile=std::string(argv[1])+"/Game.conf";
 app.preferences=nesmenu::Preferences();
 lilka::choices={2,0,1,0,2,2,1,2,3,0};
 lilka::buttons={lilka::Button::A,lilka::Button::A,lilka::Button::A,lilka::Button::A,
 lilka::Button::A,lilka::Button::A,lilka::Button::A,lilka::Button::A,lilka::Button::A,lilka::Button::B};
 lilka::step=0;lilka::titles.clear();
 assert(app.showSystemMenu()==NesApp::SystemAction::Resume);
 assert(app.preferences.precisionMode && app.preferences.directionDelayXMs==200 && app.preferences.directionDelayYMs==300);
 assert(lilka::titles.size()==4 && lilka::titles[2]==K_S_EMU_AXIS_X && lilka::titles[3]==K_S_EMU_AXIS_Y);
 nesmenu::Preferences saved;assert(nesmenu::loadConfig(app.configFile.c_str(),saved)==nesmenu::ConfigResult::Ok);
 assert(saved.directionDelayXMs==200 && saved.directionDelayYMs==300);
 // Clamps and B at each nested boundary are explicit, marked navigation.
 app.preferences.directionDelayXMs=50;app.preferences.directionDelayYMs=1000;
 lilka::choices={2,1,0,0,2,1,0,0,0};
 lilka::buttons={lilka::Button::A,lilka::Button::A,lilka::Button::A,lilka::Button::B,
 lilka::Button::A,lilka::Button::A,lilka::Button::B,lilka::Button::B,lilka::Button::B};
 lilka::step=0;assert(app.showSystemMenu()==NesApp::SystemAction::Resume);
 assert(app.preferences.directionDelayXMs==50 && app.preferences.directionDelayYMs==1000);
 lilka::buttons.clear();
 now=0;releaseAt=1990;state=lilka::State();
 lilka::choices={4};lilka::step=0;openSystemMenu();assert(quits==1&&inputState.exitRequested&&stops&&releases);assert(!app.screenshotOnNextFrame&&!resets);
 inputState.exitRequested=false;now=0;releaseAt=2010;state.select.pressed=state.start.pressed=true;
 lilka::step=0;openSystemMenu();assert(quits==2&&inputState.exitRequested);assert(lilka::step==0);assert(!app.screenshotOnNextFrame&&!resets);
 puts("Shared actual short/long/wrap chord, localized visible root/axis routing/clamps/B, NES exit cleanup/callback PASS");
}
'''
# No hardcoded visible strings in the actual shared UI.
import re
assert not re.search(r'(?:addItem|setItem|Menu|showNotice)\([^\n]*"',s)
for language in ['uk','en']:
 labels=(ROOT/f'src/keira/localizations/lang_{language}.h').read_text()
 for token in set(re.findall(r'K_S_EMU_[A-Z_]+',s)):
  assert '#define '+token+' ' in labels,token
for language in ['uk','en']:
 with tempfile.TemporaryDirectory() as d:
  p=Path(d);(p/'test.cpp').write_text(pre.replace('lang_uk.h',f'lang_{language}.h')+body+post+modal+checks)
  for flags in [[],['-fsanitize=address,undefined','-fno-pie','-no-pie']]:
   subprocess.run(['g++','-std=c++11','-Wall','-Wextra','-Werror',*flags,'-I'+str(ROOT/'src'),str(p/'test.cpp'),str(ROOT/'src/apps/nes/preferences.cpp'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test'),str(p)],check=True,timeout=15)
# GB exit falls through the established audio/battery-save/release sequence.
gb=(ROOT/'src/apps/gameboy/gameboyapp.cpp').read_text()
assert 'holdExitRequested() ? SystemAction::Exit : showSystemMenu()' in gb
assert 'if (action == SystemAction::Exit) break;' in gb
assert 'stopAudio();\n    gbcore_copy_save(core);\n    if (!writeSave())' in gb
assert 'releaseGame();' in gb
