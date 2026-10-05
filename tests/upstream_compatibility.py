"""Execute production toolbar submission: literal labels, locked canvas, no LCD access."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
s = (root / 'src/keira/app.cpp').read_text()
start = s.index('void App::queueDraw()')
body = s[start:]
assert body.count('KMTX_LOCK(canvasMutex)') == 1
assert 'display.' not in body
pre = r'''
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <string>
using String=std::string;
int depth=0;
#define KMTX_LOCK(x) assert(depth++==0)
#define KMTX_UNLOCK(x) assert(--depth==0)
#define KAPP_DBG if(false)
#define FONT_8x13 1
#define TOOL_BAR_HEIGHT 30
#define TOOL_BAR_SAFE_DISTANCE 38
#define TOOL_BAR_WIDTH (canvas->width()-76)
void taskYIELD(){assert(depth==0);}
namespace lilka {struct {void log(const char*,...){}} serial;}
struct Canvas {
 String text; int writes=0;
 int width(){return 240;} int height(){return 280;}
 void fillRect(int,int,int,int,int){assert(depth==1);++writes;}
 void setCursor(int,int){} void setFont(int){} void setTextColor(int){}
 void setTextBound(int,int,int,int){}
 void printf(const char* format,...){assert(depth==1);assert(String(format)=="%s");
 va_list args;va_start(args,format);text=va_arg(args,const char*);va_end(args);}
};
struct App {
 Canvas a,b;Canvas* canvas=&a;Canvas* backCanvas=&b;
 int canvasMutex=0,frame=0,skippedFrames=0;bool redraw=false;
 String toolBarLabel;int toolBarColor=1,toolBarBgColor=0;
 void setRedraw(bool v){assert(depth==1);redraw=v;}void queueDraw();
};
'''
checks = r'''
int main(){App app;app.toolBarLabel="100% %s %n file";app.queueDraw();
assert(app.backCanvas==&app.a);assert(app.backCanvas->text==app.toolBarLabel);
assert(app.backCanvas->writes==1);assert(app.frame==1&&app.redraw&&depth==0);
app.toolBarLabel="";app.queueDraw();assert(app.backCanvas==&app.b);
assert(app.backCanvas->writes==0);puts("Toolbar literal label, lock-before-write and buffered submission PASS");}
'''
with tempfile.TemporaryDirectory(prefix='keira-upstream-compat-') as d:
 p=Path(d);(p/'test.cpp').write_text(pre+body+checks)
 for flags in ([],['-fsanitize=address,undefined','-fno-pie','-no-pie']):
  subprocess.run(['g++','-std=c++11',*flags,str(p/'test.cpp'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True)
