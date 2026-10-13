#!/usr/bin/env python3
"""Run production PoC adapter/service against bus and compositor mocks, no firmware build."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='keira-mirror-host-') as tmp:
    tmp = Path(tmp)
    common = r'''
#pragma once
#include <cstdint>
#include <vector>
#include <cassert>
#include <cstring>
#include <initializer_list>
#include <stdexcept>
struct Stop {};
inline bool sleeping=false, failBus=false, locked=false;
inline int bitmapCalls=0, snapshotCalls=0, finalX=-1, finalY=-1, resetPin=-1;
inline std::vector<std::vector<int>> commands;
inline void vTaskDelay(int ticks) { if(ticks==100) throw Stop{}; }
#define pdMS_TO_TICKS(x) (x)
inline uint32_t millis(){return 1000;}
using gpio_num_t=int;
#define GPIO_MODE_OUTPUT 1
inline void gpio_set_direction(int pin,int){resetPin=pin;}
inline void gpio_set_level(int,int){}
struct SPIClass{};
class Arduino_DataBus {
public:
 void beginWrite(){assert(!locked);}
 void endWrite(){}
 void writeCommand(uint8_t c){commands.push_back({c});}
 void write(uint8_t v){commands.back().push_back(v);}
 void writeC8D16D16(int c,int a,int b){commands.push_back({c,a,b});}
};
class Arduino_HWSPI:public Arduino_DataBus {
public:
 Arduino_HWSPI(int dc,int cs,int clk,int mosi,int miso,SPIClass*,bool){
  assert(dc==21&&cs==-1&&clk==12&&mosi==13&&miso==-1);
 }
};
class Arduino_TFT {
public:
 Arduino_TFT(Arduino_DataBus* b,int8_t r,uint8_t,bool,int16_t,int16_t,uint8_t,uint8_t,uint8_t,uint8_t):_bus(b),_rst(r){}
 virtual void setRotation(uint8_t r){w=(r&1)?320:240;h=(r&1)?240:320;}
 virtual void writeAddrWindow(int16_t,int16_t,uint16_t,uint16_t)=0;
 bool begin(int speed){assert(speed==20000000);if(failBus)return false;tftInit();setRotation(1);return true;}
 int width(){return w;} int height(){return h;}
 void fillScreen(uint16_t){assert(!locked);}
 void draw16bitRGBBitmap(int x,int y,uint16_t* p,int ww,int hh){
  assert(!locked&&ww==280&&hh==240&&p[0]==0x1234);++bitmapCalls;finalX=x;finalY=y;
 }
protected:
 virtual void tftInit()=0;
 Arduino_DataBus* _bus;int8_t _rst;int8_t _override_datamode=-1;
 int w=240,h=320;
};
namespace lilka {
inline SPIClass SPI2;
namespace colors { constexpr uint16_t Black=0,Red=0xf800,Green=0x7e0,Blue=0x1f; }
class Canvas {
 int w,h;std::vector<uint16_t> p;
public:
 Canvas(int ww,int hh):w(ww),h(hh),p(ww*hh){}
 int width(){return w;} int height(){return h;}
 uint16_t* getFramebuffer(){return p.data();}
 void fillScreen(uint16_t color){for(auto& v:p)v=color;}
};
struct Display {int width(){return 280;}int height(){return 240;}};
inline Display display;
struct Settings {bool isSleeping(){return sleeping;}};
inline Settings displaySettings;
struct Overlay {uint32_t adjustedAt=0;bool visible(uint32_t)const{return false;}};
struct Controls {Overlay getVolumeOverlay(){return {};}Overlay getOverlay(){return {};}};
inline Controls audio,brightness;
struct Serial {template<class... T>void err(T...){} template<class... T>void log(T...){}};
inline Serial serial;
inline void drawVolumeOverlay(Canvas&,Overlay,int,int,uint32_t){}
}
class Service {
public:
 Service(const char*){}virtual ~Service()=default;void setktStackSize(int){}
 void invoke(){run();}
private:virtual void run()=0;
};
struct Apps {void renderToCanvas(lilka::Canvas* c){locked=true;++snapshotCalls;c->getFramebuffer()[0]=0x1234;locked=false;}};
struct System {Apps apps;};inline System ksystem;
'''
    (tmp/'common.h').write_text(common)
    for name in ['keira/service.h','keira/ksystem.h','lilka/spi.h','Arduino_GFX_Library.h','Arduino_TFT.h','driver/gpio.h','freertos/FreeRTOS.h','freertos/task.h']:
        dest=tmp/name;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_text('#include "common.h"\n')
    main=tmp/'main.cpp'
    main.write_text(r'''
#include "services/displaymirror/displaymirror.h"
int main(){
 {DisplayMirrorService s;try{s.invoke();}catch(Stop&){} }
 assert(snapshotCalls==1&&bitmapCalls==1&&finalX==20&&finalY==0&&resetPin==14);
 bool rgb565=false,displayOn=false,sleepOut=false;
 for(auto& c:commands){if(c==std::vector<int>{0x3a,0x55})rgb565=true;
 if(c==std::vector<int>{0x11})sleepOut=true;if(c==std::vector<int>{0x29})displayOn=true;}
 assert(rgb565&&sleepOut&&displayOn);
 sleeping=true;{DisplayMirrorService s;try{s.invoke();}catch(Stop&){} }
 assert(snapshotCalls==1&&bitmapCalls==1);
 failBus=true;{DisplayMirrorService s;s.invoke();}
 assert(snapshotCalls==1&&bitmapCalls==1);
 Arduino_DataBus bus;Ili9341Poc panel(&bus,14);
 for(int r=0;r<4;++r){panel.setRotation(r);assert(panel.width()==((r&1)?320:240));}
 panel.writeAddrWindow(20,0,280,240);
 assert(commands[commands.size()-3]==std::vector<int>({0x2a,20,299}));
 assert(commands[commands.size()-2]==std::vector<int>({0x2b,0,239}));
 assert(commands.back()==std::vector<int>({0x2c}));
}
''')
    src=root/'src/services/displaymirror'
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-DKEIRA_ILI9341_MIRROR_POC=1',f'-I{tmp}',f'-I{root / "src"}',str(src/'displaymirror.cpp'),str(src/'ili9341poc.cpp'),str(main),'-o',str(tmp/'check')],check=True)
    subprocess.run([str(tmp/'check')],check=True)
print('PASS: production mirror service and ILI9341 command adapter, using host mocks')
