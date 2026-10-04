"""Compile production GameBoyApp active-low input mapping, normal and sanitized."""
from pathlib import Path
import subprocess, tempfile
ROOT = Path(__file__).resolve().parents[1]
s = (ROOT / "src/apps/gameboy/gameboyapp.cpp").read_text()
code = s[s.index("        const int horizontal"):s.index("        gbcore_set_buttons", s.index("        const int horizontal"))]
prelude = r'''#include "apps/nes/preferences.h"
#include <cassert>
#include <cstdio>
struct Button {bool pressed=false;};
struct State {Button a,b,c,d,left,right,up,down,start;};
struct Input {
 nesmenu::Preferences preferences;
 nesmenu::DirectionFilter directionFilter, verticalFilter;
 uint8_t turboAFrame=0, turboBFrame=0;
 bool selectTap=false, startGameActive=true;
 uint32_t now=0;
 uint32_t millis() {return now;}
 uint8_t sample(const State& state) {
'''
checks = r'''return buttons;
 }
};
int main() {
 Input input; State s;
 assert(input.sample(s)==0xff);
 s.a.pressed=s.b.pressed=true; assert((input.sample(s)&3)==0);
 s=State(); s.c.pressed=s.d.pressed=true;
 for(int i=0;i<12;++i) assert((input.sample(s)&3)==(i%4<2?0:3));
 s.select.pressed=true; assert((input.sample(s)&3)==3);
 s=State(); input.preferences.precisionMode=true;
 for(int d=0;d<4;++d) {
  input.directionFilter.reset(); input.verticalFilter.reset(); s=State();
  Button* dirs[]={&s.up,&s.down,&s.left,&s.right};
  const int masks[]={0x40,0x80,0x20,0x10}; dirs[d]->pressed=true;
  input.now=UINT32_MAX-100; assert(!(input.sample(s)&masks[d]));
  input.now=148; assert(input.sample(s)&masks[d]);
  input.now=149; assert(!(input.sample(s)&masks[d]));
  dirs[d]->pressed=false; input.sample(s); dirs[d]->pressed=true;
  assert(!(input.sample(s)&masks[d]));
 }
 input.directionFilter.reset();input.verticalFilter.reset();s=State();
 s.left.pressed=s.up.pressed=true;input.now=0;assert((input.sample(s)&0x60)==0);
 input.now=1;assert((input.sample(s)&0x60)==0x60);
 s.left.pressed=false;s.right.pressed=true;assert(!(input.sample(s)&0x10));
 assert(input.sample(s)&0x40); // horizontal reversal independent of vertical
 s.left.pressed=true;assert((input.sample(s)&0x30)==0x30);
 s.left.pressed=false;assert(!(input.sample(s)&0x10));
 input.directionFilter.reset();input.verticalFilter.reset();assert(!(input.sample(s)&0x50));
 input.preferences.precisionMode=false;s.left.pressed=true;s.down.pressed=true;
 assert((input.sample(s)&0xf0)==0); // OFF preserves even opposite inputs
 puts("Actual GB input four directions/diagonal/reversal/opposites/wrap/modal/turbo PASS");
}
'''
# Select field is part of the real controller boundary.
prelude=prelude.replace("Button a,b,c,d,", "Button select,a,b,c,d,")
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp); (p/"test.cpp").write_text(prelude+code+checks)
 for flags in [[], ["-fsanitize=address,undefined","-fno-pie","-no-pie"]]:
  subprocess.run(["g++","-std=c++11","-Wall","-Wextra","-Werror",*flags,"-I"+str(ROOT/"src"),str(p/"test.cpp"),"-o",str(p/"test")],check=True)
  subprocess.run([str(p/"test")],check=True)
