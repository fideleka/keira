#!/usr/bin/env python3
"""Source-only: compile real decoder and extracted status-bar functions against HAL stubs.
No firmware build, dependency download or SDK changes. --sanitize uses ASan/UBSan.
"""
from pathlib import Path
import subprocess
import tempfile
import argparse
ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()

def function(signature):
    text = (ROOT / 'src/apps/statusbar/statusbar.cpp').read_text()
    start = text.index(signature)
    opening = text.index('{', start)
    end, depth = opening + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

source = r'''
#include <cassert>
#include <algorithm>
#include <string>
#include <cstdio>
#include <limits>
#include <cmath>
#include "apps/statusbar/charge_status.h"
#include "apps/icons/battery.h"
#include "apps/icons/battery_absent.h"
#include "apps/icons/battery_danger.h"
#include "apps/icons/battery_charge.h"
#include "apps/icons/battery_measuring.h"
struct String {
 std::string value;
 String(int x): value(std::to_string(x)) {}
 String(float x,int digits) { char buf[32];snprintf(buf,sizeof(buf),"%.*f",digits,x);value=buf; }
 String(std::string x):value(x) {}
 String operator+(const char* s) const {return String(value+s);}
};
namespace lilka {
namespace colors {constexpr uint16_t Fuchsia=0xf81f, Green=0x7e0, Yellow=0xffe0, Red=0xf800;}
struct Canvas {
 const uint16_t* icon=nullptr; int cursor=0, fills=0; std::string text;
 void draw16bitRGBBitmapWithTranColor(int x,int y,const uint16_t* data,uint16_t,int w,int h) {
  assert(x==0 && y==0 && w==16 && h==24);icon=data;
 }
 void fillRect(int x,int y,int w,int h,uint16_t) {
  assert(x>=0 && y>=0 && x+w<=60 && y+h<=24);++fills;
 }
 void setCursor(int x,int y) {assert(y==17);cursor=x;}
 void print(const char* s) {text+=s;for(const unsigned char* p=(const unsigned char*)s;*p;++p)if((*p&0xc0)!=0x80)cursor+=9;}
 void print(const String& s) {print(s.value.c_str());}
 int getCursorX()const{return cursor;}
};
struct Battery {
 float raw=3.9f;int level=80;int rawCalls=0,estimatedCalls=0,voltageCalls=0;
 float readRawVoltage(){++rawCalls;return raw;}
 int readEstimatedLevel(){++estimatedCalls;return level;}
 float readVoltage(){++voltageCalls;return raw;}
} battery;
}
class StatusBarApp {
public:
 uint8_t batteryMode=1;
 int displayedBatteryLevel=-1,pendingBatteryLevel=-1;
 uint8_t pendingBatteryLevelSeconds=0;
 keira::ChargeStatusFilter chargeStatusFilter;
 float displayedBatteryVoltage=0.0f;
 int drawBattery(lilka::Canvas*);
 int stableBatteryLevel(int);
};
'''
source += function('int StatusBarApp::drawBattery(') + '\n' + function('int StatusBarApp::stableBatteryLevel(')
source += r'''
int main() {
 using S=keira::ChargeStatus;
 using F=keira::ChargeStatusFilter;
 // Theoretical normal-divider reconstructed bands over 3.0..4.2V VBAT.
 for(int i=0;i<=120;++i){float v=3.0f+i*.01f;
  assert(F::classify(v)==S::Battery);
  assert(F::classify(v*(24.812f/(33+24.812f))/.75188f)==S::Charging);
  assert(F::classify(v*(9.0909f/(33+9.0909f))/.75188f)==S::Charged);
 }
 for(float v:{-1.f,1.4f,1.45f,1.5f,2.65f,2.7f,2.8f,4.61f,
              std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()})
  assert(F::classify(v)==S::Unknown);
 for(float v:{0.f,.49f})assert(F::classify(v)==S::Absent);
 F filter;S confirmed=S::Unknown;
 for(int cycle=0;cycle<100;++cycle)for(float v:{2.1f,1.1f,3.9f}){
  assert(filter.update(v)==confirmed);assert(filter.update(v)==confirmed);
  confirmed=F::classify(v);assert(filter.update(v)==confirmed);assert(filter.update(v)==confirmed);
 }
 assert(filter.update(1.45f)==S::Battery);
 assert(filter.update(2.1f)==S::Battery);assert(filter.update(1.45f)==S::Battery);
 assert(filter.update(2.1f)==S::Battery);assert(filter.update(2.1f)==S::Battery);
 assert(filter.update(2.1f)==S::Charging);
 assert(filter.update(std::numeric_limits<float>::quiet_NaN())==S::Charging);
 filter.reset();assert(filter.update(2.1f)==S::Unknown);
 for(int mode=1;mode<=4;++mode){
  StatusBarApp app;app.batteryMode=mode;lilka::battery={};
#if defined(KEIRA_ADC_CHARGE_STATUS) && KEIRA_ADC_CHARGE_STATUS && LILKA_VERSION >= 2
  // Boot placeholder, then preserve the exact old presentation during debounce.
  S previous=S::Unknown;
  for(float v:{3.9f,2.1f,1.1f,3.9f}){
   lilka::battery.raw=v;lilka::battery.level=v<2.7f?5:80;
   for(int n=0;n<3;++n){
    int before=lilka::battery.estimatedCalls;lilka::Canvas c;int width=app.drawBattery(&c);
    assert(width<=60);assert(lilka::battery.voltageCalls==0);
    S shown=n<2?previous:F::classify(v);
    if(shown==S::Unknown){assert(c.text==(mode==2?"":"..."));if(mode==1||mode==2)assert(c.icon==battery_measuring_img);}
    else if(shown==S::Battery){assert(c.text==(mode==1||mode==3?"80%":mode==4?"3.90v":""));}
    else {
     assert(c.icon==(shown==S::Charging?battery_charging_img:battery_charged_img));assert(c.fills==0);
     assert(c.text==(mode==2?"":shown==S::Charging?K_S_BATTERY_CHARGING_SHORT:K_S_BATTERY_CHARGED_SHORT));
    }
    if(shown!=S::Battery || v<2.7f || mode==4)assert(lilka::battery.estimatedCalls==before);
   }
   previous=F::classify(v);
  }
  assert(lilka::battery.rawCalls==12);
  // Guard-band/invalid samples retain prior state and never sample a fake percent.
  int before=lilka::battery.estimatedCalls;
  for(float invalid:{0.f,2.7f,std::numeric_limits<float>::quiet_NaN()}){
   lilka::battery.raw=invalid;lilka::Canvas retained;app.drawBattery(&retained);
   assert(retained.text==(mode==1||mode==3?"80%":mode==4?"3.90v":""));
   assert(lilka::battery.estimatedCalls==before);
  }
  StatusBarApp boot;boot.batteryMode=mode;lilka::battery.raw=0;lilka::Canvas waiting;boot.drawBattery(&waiting);
  assert(waiting.text==(mode==2?"":"..."));
  lilka::Canvas stillWaiting;boot.drawBattery(&stillWaiting);assert(stillWaiting.text==(mode==2?"":"..."));
  lilka::Canvas absent;boot.drawBattery(&absent);assert(absent.text==(mode==2?"":"N/A"));
  if(mode==1||mode==2)assert(absent.icon==battery_absent_img);
  // Three low readings confirm absence; one low transition sample cannot.
  lilka::battery.raw=0;app.drawBattery(&waiting);app.drawBattery(&waiting);
  lilka::Canvas removed;app.drawBattery(&removed);assert(removed.text==(mode==2?"":"N/A"));
  lilka::battery.raw=3.9f;
  for(int n=0;n<3;++n){lilka::Canvas restored;app.drawBattery(&restored);assert(restored.text==(n<2?(mode==2?"":"N/A"):(mode==1||mode==3?"80%":mode==4?"3.90v":"")));}
  // A failed secondary estimate is not evidence of physical battery absence.
  lilka::battery.level=-1;lilka::Canvas badEstimate;app.drawBattery(&badEstimate);
  assert(badEstimate.text==(mode==1||mode==3?"80%":mode==4?"3.90v":""));


#else
  lilka::Canvas c;assert(app.drawBattery(&c)<=60);
  assert(c.text==(mode==1||mode==3?"80%":mode==4?"3.90v":""));
  assert(lilka::battery.rawCalls==0);
  assert(lilka::battery.estimatedCalls==(mode==4?0:1));
#endif
 }
 // Existing percent smoothing remains unchanged.
 StatusBarApp level;assert(level.stableBatteryLevel(80)==80);
 assert(level.stableBatteryLevel(78)==80);assert(level.stableBatteryLevel(78)==80);
 assert(level.stableBatteryLevel(78)==78);assert(level.stableBatteryLevel(-1)==-1);
 assert(level.stableBatteryLevel(90)==90);
 puts("decoder + real drawBattery passed");
}
'''
with tempfile.TemporaryDirectory(prefix='keira-charge-host-') as tmp:
    path = Path(tmp)
    (path/'test.cpp').write_text(source)
    for language in ('uk','en'):
        for version,enabled in ((2,False),(2,True),(1,True)):
            flags=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie'] if args.sanitize else []
            subprocess.run(['g++','-std=c++11','-Wall','-Wextra','-Werror',*flags,
                            '-DLILKA_VERSION='+str(version),*(['-DKEIRA_ADC_CHARGE_STATUS=1'] if enabled else []),
                            '-include',str(ROOT/f'src/keira/localizations/lang_{language}.h'),
                            '-I'+str(ROOT/'src'),str(path/'test.cpp'),'-o',str(path/'test')],check=True)
            subprocess.run([str(path/'test')],check=True)
print('6 configurations passed: UK/EN, stock v2, modified v2, v1 guard')
