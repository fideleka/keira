"""Production NES blit differential test and patched emulation-input ordering."""
from pathlib import Path
import importlib.util
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

def method(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

pre = r'''
#include "apps/nes/pixel_convert.h"
#include <vector>
#include <cstdint>
#include <cassert>
#include <algorithm>
#include <cstdio>
#define LILKA_VERSION 2
#define NES_SCREEN_WIDTH 256
#define NES_SCREEN_HEIGHT 240
#define LILKA_DISPLAY_ROTATION 0
namespace lilka {
namespace colors {constexpr int Black=0;}
struct Canvas {
 int W,H,R; std::vector<uint16_t> data;
 Canvas(int w,int h,int r):W(w),H(h),R(r),data(w*h,0xffff){}
 int width(){return W;}int height(){return H;}int getRotation(){return R;}
 uint16_t* getFramebuffer(){return data.data();}
 int index(int x,int y)const {
  switch(R){case 1:return x*H+H-1-y;case 2:return (H-1-y)*W+W-1-x;
   case 3:return (W-1-x)*H+y;default:return y*W+x;}
 }
 void fillRect(int x,int y,int w,int h,int c){for(int j=std::max(0,y);j<std::min(H,y+h);++j)for(int i=std::max(0,x);i<std::min(W,x+w);++i)data[index(i,j)]=c;}
}; }
struct NesApp {lilka::Canvas* canvas;bool screenshotOnNextFrame=false;int submissions=0;void queueDraw(){++submissions;}};
struct bitmap_t {uint8_t*line[240];};
namespace screenshot {bool request(lilka::Canvas*){return true;}}
struct {struct{void startToast(int){}}apps;}ksystem;
#define K_S_SCREENSHOT_SAVE_ERROR 1
void nofrendo_log_printf(const char*,...){}
struct Driver {static NesApp*app;static int16_t w,h,frame_x,frame_y,frame_x_offset,frame_width,frame_height,frame_line_pixels;static uint8_t rotation;static uint16_t nesPalette[256];static int init(int,int);static void customBlit(bitmap_t*,int,void*);};
NesApp*Driver::app;int16_t Driver::w,Driver::h,Driver::frame_x,Driver::frame_y,Driver::frame_x_offset,Driver::frame_width,Driver::frame_height,Driver::frame_line_pixels;uint8_t Driver::rotation;uint16_t Driver::nesPalette[256];
'''
check=r'''
int main(){uint8_t pixels[240][256];bitmap_t bmp;
 for(int y=0;y<240;++y){bmp.line[y]=pixels[y];for(int x=0;x<256;++x)pixels[y][x]=(y*13+x*7)&255;}
 for(int i=0;i<256;++i)Driver::nesPalette[i]=i*251;
 for(auto size: {std::pair<int,int>{280,240},{240,280},{240,240},{320,200},{480,320},{281,241},{255,239},{1,1}})for(int r=0;r<4;++r){
 lilka::Canvas canvas(size.first,size.second,r);NesApp app{&canvas};Driver::app=&app;Driver::init(256,240);
 for(int frame=0;frame<3;++frame){std::fill(canvas.data.begin(),canvas.data.end(),0xfabc);Driver::customBlit(&bmp,0,nullptr);
 std::vector<uint16_t> expected(canvas.data.size(),0);int fw=std::min(256,size.first),fh=std::min(240,size.second),dx=(size.first-fw)/2,dy=(size.second-fh)/2;
 for(int y=0;y<fh;++y)for(int x=0;x<fw;++x)expected[canvas.index(dx+x,dy+y)]=Driver::nesPalette[pixels[(240-fh)/2+y][(256-fw)/2+x]];
 assert(canvas.data==expected);assert(app.submissions==frame+1);}}
 puts("PASS: production NES blit, all rotations/crops, stale overlays/borders and repeated buffers");}
'''
spec=importlib.util.spec_from_file_location('patch',ROOT/'tools/nofrendo/apply.py');patch=importlib.util.module_from_spec(spec);spec.loader.exec_module(patch)
with tempfile.TemporaryDirectory() as tmp:
    tmp=Path(tmp)
    driver=(ROOT/'src/apps/nes/driver.cpp').read_text()
    cpp=pre+method(driver,'int Driver::init(')+method(driver,'void Driver::customBlit(').replace('rect_t*','void*')+check
    (tmp/'render.cpp').write_text(cpp)
    for extra in ([],['-fsanitize=address,undefined','-fno-omit-frame-pointer']):
        subprocess.run(['g++','-std=c++17','-O2','-I'+str(ROOT/'src'),*extra,str(tmp/'render.cpp'),'-o',str(tmp/'render')],check=True)
        subprocess.run([str(tmp/'render')],check=True)
    overlay=patch.prepare(ROOT/'.pio/libdeps/v2/arduino-nofrendo/src',tmp/'core')
    core=(overlay/'nes/nes.c').read_text()
    input_test=r'''
#include <cassert>
#include <cstdio>
#include <cstdint>
struct Clock{int reads=0;operator int(){return ++reads<=4?0:3;}}nofrendo_ticks;
struct{bool poweroff=false,pause=false,autoframeskip=true;int scanline_cycles,fiq_cycles;struct Apu{void(*process)(void*,int);}*apu;}nes;
#define NES_FIQ_PERIOD 1
int polls=0,skipped=0,shown=0,renders=0;bool quit=false;
extern "C" uint64_t osd_measure_clock(){return 1000;}
extern "C" void osd_measure_frame(int draw,uint64_t us){assert(us==0);assert(draw==0||draw==1);}
extern "C" void osd_measure_report(){}
extern "C" void osd_measure_core_memory(){}
void osd_setsound(void(*)(void*,int)){}void gui_tick(int){}void gui_frame(bool){}
void osd_getinput(){assert(nofrendo_ticks.reads>=5);++polls;assert(polls==renders+1);if(quit)nes.poweroff=true;}
void vid_flush(){++shown;nes.poweroff=true;}
void nes_renderframe(bool draw){++renders;assert(polls==renders);if(!draw)++skipped;}
'''+method(core,'static void system_video(')+method(core,'void nes_emulate(')+r'''
int main(){decltype(nes)::Apu apu{};nes.apu=&apu;nes_emulate();assert(polls==3&&skipped==2&&shown==1);
 nes.poweroff=false;nofrendo_ticks.reads=0;polls=renders=shown=skipped=0;quit=true;nes_emulate();assert(polls==1&&!renders&&!shown);
 puts("PASS: input before skipped/displayed emulated frames; exit never renders another frame");}
'''
    (tmp/'input.cpp').write_text(input_test)
    for define in ([], ['-DKEIRA_MEASURE=1']):
        subprocess.run(['g++','-std=c++17','-O2','-fsanitize=address,undefined',*define,str(tmp/'input.cpp'),'-o',str(tmp/'input')],check=True)
        subprocess.run([str(tmp/'input')],check=True)
