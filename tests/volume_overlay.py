#!/usr/bin/env python3
"""Execute real SDK presentation and Keira render loop against pixel/lock stubs."""
from pathlib import Path
import argparse
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--sdk", type=Path, default=ROOT.parent / "sdk-system-shortcuts")
args = parser.parse_args()
sdk = args.sdk / "lib/lilka/src/lilka"

def function(path, signature):
    text = path.read_text()
    start = text.index(signature)
    opening = text.index("{", start)
    end, depth = opening + 1, 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]

prelude = r'''
#include <cassert>
#include <cstdint>
#include <vector>
#include <cstdio>
#include <algorithm>
#include "volume_overlay.h"
uint32_t now = 100;
uint32_t millis() {return now;}
int lockDepth=0;
#define KMTX_LOCK(x) (++lockDepth)
#define KMTX_UNLOCK(x) (--lockDepth)
#define K_AMG_DBG
#define MAX_FPS 60
#define pdMS_TO_TICKS(x) (x)
#define GET_BACK(x) (x.empty() ? nullptr : x.back())
#define APP_PCAST(x) (x)
constexpr int KTS_SUSPENDED=1;
namespace lilka {
namespace colors {constexpr uint16_t Black=0;}
struct Canvas;
template<typename T> struct GFX {void drawCanvas(Canvas*);};
struct Surface {
 int w,h;std::vector<uint16_t> pixels;
 Surface(int w,int h):w(w),h(h),pixels(w*h,0){}
 int width(){return w;} int height(){return h;}
 void fillRect(int x,int y,int w,int h,uint16_t color) {
  assert(x>=0 && y>=0 && x+w<=this->w && y+h<=this->h);
  for(int j=y;j<y+h;++j)for(int i=x;i<x+w;++i)pixels[j*this->w+i]=color;
 }
 void drawRect(int x,int y,int w,int h,uint16_t c){
  fillRect(x,y,w,1,c);fillRect(x,y+h-1,w,1,c);fillRect(x,y,1,h,c);fillRect(x+w-1,y,1,h,c);
 }
 void fillScreen(uint16_t c){std::fill(pixels.begin(),pixels.end(),c);}
 void draw16bitRGBBitmap(int x,int y,const uint16_t* p,int w,int h){
  for(int j=0;j<h;++j)for(int i=0;i<w;++i)pixels.at((y+j)*this->w+x+i)=p[j*w+i];
 }
};
struct Canvas:Surface,GFX<Canvas> {
 int cx,cy;
 Canvas(int x,int y,int w,int h):Surface(w,h),cx(x),cy(y){}
 int x(){return cx;}int y(){return cy;}uint16_t* getFramebuffer(){return pixels.data();}
};
struct Display:Surface,GFX<Display> {
 Display():Surface(280,240){}
 int normal=0,interlaced=0;
 void drawCanvas(Canvas*);void presentCanvas(Canvas*);void drawSystemOverlay();
 void drawCanvasInterlaced(Canvas* c,bool){assert(lockDepth>0);++interlaced;presentCanvas(c);}
 void draw16bitRGBBitmap(int x,int y,const uint16_t* p,int w,int h){
  ++normal;Surface::draw16bitRGBBitmap(x,y,p,w,h);
 }
} display;
struct Audio {static VolumeOverlaySnapshot getVolumeOverlay();} audio;
VolumeOverlaySnapshot feedback;
VolumeOverlaySnapshot Audio::getVolumeOverlay(){return feedback;}
struct {void log(const char*){}} serial;
}
namespace AppFlags {constexpr int APP_FLAG_FULLSCREEN=1,APP_FLAG_INTERLACED=2;}
struct App {
 lilka::Canvas* backCanvas;
 explicit App(lilka::Canvas* c):backCanvas(c){}
 int flags=0,frame=0,canvasMutex=0,state=0;
 bool backgroundDirty=false,redraw=true;
 int getState(){return state;}void resume(){state=0;}
 int getFlags(){return flags;}bool getRedraw(){return redraw;}void setRedraw(bool r){redraw=r;}
};
struct ThreadManager {int lock=0;void threadsClean(){}};
struct Stop {};
void vTaskDelayUntil(uint32_t*,int){assert(lockDepth==0);throw Stop();}
struct AppManager:ThreadManager {
 std::vector<App*> threads;
 App* panel;int panelMtx=0;bool volumeOverlayWasVisible=false;uint32_t lastFrameTick=0;
 struct {uint32_t endTime=0;} toast;
 void threadsRun(){}void renderToast(lilka::Canvas*){assert(false);}
 void run();void renderToCanvas(lilka::Canvas*);
 void tick(){try{run();}catch(Stop&){}assert(lockDepth==0);}
};
'''
checks = r'''
int main(){
 using namespace lilka;
 for(auto dimensions : {std::pair<int,int>{280,240},{240,280}}){
  const int w=dimensions.first,h=dimensions.second;
  display=Display();display.w=w;display.h=h;display.pixels.assign(w*h,0);
  const auto g=volumeOverlayGeometry(w,h);
  assert(g.x*2+g.width==w && g.y*2+g.height==h);
  assert(g.width==w*3/4 && g.barHeight==22 && g.barWidth==g.width-20);
  Canvas panelCanvas(0,0,w,24), full(0,24,w,h-24), gb((w-160)/2,(h-144)/2,160,144);
  panelCanvas.fillScreen(0x1111);full.fillScreen(0x2222);gb.fillScreen(0x3333);
  App panel{&panelCanvas},app{&full};AppManager manager;manager.panel=&panel;manager.threads.push_back(&app);
  for(int mode=0;mode<4;++mode){
   // Menu/app, NES fullscreen, GB/GBC bounded framebuffer, interlaced NES.
   app.backCanvas=mode==2?&gb:&full;
   if(mode!=0 && mode!=2){full.cy=0;full.h=h;full.pixels.assign(w*h,0x2222);}
   else {full.cy=24;full.h=h-24;full.pixels.assign(w*(h-24),0x2222);}
   app.flags=mode==0?0:AppFlags::APP_FLAG_FULLSCREEN;
   if(mode==3)app.flags|=AppFlags::APP_FLAG_INTERLACED;
   const auto source=app.backCanvas->pixels,panelSource=panelCanvas.pixels;
   feedback.valid=false;manager.volumeOverlayWasVisible=false;app.redraw=true;panel.redraw=true;
   app.backgroundDirty=true;manager.tick();const auto clean=display.pixels;
   // Paused: no queueDraw, yet overlay appears, changes, renews, and expires.
   for(int level : {0,1,5,50,100,135,-1}){
    now+=100;feedback.level=level;feedback.valid=true;feedback.adjustedAt=now;
    const int interlaced=display.interlaced;manager.tick();
    assert(display.interlaced==interlaced);
    const int bounded=std::max(0,std::min(100,level));
    const int filled=(g.barWidth-4)*bounded/100;
    for(int x=0;x<g.barWidth-4;++x)
     assert(display.pixels[(g.barY+2)*w+g.barX+2+x]==(x<filled?0x07ff:0));
    assert(display.pixels[g.y*w+g.x]==0xffff);
    if(level==0){ // Verify readable high-contrast M in MUTE, independent of renderer constants.
     const int sx=g.x+(g.width-60)/2;
     const int rows[]={5,7,7,5,5};
     for(int r=0;r<5;++r)for(int c=0;c<3;++c)
      assert(display.pixels[(g.y+12+r*4)*w+sx+c*4]==((rows[r]&(1<<(2-c)))?0xffff:0));
    }
    assert(app.backCanvas->pixels==source && panelCanvas.pixels==panelSource);
    Canvas screenshot(0,0,w,h);manager.renderToCanvas(&screenshot);
    assert(screenshot.pixels==clean); // Screenshots never capture feedback.
   }
   now=feedback.adjustedAt+1199;manager.tick();assert(manager.volumeOverlayWasVisible);
   now++;manager.tick();assert(!manager.volumeOverlayWasVisible && display.pixels==clean);
   assert(app.backCanvas->pixels==source);
  }
  // Actual SDK full-frame presentation integrates automatically, without source writes.
  Canvas sdkCanvas(0,0,w,h);sdkCanvas.fillScreen(0x4444);const auto source=sdkCanvas.pixels;
  feedback.level=50;feedback.adjustedAt=now;feedback.valid=true;
  display.drawCanvas(&sdkCanvas);assert(display.pixels!=source && sdkCanvas.pixels==source);
  now+=1200;display.drawCanvas(&sdkCanvas);assert(display.pixels==source);
  Canvas partial(0,0,100,100);partial.fillScreen(0x5555);
  feedback.adjustedAt=now;display.presentCanvas(&sdkCanvas);display.drawCanvas(&partial);
  assert(display.pixels[g.y*w+g.x]!=0xffff); // Partial presentation never opts in.
 }
 Surface tiny(95,79);feedback.adjustedAt=now;
 drawVolumeOverlay(tiny,feedback,95,79,now);for(auto p:tiny.pixels)assert(p==0);
 puts("Actual SDK presentation + Keira paused/menu/NES/GB/GBC/interlaced restoration, snapshots, geometry/bars PASS");
}
'''
code = function(sdk / "display.cpp", "void GFX<T>::drawCanvas(")
code = "template <typename T>\n" + code
for signature in ("void Display::presentCanvas(", "void Display::drawCanvas(", "void Display::drawSystemOverlay("):
    code += "\n" + function(sdk / "display.cpp", signature)
code = "namespace lilka {\n" + code + "\n}\n"
for signature in ("void AppManager::run()", "void AppManager::renderToCanvas("):
    code += "\n" + function(ROOT / "src/keira/appmanager.cpp", signature)
# Verify the custom emulator paths submit canvases rather than bypass presentation.
assert "app->queueDraw();" in (ROOT / "src/apps/nes/driver.cpp").read_text()
assert "queueDraw();" in (ROOT / "src/apps/gameboy/gameboyapp.cpp").read_text()
assert "drawVolumeOverlay" not in function(ROOT / "src/keira/appmanager.cpp", "void AppManager::renderToCanvas(")
with tempfile.TemporaryDirectory(prefix="keira-volume-overlay-") as directory:
    tmp = Path(directory)
    (tmp / "test.cpp").write_text(prelude + code + checks)
    for flags in ([], ["-fsanitize=address,undefined", "-fno-pie", "-no-pie"]):
        subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", *flags, "-I" + str(sdk),
                        str(tmp / "test.cpp"), "-o", str(tmp / "test")], check=True)
        subprocess.run([str(tmp / "test")], check=True)
