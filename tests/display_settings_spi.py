"""Run the production Display settings menu with deterministic input and NVS stubs."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root/'src/apps/displaysettings/displaysettings.cpp').read_text()
source = re.sub(r'^#include .*$', '', source, flags=re.M)
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
template<class... T> String StringFormat(const char*,T...){return "frequency";}
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
struct Preferences {
 void begin(const char*,bool){assert(locked);}
 void end(){}
 void putUInt(const char* k,uint32_t v){assert(String(k)=="dispFrequency");stored=v;++writes;}
 void putUChar(const char* k,uint8_t v){assert(String(k)=="dispMode");stored=v;++writes;}
};
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
    (path/'test.cpp').write_text(preamble+'\n'+''.join(f'#define {name} "{name}"\n' for name in labels)+source+checks)
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-sign-compare',
                    '-fsanitize=address,undefined','-fno-pie','-no-pie',str(path/'test.cpp'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
