"""Host execute the production NES timer with deterministic daemon delays."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "src/apps/nes/osd.cpp").read_text()
a = source.index("int osd_installtimer(")
b = source.index("\n/* filename manipulation */", a)
c = source.index("void stopNesTimer(")
d = source.index("\nvoid prepareRuntimeShutdown();", c)
pre = r'''
#include "keira/fractional_ticks.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#define LILKA_VERSION 2
#define configTICK_RATE_HZ 1000
using TickType_t=uint32_t;
struct Timer;using TimerHandle_t=Timer*;using TimerCallbackFunction_t=void(*)(TimerHandle_t);
struct NesApp {FractionalTicks timerPeriod{1000,60};TickType_t timerDeadline=0;TimerCallbackFunction_t timerCallback=nullptr;int timerMux=0;bool timerEnabled=false;};
NesApp app;struct Driver{static NesApp*app;};NesApp*Driver::app=&::app;
struct Timer {uint32_t period,due;void*id;TimerCallbackFunction_t fn;};
Timer storage;TimerHandle_t timer=nullptr;uint32_t now=0,callbacks=0,commandDelay=0;bool failCreate=false,failStart=false;int deletions=0;
uint32_t xTaskGetTickCount(){return now;}
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY 0
void portENTER_CRITICAL(int*){}void portEXIT_CRITICAL(int*){}
void nofrendo_log_printf(const char*,...){}
TimerHandle_t xTimerCreate(const char*,uint32_t period,int,void*id,TimerCallbackFunction_t fn){storage={period,now+period,id,fn};return failCreate?nullptr:&storage;}
int xTimerStart(TimerHandle_t,int){return !failStart;}
void xTimerDelete(TimerHandle_t,int){++deletions;}
void*pvTimerGetTimerID(TimerHandle_t h){return h->id;}
int xTimerChangePeriod(TimerHandle_t h,uint32_t period,int){assert(period>0&&period<=1000);h->period=period;h->due=now+commandDelay+period;return 1;}
bool disableDuringCallback=false;
using StaticSemaphore_t=int;using SemaphoreHandle_t=int*;
int stops=0,barriers=0,gives=0;
void xTimerStop(TimerHandle_t,int){++stops;}
SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t*p){*p=0;return p;}
void xSemaphoreGive(SemaphoreHandle_t p){*p=1;++gives;}
void xSemaphoreTake(SemaphoreHandle_t p,int){assert(*p==1);}
void vSemaphoreDelete(SemaphoreHandle_t p){assert(*p==1);}
void xTimerPendFunctionCall(void(*f)(void*,uint32_t),void*p,uint32_t n,int){++barriers;f(p,n);}
void tick(TimerHandle_t){++callbacks;if(disableDuringCallback)app.timerEnabled=false;}
void fire(uint32_t jitter=0){now=storage.due+jitter;storage.fn(&storage);}
'''
check = r'''
int main(){
 assert(osd_installtimer(0,(void*)tick,0,nullptr,0)==-1);
 failCreate=true;assert(osd_installtimer(60,(void*)tick,0,nullptr,0)==-1);
 failCreate=false;failStart=true;assert(osd_installtimer(60,(void*)tick,0,nullptr,0)==-1&&deletions==1&&!timer);
 failStart=false;
 for(unsigned hz:{50u,60u}){
 now=UINT32_MAX-100;callbacks=0;commandDelay=0;auto start=now;
 assert(!osd_installtimer(hz,(void*)tick,0,nullptr,0));
 for(unsigned n=0;n<hz*100;++n)fire();
 assert(now-start==100000&&callbacks==hz*100);
 // Variable daemon latency and a delayed timer command must not accumulate drift.
 start=now;callbacks=0;commandDelay=1;
 for(unsigned n=0;n<hz*100;++n)fire(n%19==0?3:0);
 assert(now-start>=100000&&now-start<=100004);
 // Stop barrier / menu resume resets the absolute phase, not the old debt.
 now+=15000;app.timerPeriod.reset();auto first=app.timerPeriod.next();app.timerDeadline=now+first;
 xTimerChangePeriod(timer,first,0);start=now;commandDelay=0;
 for(unsigned n=0;n<hz;++n)fire();assert(now-start>=1000&&now-start<=1001);
 }
 // A stop racing the original callback must suppress its reschedule.
 disableDuringCallback=true;const auto due=storage.due;fire();assert(storage.due==due);
 stopNesTimer(true);assert(stops==1&&barriers==1&&gives==1&&!app.timerEnabled);
 puts("PASS: production NES 50/60 Hz timer, tick wrap, callback/command jitter, menu rebase and allocation/start failures");
}
'''
with tempfile.TemporaryDirectory() as directory:
    p = Path(directory)
    (p / "timer.cpp").write_text(pre + source[c:d] + source[a:b] + check)
    subprocess.run(["g++", "-std=c++11", "-O2", "-fsanitize=address,undefined",
                    "-I" + str(ROOT / "src"), str(p / "timer.cpp"), "-o", str(p / "timer")], check=True)
    subprocess.run([str(p / "timer")], check=True)
