#!/usr/bin/env python3
"""Exercise the production device page with scripted inputs; no firmware build."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/apps/devices/devices.cpp').read_text()
page = source[source.index('void DevicesApp::devicePage'):]
assert 'settingPage' not in source
mock = r'''
#include <cassert>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>
struct String:std::string {using std::string::string;String(const std::string&s):std::string(s){}String(int n):std::string(std::to_string(n)){} };
#define K_S_DEVICES_ENABLED "Enabled"
#define K_S_ON "On"
#define K_S_OFF "Off"
#define K_S_STATUS "Status"
#define K_S_DEVICES_RESET "Reset"
#define K_S_DEVICES_IDENTITY "Identity"
#define K_S_ERROR "Error"
#define K_S_DEVICES_CHANGE_FAILED "Failed"
#define K_S_DEVICES_RESET_CONFIRM "Confirm"
#define KD_SETTING_CHOICE 1
#define KD_SETTING_BOOLEAN 2
#define pdMS_TO_TICKS(x) x
void vTaskDelay(int){}
int alerts=0;void alert(const char*,String){++alerts;}bool confirm(const char*,const char*){return true;}
const char*driverText(const char*s){return s;}
namespace lilka {
enum class Button {A,B};namespace colors{int White=1;}
struct Key {bool justPressed=false;};struct State{Key up,down,left,right,d,a,b;bool selectHeld=false;};
std::vector<State> script;unsigned tick=0,constructions=0;int lastScroll=0;
struct Controller {State peekState(){return script.at(tick);}}controller;
struct Menu {
 int cursor=0,scroll=0,count=0;bool finished=false,activate=true,horizontal=true;Button button=Button::A;
 Menu(const char*){++constructions;}void addItem(String,void* =nullptr,int=0,String=""){++count;}
 void addActivationButton(Button b){if(b==Button::A)activate=true;}void removeActivationButton(Button b){assert(b==Button::A);activate=false;}
 void setHorizontalNavigationEnabled(bool b){horizontal=b;}void setCursor(int n){cursor=n;}int getCursor(){return cursor;}
 bool isFinished(){return finished;}Button getButton(){return button;}
 void setItem(int row,String,void*,int,String){assert(row==cursor);assert(scroll==std::max(0,cursor-4));}
 void update(){assert(!horizontal);auto s=script.at(tick++);if(s.down.justPressed)cursor++;if(s.up.justPressed)cursor--;
 scroll=std::max(0,cursor-4);lastScroll=scroll;
 if(s.b.justPressed){button=Button::B;finished=true;}else if(s.a.justPressed&&activate){button=Button::A;finished=true;}}
 void draw(void*){}
};
}
#define K_BTN_BACK lilka::Button::B
struct DeviceManager {
 struct Info{const char*name="Display";const char*status="OK";const char*version="1";const char*uuid="id";bool enabled=true;unsigned settingCount=10;};
 struct SettingInfo{const char*label="Field";const char*choice="Choice";int kind=KD_SETTING_CHOICE,value=0;};
 int changes=0,value=0,enabledChanges=0,resets=0;bool fail=false;
 bool info(unsigned,Info&){return true;}bool settingInfo(unsigned,unsigned,SettingInfo&s){s.value=value;return true;}
 bool changeSetting(unsigned,unsigned f,int direction){assert(f==9);++changes;if(fail){fail=false;return false;}value=(value+direction+3)%3;return true;}
 bool setEnabled(unsigned,bool){++enabledChanges;return true;}bool resetSettings(unsigned){++resets;return true;}
}manager;
struct {DeviceManager*devices=&manager;}ksystem;
struct DevicesApp{void*canvas=nullptr;void queueDraw(){}void devicePage(unsigned);};
'''
checks = r'''
int main(){using namespace lilka;State down;down.down.justPressed=true;State a;a.a.justPressed=true;
State left;left.left.justPressed=true;State right;right.right.justPressed=true;State b;b.b.justPressed=true;
State select=a;select.selectHeld=true;State both=left;both.right.justPressed=true;
script.assign(11,down);script.insert(script.end(),{a,right,a,left,select,both,b});
DevicesApp app;app.devicePage(0);assert(manager.changes==4&&manager.value==2);assert(constructions==1&&lastScroll==7&&alerts==0);
// Failed changes remain on the same page and can be retried.
tick=0;constructions=0;manager.fail=true;script.assign(11,down);script.insert(script.end(),{a,a,b});
app.devicePage(0);assert(manager.changes==6&&manager.value==0&&alerts==1&&constructions==1);
// Ordinary actions still activate and Back leaves without changes.
tick=0;constructions=0;script={a,b};app.devicePage(0);assert(manager.enabledChanges==1&&constructions==2);
puts("Production inline driver settings: advance/reverse, wrap, modifier exclusion, retained scroll, failure/retry and ordinary actions PASS");}
'''
with tempfile.TemporaryDirectory(prefix='keira-inline-settings-') as directory:
    p=Path(directory)
    (p/'test.cpp').write_text(mock+page+checks)
    for flags in ([], ['-fsanitize=address,undefined','-fno-pie','-no-pie']):
        subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror',*flags,str(p/'test.cpp'),'-o',str(p/'test')],check=True)
        subprocess.run([str(p/'test')],check=True)
