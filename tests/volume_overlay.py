#!/usr/bin/env python3
"""Execute production SDK/Keira presentation against pixel/lock stubs.

Every LCD pixel write (bitmap, primitive, window/writePixels) is checked during
active feedback and expiry: protected pixels must be final, written at most
once, and never produced by LCD primitives. This is not merely a final-image
comparison. Static unchanged feedback writes nothing; game refreshes retain
field parity outside it. Production Display state is extracted from its header.
Hardware timing/SPI library implementation are intentionally not simulated.
"""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--sdk", type=Path, default=ROOT.parent / "sdk-system-shortcuts")
parser.add_argument(
    "--u8g2", type=Path,
    default=Path(os.environ.get("U8G2_CLIB", ROOT.parent / "lilka-sdk/lib/lilka/.pio/libdeps/v2/U8g2/src/clib")),
    help="Existing read-only U8g2 clib directory; never downloads dependencies")
args = parser.parse_args()
u8g2 = args.u8g2
assert (u8g2 / "u8g2_font.c").is_file(), "Supply --u8g2 with an existing dependency directory"
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
#include <cstring>
#include <chrono>
using std::min;using std::max;
#include "volume_overlay.h"
uint32_t now = 100;
uint32_t millis() {return now;}
int lockDepth=0;
std::vector<uint16_t> expected;
std::vector<int> touched;
std::vector<int> allTouched;
lilka::VolumeOverlayGeometry protectedRegion = {};
int pixelWrites=0, primitiveWrites=0;
void record(int x,int y,int width,uint16_t value,bool primitive){
 ++pixelWrites;
 if(!allTouched.empty())++allTouched.at(y*width+x);
 const auto& g=protectedRegion;
 if(x>=g.x && x<g.x+g.width && y>=g.y && y<g.y+g.height){
  assert(!primitive);assert(value==expected.at(y*width+x));
  assert(++touched.at(y*width+x)==1);
 }
}
std::vector<int> held;
void lock(int id){assert(held.empty() || held.back()<id);held.push_back(id);++lockDepth;}
void unlock(int id){assert(!held.empty()&&held.back()==id);held.pop_back();--lockDepth;}
#define KMTX_LOCK(x) ::lock(x)
#define KMTX_UNLOCK(x) unlock(x)
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
void referenceFontLine(u8g2_t* font,u8g2_uint_t x,u8g2_uint_t y,u8g2_uint_t len,uint8_t dir){
 assert(dir==0);
 static_cast<Surface*>(u8g2_GetUserPtr(font))->fillRect(x,y,len,1,0xffff);
}
struct Canvas:Surface,GFX<Canvas> {
 int cx,cy;
 Canvas(int x,int y,int w,int h):Surface(w,h),cx(x),cy(y){}
 int x(){return cx;}int y(){return cy;}uint16_t* getFramebuffer(){return pixels.data();}
};
struct Display:Surface,GFX<Display> {
 Display():Surface(280,240){}
 int normal=0,interlaced=0,rotation=0,wx=0,wy=0,ww=0,wh=0,cursor=0,writeDepth=0;
 int getRotation(){return rotation;}
 // PRODUCTION_DISPLAY_STATE
 void drawCanvas(Canvas*);void presentCanvas(Canvas*);void drawSystemOverlay();
 bool prepareSystemOverlay(const VolumeOverlaySnapshot&,uint32_t);
 void presentCanvasOutsideOverlay(Canvas*,int parity=-1);
 void clearOutsideOverlay(uint16_t);void finishSystemOverlay(Canvas* const*,int);
 bool systemOverlayNeedsTransfer() const;
 void drawCanvasInterlaced(Canvas*,bool);
 void startWrite(){assert(writeDepth++==0);}
 void endWrite(){assert(--writeDepth==0);}
 void writeAddrWindow(int x,int y,int w,int h){
  assert(writeDepth==1);assert(x>=0&&y>=0&&x+w<=width()&&y+h<=height());
  wx=x;wy=y;ww=w;wh=h;cursor=0;
 }
 void writePixels(uint16_t* p,int count){
  assert(writeDepth==1);assert(cursor+count<=ww*wh);
  for(int i=0;i<count;++i,++cursor){
   int x=wx+cursor%ww,y=wy+cursor/ww;
   record(x,y,w,p[i],false);pixels.at(y*w+x)=p[i];
  }
 }
 void fillRect(int x,int y,int rw,int rh,uint16_t c){
  ++primitiveWrites;
  for(int j=y;j<y+rh;++j)for(int i=x;i<x+rw;++i){record(i,j,w,c,true);pixels.at(j*w+i)=c;}
 }
 void fillScreen(uint16_t c){fillRect(0,0,w,h,c);}
 void drawRect(int x,int y,int rw,int rh,uint16_t c){
  fillRect(x,y,rw,1,c);fillRect(x,y+rh-1,rw,1,c);fillRect(x,y,1,rh,c);fillRect(x+rw-1,y,1,rh,c);
 }
 void draw16bitRGBBitmap(int x,int y,const uint16_t* p,int rw,int rh){
  ++normal;
  for(int j=0;j<rh;++j)for(int i=0;i<rw;++i){record(x+i,y+j,w,p[j*rw+i],false);pixels.at((y+j)*w+x+i)=p[j*rw+i];}
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
 explicit App(lilka::Canvas* c):backCanvas(c){static int next=10;canvasMutex=next++;}
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
 App* panel;App* lastPresentedApp=nullptr;int panelMtx=1;uint32_t lastFrameTick=0;
 struct {uint32_t endTime=0;} toast;
 void threadsRun(){}void renderToast(lilka::Canvas*){assert(false);}
 void run();void renderToCanvas(lilka::Canvas*);
 void tick(){try{run();}catch(Stop&){}assert(lockDepth==0);}
};
'''
checks = r'''
int main(){
 using namespace lilka;
 static_assert(sizeof(OverlayStorage)==624, "Bounded presentation storage changed");
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
   feedback.valid=false;protectedRegion={};app.redraw=true;panel.redraw=true;
   app.backgroundDirty=true;manager.tick();const auto clean=display.pixels;
   // Paused: no queueDraw, yet overlay appears, changes, renews, and expires.
   for(int level : {0,1,2,3,4,5,6,7,8,9,10,50,100,135,-1}){
    now+=100;feedback.level=level;feedback.valid=true;feedback.adjustedAt=now;
    Surface finalFrame(w,h);finalFrame.pixels=clean;
    drawVolumeOverlay(finalFrame,feedback,w,h,now);
    expected=finalFrame.pixels;protectedRegion=g;touched.assign(w*h,0);pixelWrites=0;primitiveWrites=0;
    const bool changed=!display.overlayActive || display.overlayLevel!=max(0,min(100,level));
    manager.tick();
    for(int y=g.y;y<g.y+g.height;++y)for(int x=g.x;x<g.x+g.width;++x)assert(touched[y*w+x]==(changed?1:0));
    assert(primitiveWrites==0); // No LCD primitives used for overlay.
    touched.assign(w*h,0);pixelWrites=0;manager.tick();assert(pixelWrites==0); // Static, unchanged.
    // Both menu/statusbar and game refreshes may update outside, never the panel.
    app.frame=level&1;app.redraw=true;panel.redraw=true;touched.assign(w*h,0);allTouched.assign(w*h,0);manager.tick();
    for(auto n:touched)assert(n==0);
    if(mode==3){
     for(int y=0;y<h;++y)for(int x=0;x<w;++x){
      const bool inside=x>=g.x&&x<g.x+g.width&&y>=g.y&&y<g.y+g.height;
      assert(allTouched[y*w+x]==(!inside && y%2==app.frame%2 ? 1 : 0));
     }
    }
    allTouched.clear();
    const int bounded=std::max(0,std::min(100,level));
    const int filled=(g.barWidth-4)*bounded/100;
    for(int x=0;x<g.barWidth-4;++x)
     assert(display.pixels[(g.barY+2)*w+g.barX+2+x]==(x<filled?0x07ff:0));
    assert(display.pixels[g.y*w+g.x]==0xffff);
    // Independent regular-font reference: actual U8g2 asset/decoder, fixed
    // 10px advances and baseline, not drawVolumeOverlay or block-font mocks.
    Surface reference(w,h);
    u8g2_t font{};
    u8g2_SetUserPtr(&font,&reference);
    static const u8g2_cb_t fontCallbacks={nullptr,nullptr,referenceFontLine};
    font.cb=&fontCallbacks;font.user_x1=w;font.user_y1=h;
#ifdef U8G2_WITH_CLIP_WINDOW_SUPPORT
    font.is_page_clip_window_intersection=1;
#endif
    font.draw_color=1;u8g2_SetFont(&font,u8g2_font_10x20_t_cyrillic);
    u8g2_SetFontMode(&font,1);u8g2_SetFontPosBaseline(&font);
    char label[5];
    if(bounded)snprintf(label,sizeof(label),"%d%%",bounded);else strcpy(label,"MUTE");
    const int length=strlen(label);
    for(int i=0;i<length;++i){
     assert(u8g2_GetGlyphWidth(&font,label[i])==10);
     assert(u8g2_DrawGlyph(&font,g.x+(g.width-length*10)/2+i*10,g.y+32,label[i])==10);
    }
    int ink=0;
    for(int y=g.y+12;y<g.y+32;++y)for(int x=g.x+2;x<g.x+g.width-2;++x){
     assert(display.pixels[y*w+x]==reference.pixels[y*w+x]);
     ink+=reference.pixels[y*w+x]!=0;
    }
    assert(ink>0);
    if(level==0){
     // Pinned real FONT_10x20 M bitmap: 1px raster, not the old scaled block M.
     const char* rows[]={"..........","..........","..........","..........","..........",
                         "..........","..........",".##....##.",".##....##.",".###..###.",
                         ".###..###.",".########.",".##.##.##.",".##.##.##.",".##.##.##.",
                         ".##.##.##.",".##....##.",".##....##.",".##....##.",".##....##."};
     const int sx=g.x+(g.width-40)/2;
     for(int yy=0;yy<20;++yy)for(int xx=0;xx<10;++xx)
      assert(display.pixels[(g.y+12+yy)*w+sx+xx]==(rows[yy][xx]=='#'?0xffff:0));
    }
    assert(app.backCanvas->pixels==source && panelCanvas.pixels==panelSource);
    Canvas screenshot(0,0,w,h);manager.renderToCanvas(&screenshot);
    assert(screenshot.pixels==clean); // Screenshots never capture feedback.
   }
   now=feedback.adjustedAt+1199;touched.assign(w*h,0);pixelWrites=0;
   manager.tick();assert(display.overlayActive && pixelWrites==0);
   now++;expected=clean;touched.assign(w*h,0);manager.tick();
   assert(!display.overlayActive && display.pixels==clean);
   for(int y=g.y;y<g.y+g.height;++y)for(int x=g.x;x<g.x+g.width;++x)assert(touched[y*w+x]==1);
   protectedRegion={};pixelWrites=0;manager.tick();assert(pixelWrites==0);
   assert(app.backCanvas->pixels==source);
  }
  // Shared presentation: switch from a live menu to paused letterboxed game
  // with no queued redraw/backgroundDirty. Identity invalidation redraws outside
  // while leaving the cached opaque panel completely untouched.
  app.flags=0;app.backCanvas=&full;app.backgroundDirty=true;feedback.valid=false;
  protectedRegion={};manager.tick();
  feedback.level=50;feedback.adjustedAt=now;feedback.valid=true;Surface overlayFrame(w,h);drawVolumeOverlay(overlayFrame,feedback,w,h,now);
  expected=overlayFrame.pixels;protectedRegion=g;touched.assign(w*h,0);manager.tick();
  App next(&gb);next.flags=AppFlags::APP_FLAG_FULLSCREEN;next.redraw=false;
  manager.threads.back()=&next;touched.assign(w*h,0);manager.tick();for(auto n:touched)assert(n==0);
  Canvas nextClean(0,0,w,h);manager.renderToCanvas(&nextClean);
  next.backgroundDirty=true;touched.assign(w*h,0);manager.tick();for(auto n:touched)assert(n==0);
  // Changed app and panel sources remain hidden beneath the unchanged overlay.
  gb.fillScreen(0x6666);next.redraw=true;touched.assign(w*h,0);manager.tick();for(auto n:touched)assert(n==0);
  manager.renderToCanvas(&nextClean);
  now+=1200;expected=nextClean.pixels;touched.assign(w*h,0);manager.tick();assert(display.pixels==expected);
  for(int y=g.y;y<g.y+g.height;++y)for(int x=g.x;x<g.x+g.width;++x)assert(touched[y*w+x]==1);
  protectedRegion={};
  // Overlapping layers: status panel visible in the uncovered portions of a
  // bounded non-fullscreen app. Expiry must transmit the final composition only.
  panelCanvas.h=h;panelCanvas.pixels.assign(w*h,0x1111);next.flags=0;next.backgroundDirty=true;
  feedback.level=50;feedback.adjustedAt=now;feedback.valid=true;
  expected=overlayFrame.pixels;protectedRegion=g;touched.assign(w*h,0);manager.tick();
  manager.renderToCanvas(&nextClean);
  now+=1200;expected=nextClean.pixels;touched.assign(w*h,0);manager.tick();assert(display.pixels==expected);
  for(int y=g.y;y<g.y+g.height;++y)for(int x=g.x;x<g.x+g.width;++x)assert(touched[y*w+x]==1);
  protectedRegion={};panelCanvas.h=24;panelCanvas.pixels.assign(w*24,0x1111);next.flags=AppFlags::APP_FLAG_FULLSCREEN;
  // Same-size 180-degree rotation invalidates cached LCD coordinates/panel.
  feedback.level=5;feedback.adjustedAt=now;feedback.valid=true;drawVolumeOverlay(overlayFrame,feedback,w,h,now);
  expected=overlayFrame.pixels;protectedRegion=g;touched.assign(w*h,0);manager.tick();
  display.rotation=2;touched.assign(w*h,0);manager.tick();
  for(int y=g.y;y<g.y+g.height;++y)for(int x=g.x;x<g.x+g.width;++x)assert(touched[y*w+x]==1);
  // Portrait/landscape rotation with resized/repositioned retained sources.
  const int rw=h,rh=w;display.rotation=3;display.w=rw;display.h=rh;display.pixels.assign(rw*rh,0);
  panelCanvas.w=rw;panelCanvas.pixels.assign(rw*24,0x1111);
  gb.cx=(rw-160)/2;gb.cy=(rh-144)/2;
  const auto rotatedG=volumeOverlayGeometry(rw,rh);Surface rotatedFrame(rw,rh);
  drawVolumeOverlay(rotatedFrame,feedback,rw,rh,now);
  expected=rotatedFrame.pixels;protectedRegion=rotatedG;touched.assign(rw*rh,0);manager.tick();
  Canvas rotatedClean(0,0,rw,rh);manager.renderToCanvas(&rotatedClean);
  now+=1200;expected=rotatedClean.pixels;touched.assign(rw*rh,0);manager.tick();assert(display.pixels==expected);
  for(int y=rotatedG.y;y<rotatedG.y+rotatedG.height;++y)for(int x=rotatedG.x;x<rotatedG.x+rotatedG.width;++x)
   assert(touched[y*rw+x]==1);
  protectedRegion={};display=Display();display.w=w;display.h=h;display.pixels.assign(w*h,0);
  // Actual SDK full-frame presentation integrates automatically, without source writes.
  Canvas sdkCanvas(0,0,w,h);sdkCanvas.fillScreen(0x4444);const auto source=sdkCanvas.pixels;
  feedback.level=50;feedback.adjustedAt=now;feedback.valid=true;
  Surface sdkFinal(w,h);sdkFinal.pixels=source;drawVolumeOverlay(sdkFinal,feedback,w,h,now);
  expected=sdkFinal.pixels;protectedRegion=g;touched.assign(w*h,0);
  display.drawCanvas(&sdkCanvas);assert(display.pixels==expected && sdkCanvas.pixels==source);
  touched.assign(w*h,0);display.drawCanvas(&sdkCanvas);for(auto n:touched)assert(n==0);
  sdkCanvas.fillScreen(0x7777);touched.assign(w*h,0);display.drawCanvas(&sdkCanvas);
  for(auto n:touched)assert(n==0);
  assert(sdkCanvas.pixels==std::vector<uint16_t>(w*h,0x7777));
  now+=1200;expected=sdkCanvas.pixels;touched.assign(w*h,0);display.drawCanvas(&sdkCanvas);assert(display.pixels==expected);
  for(int y=g.y;y<g.y+g.height;++y)for(int x=g.x;x<g.x+g.width;++x)assert(touched[y*w+x]==1);
  protectedRegion={};
  Canvas partial(0,0,100,100);partial.fillScreen(0x5555);
  feedback.adjustedAt=now;display.presentCanvas(&sdkCanvas);display.drawCanvas(&partial);
  assert(display.pixels[g.y*w+g.x]!=0xffff); // Partial presentation never opts in.
 }
 Surface tiny(95,79);feedback.adjustedAt=now;
 drawVolumeOverlay(tiny,feedback,95,79,now);for(auto p:tiny.pixels)assert(p==0);
 uint16_t guarded[212]={};
 const auto benchStart=std::chrono::steady_clock::now();
 for(int frame=0;frame<1000;++frame){
  VolumeOverlaySnapshot state;state.valid=true;state.level=frame%101;
  const auto geometry=volumeOverlayGeometry(280,240);
  for(int y=geometry.y;y<geometry.y+geometry.height;++y){
   VolumeOverlayRow row{guarded+1,geometry.x,y,geometry.width};
   drawVolumeOverlay(row,state,280,240,0);
   assert(guarded[0]==0 && guarded[211]==0);
  }
 }
 const auto micros=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-benchStart).count();
 printf("Host 1000 changed scanline panels: %lld us; font context %zu bytes, decoder %zu bytes, source asset 6979 bytes\n",(long long)micros,sizeof(VolumeOverlayFontTarget<VolumeOverlayRow>),sizeof(u8g2_t));
 puts("Every LCD write: SDK + Keira static/menu/statusbar/NES/GB/GBC/interlace, expiry/switch/rotation/immutable sources PASS");
}
'''
display_header = (sdk / "display.h").read_text()
state = display_header[display_header.index("    static constexpr int overlayRowWidth"):
                       display_header.index("    const void* splash;")]
prelude = prelude.replace("// PRODUCTION_DISPLAY_STATE", state)
prelude += "namespace lilka { struct OverlayStorage {\n" + state + "}; }\n"
prelude = "#define LILKA_DISPLAY_WIDTH 240\n#define LILKA_DISPLAY_HEIGHT 280\n" + prelude
code = function(sdk / "display.cpp", "void GFX<T>::drawCanvas(")
code = "template <typename T>\n" + code
for signature in ("void Display::presentCanvas(", "void Display::drawCanvas(", "void Display::drawSystemOverlay(", "bool Display::prepareSystemOverlay(",
                  "void Display::presentCanvasOutsideOverlay(", "void Display::clearOutsideOverlay(",
                  "void Display::finishSystemOverlay(", "void Display::drawCanvasInterlaced(",
                  "bool Display::systemOverlayNeedsTransfer("):
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
    # Extract verbatim the maintained 6979-byte regular font, rather than
    # compiling every unrelated font in the dependency's multi-megabyte file.
    fonts = (u8g2 / "u8g2_fonts.c").read_text(encoding="latin1")
    start = fonts.index("const uint8_t u8g2_font_10x20_t_cyrillic[")
    asset = fonts[start:fonts.index('";', start) + 2]
    (tmp / "font.c").write_text('#include "clib/u8g2.h"\n' + asset)
    for flags in ([], ["-fsanitize=address", "-fno-pie", "-no-pie"],
                  ["-fsanitize=undefined", "-fno-pie", "-no-pie"]):
        objects = []
        for source in [u8g2 / name for name in ("u8g2_font.c", "u8g2_hvline.c", "u8g2_intersection.c")] + [tmp / "font.c"]:
            obj = tmp / (source.stem + ".o")
            subprocess.run(["gcc", "-std=c99", "-ffunction-sections", "-fdata-sections", *flags,
                            "-I" + str(u8g2.parent), "-c", str(source), "-o", str(obj)], check=True)
            objects.append(str(obj))
        subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", *flags, "-I" + str(sdk), "-I" + str(u8g2.parent), "-Wl,--gc-sections",
                        *objects, str(tmp / "test.cpp"), "-o", str(tmp / "test")], check=True)
        subprocess.run([str(tmp / "test")], check=True)
    # Prove guard sensitivity: a background transfer through the footprint must
    # fail, even if finishSystemOverlay would leave a correct final framebuffer.
    bad_code = code.replace("void Display::presentCanvasOutsideOverlay(Canvas* canvas, int parity) {",
                            "void Display::presentCanvasOutsideOverlay(Canvas* canvas, int parity) { presentCanvas(canvas);")
    assert bad_code != code
    (tmp / "test.cpp").write_text(prelude + bad_code + checks)
    subprocess.run(["g++", "-std=c++11", "-fsanitize=undefined", "-fno-pie", "-no-pie", "-I" + str(sdk), "-I" + str(u8g2.parent), "-Wl,--gc-sections", *objects, str(tmp / "test.cpp"),
                    "-o", str(tmp / "test")], check=True)
    rejected = subprocess.run([str(tmp / "test")], capture_output=True, text=True)
    assert rejected.returncode != 0 and "value==expected" in rejected.stderr, rejected.stderr
    print("Intermediate-write mutation rejected PASS; presentation state 624 bytes, zero heap")
