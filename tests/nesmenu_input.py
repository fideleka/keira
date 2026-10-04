"""Compile the actual OSD input code with host controller/event shims.
Audio/timer/SDK modal rendering are device/native-build gates, not simulated here.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
osd = (ROOT / "src/apps/nes/osd.cpp").read_text()
helpers = osd[osd.index("namespace {"):osd.index("int osd_init_sound();")]
input_code = osd[osd.index("void osd_getinput(void)"):osd.index("int logprint(")]
prelude = r'''
#include "apps/nes/preferences.h"
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>
#define pdMS_TO_TICKS(x) (x)
using TickType_t = unsigned;
using event_t = void (*)(int);
enum {event_joypad1_up, event_joypad1_down, event_joypad1_left, event_joypad1_right,
      event_joypad1_select, event_joypad1_start, event_joypad1_a, event_joypad1_b,
      event_state_save, event_state_load};
enum {INP_STATE_BREAK, INP_STATE_MAKE};
namespace lilka {
struct ButtonState {bool pressed = false;};
struct State {ButtonState up, down, left, right, select, start, a, b, c, d;};
struct Controller {State state; State getState() {return state;}} controller;
}
struct App {nesmenu::Preferences preferences; nesmenu::DirectionFilter directionFilter;};
struct Driver {static App* app;};
App app; App* Driver::app = &::app;
struct Acquire {explicit Acquire(void*) {}};
void* xSoundMutex = nullptr;
unsigned saves = 0, loads = 0, menus = 0;
void saved(int) {++saves;} void loaded(int) {++loads;}
void joy(int) {}
event_t event_get(int index) {return index == event_state_save ? saved : index == event_state_load ? loaded : joy;}
uint32_t now = 0;
uint32_t millis() {return now;}
'''
checks = r'''
namespace {
void openSystemMenu() {
    ++menus;
    releaseJoypad();
    for (bool button : inputState.forwarded) assert(!button);
    // Model the real modal all-buttons-release barrier.
    lilka::controller.state = lilka::State();
    app.directionFilter.reset();
    resetInputState();
}
}
int main() {
    auto& s = lilka::controller.state;
    s.start.pressed = true; osd_getinput();
    assert(inputState.forwarded[5] && !menus); // solitary Start immediate
    s.select.pressed = true; osd_getinput();
    assert(inputState.forwarded[5] && !menus); // Start-first is not stolen
    s = lilka::State(); osd_getinput();
    assert(inputState.forwarded[4]); // solitary Select one-sample tap
    osd_getinput(); assert(!inputState.forwarded[4]);
    s.select.pressed = true; osd_getinput();
    s.start.pressed = true; s.a.pressed = true; osd_getinput();
    assert(menus == 1);
    osd_getinput();
    for (bool button : inputState.forwarded) assert(!button); // no modal chord/A leak
    s.select.pressed = true; s.c.pressed = true; osd_getinput();
    assert(saves == 1 && !inputState.forwarded[6]);
    osd_getinput(); assert(saves == 1);
    s.start.pressed = true; osd_getinput(); assert(menus == 1); // consumed Select cannot become menu
    s = lilka::State(); osd_getinput(); assert(!inputState.forwarded[4]);
    s.select.pressed = true; s.d.pressed = true; osd_getinput(); assert(loads == 1);
    s = lilka::State(); osd_getinput(); assert(!inputState.forwarded[4]);
    s.select.pressed = true; s.c.pressed = true; s.d.pressed = true; osd_getinput();
    s.d.pressed = false; osd_getinput(); assert(saves == 1 && loads == 1);
    s = lilka::State(); osd_getinput(); assert(!inputState.forwarded[4]);
    s.select.pressed = true; s.start.pressed = true; s.c.pressed = true; osd_getinput();
    assert(menus == 1 && saves == 1);
    s.start.pressed = false; osd_getinput(); assert(saves == 1); // mixed chord stays consumed
    s = lilka::State(); osd_getinput(); assert(!inputState.forwarded[4]);
    app.preferences.turboA = false; s.c.pressed = true; osd_getinput();
    assert(!inputState.forwarded[6]);
    s.a.pressed = true; osd_getinput(); assert(inputState.forwarded[6]); // ordinary A always works
    s = lilka::State(); osd_getinput();
    app.preferences.turboA = true; s.c.pressed = true;
    osd_getinput(); assert(inputState.forwarded[6]);
    osd_getinput(); assert(inputState.forwarded[6]);
    osd_getinput(); assert(!inputState.forwarded[6]); // unchanged C pulse period
    s = lilka::State(); osd_getinput();
    app.preferences.precisionMode = true; s.left.pressed = true; s.down.pressed = true; now = 0;
    osd_getinput(); assert(inputState.forwarded[2] && inputState.forwarded[1]);
    now = 20; osd_getinput(); assert(!inputState.forwarded[2] && inputState.forwarded[1]); // Down untouched
    now = 250; osd_getinput(); assert(inputState.forwarded[2] && inputState.forwarded[1]);
    puts("Actual OSD input: solitary/chord consumption/turbo/precision/Down tests PASS");
}
'''
with tempfile.TemporaryDirectory(prefix="keira-nesmenu-input-") as tmp:
    tmp = Path(tmp)
    source = tmp / "input.cpp"
    source.write_text(prelude + helpers + input_code + checks)
    subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                    "-fsanitize=address,undefined", "-I" + str(ROOT / "src"), str(source), "-o", str(tmp / "input")], check=True)
    subprocess.run([str(tmp / "input")], check=True)
