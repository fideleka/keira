#!/usr/bin/env python3
"""Differential check of production row kernel against exact RGB565 comparison."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
source = r'''
#include "devices/frame_row.h"
#include <cassert>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <sys/mman.h>
int main() {
    auto* page=static_cast<uint16_t*>(mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    assert(page!=MAP_FAILED);std::fill(page,page+320,0x1234);
    std::vector<uint16_t> identical(320,0x1234);
    assert(!mprotect(page,4096,PROT_READ));
    assert(!displayFrame::copyCompareRow(page,identical.data(),identical.data(),320,false,true));
    assert(!munmap(page,4096));
    // Exercise both word-aligned and halfword-only layouts, all supported widths,
    // first/last/sparse changes, identical images, force and retained-frame reuse.
    uint32_t random=12345;
    for (unsigned width=1;width<=320;++width)
    for (unsigned offset=0;offset<2;++offset)
    for (unsigned which=0;which<6;++which)
    for (bool inPlace : {false,true})
    for (bool force : {false,true})
    for (bool fresh : {false,true}) {
        std::vector<uint16_t> src(width+4,0xbeef), prior(width+4,0xbeef), dst(width+4,0xbeef);
        auto* s=src.data()+offset+2;auto* p=prior.data()+offset+2;auto* d=dst.data()+offset+2;
        for(unsigned x=0;x<width;++x){random=random*1664525+1013904223;s[x]=p[x]=uint16_t(random>>16);}
        if(which==1)s[0]^=1;
        if(which==2)s[width-1]^=0x8000;
        if(which==3)s[width/2]^=0xffff;
        if(which==4)for(unsigned x=0;x<width;x+=7)s[x]^=0x1234;
        if(which==5)for(unsigned x=0;x<width;++x)s[x]^=0xffff;
        const auto immutable=src;const auto old=prior;
        const bool changed=!std::equal(s,s+width,p);
        if(inPlace)d=p;
        else if(fresh)std::copy(p,p+width,d);
        assert(displayFrame::copyCompareRow(d,s,p,width,force,fresh)==(force||changed));
        assert(std::equal(s,s+width,d));assert(src==immutable);
        assert(d[-1]==0xbeef&&d[width]==0xbeef);
        if(!inPlace)assert(prior==old); // External lease never written.
    }
}
'''
with tempfile.TemporaryDirectory(prefix="keira-frame-row-") as directory:
    tmp=Path(directory)
    (tmp/'test.cpp').write_text(source)
    subprocess.run(['g++','-std=c++11','-O2','-fstrict-aliasing','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-I'+str(root/'src'),'-I'+str(root.parent/'sdk/lib/lilka/src'),str(tmp/'test.cpp'),'-o',str(tmp/'test')],check=True)
    subprocess.run([str(tmp/'test')],check=True)
print('PASS: 30720 exact row cases, alignment/odd widths/guards/immutable source and lease')
