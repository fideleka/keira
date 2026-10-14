"""Exact production RGB565 kernel parity for alignment, short rows and tails."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
SOURCE = r'''
#include "apps/nes/pixel_convert.h"
#include <vector>
#include <cassert>
#include <cstdio>
int main() {
 unsigned cases=0;
 uint16_t palette[256];for(unsigned i=0;i<256;++i)palette[i]=i*239u;
 for(unsigned n=0;n<=320;++n)for(unsigned outOffset=0;outOffset<2;++outOffset)
  for(unsigned sourceOffset=0;sourceOffset<2;++sourceOffset)for(unsigned seed=0;seed<8;++seed){
   std::vector<uint8_t> input(n+sourceOffset+1);
   std::vector<uint16_t> actual(n+outOffset+2,0xbeef),expected=actual;
   for(unsigned i=0;i<input.size();++i)input[i]=(i*31u+seed*47u)&255;
   for(unsigned i=0;i<n;++i)expected[outOffset+i]=palette[input[sourceOffset+i]];
   nesPixels::convertRow(actual.data()+outOffset,input.data()+sourceOffset,palette,n);
   assert(actual==expected);++cases;
  }
 printf("PASS: %u RGB565 kernel cases, all widths 0..320, word/halfword alignment, odd/short tails and guards\n",cases);
}
'''
with tempfile.TemporaryDirectory(prefix='nes-pixel-parity-') as tmp:
    p=Path(tmp);(p/'test.cpp').write_text(SOURCE)
    subprocess.run(['g++','-std=c++11','-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    '-fno-pie','-no-pie','-I'+str(ROOT/'src'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
