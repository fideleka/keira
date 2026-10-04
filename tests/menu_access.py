"""Production shared menu/gesture boundary harness; both locales and exit routing."""
from pathlib import Path
import subprocess,tempfile,re
ROOT=Path(__file__).resolve().parents[1]
s=(ROOT/'src/apps/emulator/systemmenu.cpp').read_text()
menu=s[s.index('EmulatorMenuApp::SystemAction'):s.index('// A Select-first')]
gesture=s[s.index('bool EmulatorMenuApp::holdExitRequested'):]
# Validate actual root layout fits the SDK viewport, and all UI text is localized.
root=menu[:menu.index('menu.addActivationButton')]
assert re.findall(r'menu.addItem\((\w+)\)',root)==['K_S_EMU_RESUME','K_S_EMU_SCREENSHOT','K_S_EMU_CONTROLS','K_S_EMU_MORE','K_S_EMU_EXIT']
assert not re.search(r'(addItem|setTitle|Menu |configWarning =)\([^\n]*"',s)
pre=r'''
#include "apps/nes/preferences.h"
#include <cassert>
#include <vector>
#include <string>
#include <cstdio>
using String=std::string;
String StringFormat(const char*,unsigned){return "delay";}
uint32_t now=0;uint32_t millis(){return now;}
#define pdMS_TO_TICKS(x) (x)
void vTaskDelay(unsigned n){now+=n;}
namespace lilka {
enum class Button{A,B};namespace colors{constexpr int White=0;}
struct Key{bool pressed=false;};struct State{Key select,start;};
struct Controller {unsigned calls=0,releaseAt=0;State getState(){State s;s.select.pressed=s.start.pressed=calls++<releaseAt;return s;}} controller;
struct Canvas{};
std::vector<int> choices;unsigned step=0;
struct Menu{std::vector<String> items;int selected=0;Menu(const String&){}
void addItem(const String& x){items.push_back(x);}void addActivationButton(Button){}
void setItem(int,const String&,void*,int,const String&){}void setTitle(const String&){}
void update(){assert(step<choices.size());selected=choices[step++];assert(selected<int(items.size()));}
void draw(Canvas*){}bool isFinished(){return true;}Button getButton(){return Button::A;}int getCursor(){return selected;}};
}
struct EmulatorMenuApp {
enum class SystemAction{Resume,Exit,Reset,Save,Load,Screenshot};
nesmenu::Preferences preferences;bool hasFrameskipSetting=false,automaticFrameskip=true;
String menuTitle="paused";lilka::Canvas* canvas=nullptr;unsigned releases=0,writes=0;
void waitForRelease(){++releases;}void queueDraw(){}void savePreferences(){++writes;}
SystemAction showSystemMenu();bool holdExitRequested();
};
'''
checks=r'''
int main(){EmulatorMenuApp app;
for(bool gb:{false,true}){app.hasFrameskipSetting=gb;
 for(int entry:{0,1,4}){lilka::choices={entry};lilka::step=0;
 auto action=app.showSystemMenu();assert(action==(entry==0?EmulatorMenuApp::SystemAction::Resume:entry==1?EmulatorMenuApp::SystemAction::Screenshot:EmulatorMenuApp::SystemAction::Exit));}
 lilka::choices={3,4,0,gb?6:5,4};lilka::step=0;assert(app.showSystemMenu()==EmulatorMenuApp::SystemAction::Exit);assert(app.writes==0); // cancel reset/back/exit
}
for(unsigned duration:{0u,1u,199u,200u,201u}){now=0;lilka::controller.calls=0;lilka::controller.releaseAt=duration;
 assert(app.holdExitRequested()==(duration>200));}
now=UINT32_MAX-100;lilka::controller.calls=0;lilka::controller.releaseAt=500;assert(app.holdExitRequested());
puts("Actual shared menu first-page Resume/Screenshot/Exit, reset cancel, short/long/rollover gesture PASS");}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)
 for lang in ['en','uk']:
  (p/'test.cpp').write_text('#include "keira/localizations/lang_'+lang+'.h"\n'+pre+menu+gesture+checks)
  for flags in [[],['-fsanitize=address,undefined','-fno-pie','-no-pie']]:
   subprocess.run(['g++','-std=c++11','-Wall','-Wextra','-Werror',*flags,'-I'+str(ROOT/'src'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
subprocess.run(['python3','tools/checklang.py'],cwd=ROOT,check=True)
