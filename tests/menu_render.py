"""Execute actual NES customBlit on two poisoned canvases, all rotations/fields."""
from pathlib import Path
import subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
s=(ROOT/'src/apps/nes/driver.cpp').read_text()
code=s[s.index('void Driver::customBlit'):s.index('uint16 Driver::nesPalette')]
prelude=r'''
#include <cassert>
#include <cstdint>
#include <vector>
#include <cstdio>
namespace lilka {
namespace colors {constexpr int Black=0;}
struct Canvas {
 std::vector<uint16_t> pixels=std::vector<uint16_t>(320*240,0xdead);
 void fillScreen(int n) {for(auto& p:pixels)p=n;}
 uint16_t* getFramebuffer(){return pixels.data();}
 void writePixelPreclipped(int x,int y,int p){pixels.at(y*320+x)=p;}
};
}
struct App {lilka::Canvas buffers[2]; lilka::Canvas* canvas=&buffers[0];
 bool screenshotOnNextFrame=false; int draws=0;
 void queueDraw(){++draws;canvas=&buffers[draws%2];}
} app;
int captured=0;
namespace screenshot {bool request(lilka::Canvas* c){assert(c->pixels[0]==0);assert(c->pixels[32]==42);++captured;return true;}}
constexpr const char* K_S_SCREENSHOT_SAVE_ERROR="failed";
struct {struct {void startToast(const char*){assert(false);}} apps;} ksystem;
struct bitmap_t {uint8_t* line[240];};struct rect_t{};
struct Driver {
 static App* app; static int frame_height,frame_width,frame_x,frame_y,w,h,rotation;
 static uint16_t nesPalette[256];
 static void customBlit(bitmap_t*,int,rect_t*);
};
App* Driver::app=&::app;
int Driver::frame_height=240,Driver::frame_width=256,Driver::frame_x=32,Driver::frame_y=0,Driver::w=320,Driver::h=240,Driver::rotation=0;
uint16_t Driver::nesPalette[256]={0,42};
'''
checks=r'''
int main(){uint8_t row[256];for(auto& p:row)p=1;bitmap_t bmp;for(auto& p:bmp.line)p=row;
 for(int rotation=0;rotation<4;++rotation){Driver::rotation=rotation;
  if(rotation%2){Driver::w=240;Driver::h=320;}else{Driver::w=320;Driver::h=240;}
  for(auto& c:app.buffers)for(auto& p:c.pixels)p=0xdead;
  app.screenshotOnNextFrame=(rotation==0);
  for(int i=0;i<4;++i)Driver::customBlit(&bmp,0,nullptr);
  for(auto& c:app.buffers)for(auto p:c.pixels)assert(p==0||p==42);
 }assert(captured==1);puts("Actual NES render double buffers/borders/rotations/snapshot PASS");}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(prelude+code+checks)
 for fields in [[],['-DINTERLACED']]:
  for sanitizer in [[],['-fsanitize=address,undefined','-fno-pie','-no-pie']]:
   subprocess.run(['g++','-std=c++11','-Wall','-Wextra','-Wno-unused-parameter',*fields,*sanitizer,str(p/'test.cpp'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
