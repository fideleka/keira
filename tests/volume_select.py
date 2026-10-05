#!/usr/bin/env python3
"""Real SDK Controller + NES OSD/GB modifier and mapping + shared Menu::update.
Rendering, emulator cores and blocking modal hardware are host boundaries.
"""
import argparse
import ast
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--sdk", type=Path, default=ROOT.parent / "sdk")
args = parser.parse_args()
SDK = args.sdk / "lib/lilka/src/lilka"
TEST = args.sdk / "tests/system_shortcuts"
U8G2 = Path(os.environ.get("U8G2_CLIB", ROOT.parent / "lilka-sdk/lib/lilka/.pio/libdeps/v2/U8g2/src/clib"))

def function(text, signature):
    begin = text.index(signature)
    opening = text.index("{", begin)
    end, depth = opening + 1, 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[begin:end]

def literal(path, name):
    for node in ast.parse(path.read_text()).body:
        if isinstance(node, ast.Assign) and any(isinstance(x, ast.Name) and x.id == name for x in node.targets):
            return ast.literal_eval(node.value)
    raise AssertionError(name)

pre = literal(ROOT / "tests/nesmenu_input.py", "prelude").replace("#define pdMS_TO_TICKS(x) (x)", "#include <vector>")
begin = pre.index("namespace lilka {")
end = pre.index("struct App", begin)
pre = pre[:begin] + '''#define private public
#include "controller.h"
#undef private
#include "audio.h"
''' + pre[end:]
pre = pre.replace("""uint32_t now = 0;
uint32_t millis() {return now;}""", "")
host = (TEST / "regression.cpp").read_text()
host = host[host.index("uint32_t hostNow"):host.index("using namespace lilka;")]
pre += host
osd = (ROOT / "src/apps/nes/osd.cpp").read_text()
helpers = osd[osd.index("namespace {"):osd.index("int osd_init_sound();")]
nes = function(osd, "void osd_getinput(void)")
gb = (ROOT / "src/apps/gameboy/gameboyapp.cpp").read_text()
start = gb.index("        if (state.selectConsumed)")
end = gb.index("        // Keep CPU", start)
modifier = gb[start:end]
modal = function(modifier, "            if (!startGameActive")
modifier = modifier.replace(modal, modal[:modal.index("{") + 1] + " ++menus; return 0xff; }")
modifier = modifier.replace("ksystem.apps.startToast(saveState() ? K_S_GB_STATE_SAVED : K_S_GB_STATE_SAVE_ERROR);", "++saves;")
modifier = modifier.replace("ksystem.apps.startToast(loadState() ? K_S_GB_STATE_LOADED : K_S_GB_STATE_LOAD_ERROR);", "++loads;")
modifier = modifier.replace("            nextFrameAt = esp_timer_get_time();", "").replace("            skippedInRow = 0;", "")
mapping = gb[gb.index("        const int horizontal"):gb.index("        gbcore_set_buttons", gb.index("        const int horizontal"))]
menu = function((SDK / "menu.cpp").read_text(), "void Menu::update()")
classes = r'''
namespace {
void openSystemMenu() { ++menus; releaseJoypad(); resetInputState(); }
}
struct GBInput {
 bool startWasPressed=false,startGameActive=false,selectWasPressed=false,selectConsumed=false;
 bool saveChordActive=false,loadChordActive=false;
 uint8_t turboAFrame=0,turboBFrame=0;
 nesmenu::Preferences preferences;
 nesmenu::DirectionFilter directionFilter,verticalFilter;
 uint8_t sample() {
 const auto state=lilka::controller.getState();
'''
classes += modifier + mapping + "return buttons; } };\n"
classes += r'''
namespace lilka {
struct Item { void (*callback)(void*)=nullptr; void* callbackData=nullptr; };
struct Menu {
 std::vector<Item> items = std::vector<Item>(8);
 std::vector<Button> activationButtons = {A};
 int cursor=3,scroll=0; uint32_t lastCursorMove=0; Button button=COUNT; bool done=false;
 void update();
};
#define MENU_HEIGHT 5
#define LILKA_UI_UPDATE_DELAY_MS 1
'''
classes += menu + "\n}\n"
checks = r'''
constexpr uint16_t S=1<<lilka::SELECT,T=1<<lilka::START;
void tick(uint16_t raw,uint32_t time) {
 hostNow=time;
 const int delta=lilka::controller.scanInputs(raw,time);
 if(delta) lilka::audio.stepVolumeShortcut(delta);
}
void fresh() {
 lilka::controller.physicalPressed=0;
 memset(lilka::controller.physicalTime,0,sizeof(lilka::controller.physicalTime));
 lilka::controller.shortcuts=lilka::detail::SystemShortcuts();
 lilka::controller.state=lilka::State();
 lilka::controller.setSystemShortcutsEnabled(true);
 resetInputState();
 app.directionFilter.reset();app.verticalFilter.reset();
}
int main() {
 for(bool gbMode:{false,true}) for(int direction:{0,1}) for(bool selectFirstRelease:{false,true}) for(bool delayed:{false,true}) {
  fresh(); GBInput gb; lilka::Menu menu;
  auto sample=[&](){
   if(gbMode) return !(gb.sample()&4);
   osd_getinput();return inputState.forwarded[4];
  };
  const uint16_t d=1<<direction;
  tick(S,100);assert(lilka::controller.peekState().select.justPressed);
  assert(!sample()); // Select is pending, not forwarded yet.
  tick(S|d,120);assert(!lilka::controller.peekState().select.pressed);
  assert(lilka::controller.peekState().selectConsumed);
  if(!delayed) assert(!sample());
  tick(S|d,520);if(!delayed) assert(!sample()); // repeat
  tick(selectFirstRelease?d:S,540);if(!delayed) assert(!sample());
  tick(selectFirstRelease?d:S,10000);if(!delayed) assert(!sample());
  tick(0,10020);tick(0,10040); // delayed reader misses capture AND release
  assert(!sample());assert(!sample());
  assert(lilka::controller.peekState().selectConsumed);
  tick(S,10060);assert(!lilka::controller.peekState().selectConsumed);assert(!sample());
  tick(0,10080);assert(sample()); // next ordinary tap still works
  assert(!sample());
  // Real shared menu consumes no direction/release/Select from volume adjustment.
  fresh();tick(S,100);menu.update();assert(menu.cursor==3&&!menu.done);
  tick(S|d,120);menu.update();assert(menu.cursor==3&&!menu.done);
  tick(selectFirstRelease?d:S,540);menu.update();assert(menu.cursor==3&&!menu.done);
  tick(0,560);menu.update();assert(menu.cursor==3&&!menu.done);
  tick(d,580);menu.update();assert(menu.cursor==(direction==0?2:4));
 }
 for(bool gbMode:{false,true}) {
  fresh();GBInput gb;
  auto sample=[&](){if(gbMode) return gb.sample();osd_getinput();return uint8_t(0xff);};
  unsigned before=menus;
  tick(S,100);sample();tick(S|1,120);sample();
  tick(S|T|1,520);sample();assert(menus==before+1); // Start wins even after volume.
  fresh();gb=GBInput();before=menus;
  tick(1,100);sample();tick(S|1,120);sample();
  assert(!lilka::controller.peekState().selectConsumed); // direction-first ordinary
  tick(S|T|1,140);sample();assert(menus==before+1);
  fresh();gb=GBInput();before=menus;
  tick(T,100);sample();tick(T|S|1,120);sample();assert(menus==before);
  fresh();gb=GBInput();tick(S,100);sample();tick(S|1,120);sample();
  lilka::controller.setSystemShortcutsEnabled(false);
  tick(S|1,520);sample();tick(1,540);sample();tick(0,560);sample();
  assert(gbMode?!gb.selectWasPressed:!inputState.forwarded[4]);
 }
 // The actual WAD-picker style shared Menu navigates with Select high at boot,
 // after a launch-held Select releases, and after a consumed shortcut releases.
 for(int direction:{0,1}) {
  fresh();lilka::Menu menu;const uint16_t d=1<<direction;
  int volume=lilka::audio.getVolume();
  tick(d,10);menu.update();assert(menu.cursor==(direction==0?2:4));
  tick(d,10000);menu.update();assert(lilka::audio.getVolume()==volume);
  fresh();menu.cursor=3;tick(S,100);menu.update();tick(d,105);menu.update();
  tick(d,110);menu.update();assert(menu.cursor==(direction==0?2:4));
  assert(lilka::audio.getVolume()==volume);
  tick(0,130);tick(d,150);menu.update();assert(lilka::audio.getVolume()==volume);
 }
 // Capture cancellation survives raw-release debounce and cannot synthesize a re-press.
 fresh();tick(S,100);tick(S|1,110);tick(1,115);
 assert(!lilka::controller.peekState().select.pressed);
 tick(S|1,116);assert(!lilka::controller.peekState().select.pressed);
 tick(1,120);tick(0,140);assert(lilka::controller.peekState().selectConsumed);
 puts("Production Controller+NES OSD/GB modifier+mapping/Menu: release orders, delayed frames, repeats, cancel, next tap, Start priority PASS");
}
'''
with tempfile.TemporaryDirectory(prefix="volume-select-") as directory:
    tmp=Path(directory)
    for name in ("controller.cpp","controller.h","audio.cpp","audio.h","config.h","system_shortcuts.h","volume_overlay.h"):
        (tmp/name).write_text((SDK/name).read_text())
    for name in ("Arduino.h","I2S.h","Preferences.h","serial.h","driver/uart.h","freertos/FreeRTOS.h","freertos/semphr.h"):
        p=tmp/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('''#pragma once
#include "host.h"
''')
    (tmp/"ping.h").write_text('''#pragma once
const uint8_t ping_raw[2]={};
const int ping_raw_size=2;
''')
    (tmp/"test.cpp").write_text(pre+helpers+nes+classes+checks)
    for flags in ([],["-fsanitize=address,undefined","-fno-pie","-no-pie"]):
        subprocess.run(["g++","-std=c++11","-DLILKA_VERSION=2","-DLILKA_NO_AUDIO_HELLO","-Wall","-Wextra",
                        "-Wno-reorder","-Wno-unused-parameter","-Wno-sign-compare",*flags,
                        "-I"+str(tmp),"-I"+str(TEST),"-I"+str(U8G2.parent),"-I"+str(ROOT/"src"),
                        str(tmp/"controller.cpp"),str(tmp/"audio.cpp"),str(tmp/"test.cpp"),"-o",str(tmp/"test")],check=True)
        subprocess.run([str(tmp/"test")],check=True)
