"""Run the production Display settings menu with deterministic input and NVS stubs."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root/'src/apps/displaysettings/displaysettings.cpp').read_text()
labels = sorted(set(re.findall(r'\bK_S_\w+', source)))
preamble = r'''
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>
using std::size_t;
struct String:std::string {
 using std::string::string;
 String(const std::string& s):std::string(s){}
 String(uint32_t n):std::string(std::to_string(n)){}
};
#define K_BTN_BACK 1
#define NVS_LOCK assert(!locked);locked=true
#define NVS_UNLOCK assert(locked);locked=false
#define LILKA_SPI_NVS_NAMESPACE "spi"
#define LILKA_SPI_NVS_DISPLAY_FREQUENCY_KEY "dispFrequency"
#define LILKA_SPI_NVS_DISPLAY_MODE_KEY "dispMode"
#define SPI_MODE0 0
#define SPI_MODE1 1
#define SPI_MODE2 2
#define SPI_MODE3 3
#define pdMS_TO_TICKS(x) x
int selected=4,childCursor=2,childButton=0,writes=0,adjustments=0,consumed=0;
uint32_t stored=0;
bool locked=false,keyA=true,keyRight=false;
void vTaskDelay(int){}
namespace lilka {
namespace colors {constexpr int White=0;}
enum class Button {A};
struct Key {bool justPressed=false;};
struct State {bool selectHeld=false;Key left,right,a,d;};
struct Controller {
 State peekState(){State s;s.a.justPressed=keyA;s.right.justPressed=keyRight;return s;}
 State getState(){++consumed;return {};}
} controller;
struct Menu {
 bool child=true,done=false;
 Menu(){} Menu(const char*):child(false){}
 void addItem(const String&){} void addActivationButton(int){}
 void removeActivationButton(Button){}void setHorizontalNavigationEnabled(bool){}
 void setItem(int,const String&,void*,int,const String&){}
 void setTitle(const char*){}
 int getCursor(){return child?childCursor:selected;}
 int getButton(){return childButton;}
 bool isFinished(){return done;}
 void update(){if(child)assert(consumed==1);done=true;}
 void draw(void*){}
};
struct Brightness {
 bool isEnabled(){return true;}uint32_t getBrightness(){return 50;}
 void stepBrightnessShortcut(int){++adjustments;}
} brightness;
struct Settings {
 uint32_t getDimTimeoutSeconds(){return 0;}uint32_t getTimeoutSeconds(){return 0;}
 bool isAvailable(){return true;}
 void setDimTimeoutSeconds(uint32_t){++adjustments;}
 void setTimeoutSeconds(uint32_t){++adjustments;}
} displaySettings;
}
struct DeviceManager {
 enum Mode{Internal,External,Both};int displayMode(){return 0;}
 bool setDisplayMode(Mode){++adjustments;return true;}bool externalOutput(){return true;}
};
struct {DeviceManager* devices=nullptr;} ksystem;
struct App {explicit App(const char*){}};
struct DisplayConfigApp:App {
 DisplayConfigApp();void run();void setDisplaySpeed();void setDisplaySpiMode();
 void* canvas=nullptr;void queueDraw(){}void alert(const char*,const char*){}
};
'''
preferences_header = r'''
struct Preferences {
 void begin(const char*,bool){assert(locked);}
 void end(){}
 void putUInt(const char* k,uint32_t v){assert(String(k)=="dispFrequency");stored=v;++writes;}
 void putUChar(const char* k,uint8_t v){assert(String(k)=="dispMode");stored=v;++writes;}
};
'''

checks = r'''
int main(){
 for(int entry:{4,5})for(int back:{0,1}){
  selected=entry;childButton=back;writes=adjustments=consumed=0;stored=0;
  DisplayConfigApp app;app.run();
  assert(!locked&&consumed==1&&adjustments==0);
  assert(writes==(back?0:1));
  if(!back)assert(stored==(entry==4?40000000u:2u));
 }
 // Right on either SPI row must not change dim timeout or persist a selection.
 keyA=false;keyRight=true;
 for(int entry:{4,5}){
  selected=entry;writes=adjustments=consumed=0;
  DisplayConfigApp app;app.run();
  assert(writes==0&&adjustments==0&&consumed==0);
 }
 puts("Display SPI menus: opening press consumed, frequency/mode saves and Back cancellation PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='keira-display-spi-') as directory:
    path = Path(directory)
    # Keep production includes. Only HAL/application boundaries are stubbed;
    # StringFormat's declaration comes from the actual Keira header.
    for name in ['displaysettings.h', 'keira/keira.h', 'keira/ksystem.h',
                 'devices/devicemanager.h', 'Arduino.h']:
        header = path/name
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_text('#pragma once\n')
    header = path/'keira/utils/string.h'
    header.parent.mkdir(parents=True, exist_ok=True)
    header.write_text((root/'src/keira/utils/string.h').read_text())
    (path/'Preferences.h').write_text('#pragma once\n'+preferences_header)
    prefix = preamble+'\n'+''.join(f'#define {name} "{name}"\n' for name in labels)
    suffix = '\nString StringFormat(const char*,...){return "frequency";}\n'+checks
    command = ['g++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-sign-compare',
               '-I'+str(path), '-fsanitize=address,undefined','-fno-pie','-no-pie',
               str(path/'test.cpp'),'-o',str(path/'test')]
    (path/'test.cpp').write_text(prefix+source+suffix)
    subprocess.run(command, check=True)
    subprocess.run([str(path/'test')],check=True)
    for include in ['#include <Preferences.h>', '#include "keira/utils/string.h"']:
        (path/'test.cpp').write_text(prefix+source.replace(include, '')+suffix)
        result = subprocess.run(command, text=True, capture_output=True)
        assert result.returncode != 0, f'Missing include was masked: {include}'
    print('Missing Preferences/StringFormat includes correctly fail compilation PASS')
