#!/usr/bin/env python3
"""Execute the production deadline policy, including overruns and tick wrap."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/keira/appmanager.cpp").read_text()
start = source.index("void AppManager::waitForFrame()")
end = source.index("\nAppManager::AppManager()", start)
method = source[start:end]
prelude = r"""
#include <cassert>
#include <cstdint>
using TickType_t=uint32_t;
#define MAX_FPS 60
#define pdMS_TO_TICKS(x) (x)
uint32_t now=0, slept=0, until=0;
uint32_t xTaskGetTickCount(){return now;}
void vTaskDelay(unsigned ticks){slept+=ticks;now+=ticks;}
void vTaskDelayUntil(uint32_t* previous,unsigned ticks){++until;*previous+=ticks;now=*previous;}
struct AppManager {uint32_t lastFrameTick=0;void waitForFrame();};
"""
checks = r"""
int main(){
 AppManager app;
 now=5;app.waitForFrame();assert(until==1&&slept==0&&now==16);
 now=100;app.waitForFrame();assert(until==1&&slept==1&&app.lastFrameTick==101);
 now=105;app.waitForFrame();assert(until==2&&slept==1&&now==117);
 // Continuous overloaded rendering must yield on every iteration.
 for(int i=0;i<100;++i){now+=30;app.waitForFrame();}
 assert(slept==101&&until==2&&app.lastFrameTick==now);
 app.lastFrameTick=UINT32_MAX-10;now=5;app.waitForFrame();
 assert(slept==102&&app.lastFrameTick==6);
 app.lastFrameTick=UINT32_MAX-10;now=0;app.waitForFrame();
 assert(until==3&&now==5);
}
"""
with tempfile.TemporaryDirectory(prefix="keira-frame-pacing-") as directory:
    tmp = Path(directory)
    (tmp / "test.cpp").write_text(prelude + method + checks)
    subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    str(tmp / "test.cpp"), "-o", str(tmp / "test")], check=True)
    subprocess.run([str(tmp / "test")], check=True)
print("PASS: normal pacing, sustained overruns, recovery and tick wrap")
