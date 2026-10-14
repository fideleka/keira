"""Production GB/GBC mixer + PCM writer against a consumption-clock I2S boundary."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / 'lib/Gnuboy/src'

def method(path, signature):
    source = path.read_text()
    a = source.index(signature)
    b = source.index('{', a)
    depth, end = 1, b + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[a:end]

mixer = r'''
#include <assert.h>
#include <string.h>
#include "gnuboy.h"
#include "hw.h"
#include "sound.h"
gb_t GB; gb_cart_t cart;
static int16_t pcm[1280];
void setup_mixer(int color) {
 memset(&GB,0,sizeof GB);GB.hwtype=color?GB_HW_CGB:GB_HW_DMG;
 GB.audio.samplerate=32000;GB.audio.format=GB_AUDIO_STEREO_S16;
 GB.audio.buffer=pcm;GB.audio.len=1280;GB.snd=gb_sound_init();
 gb_sound_reset(true);assert(GB.snd->rate==66);
}
size_t mix_frame(int16_t* output) {
 GB.audio.pos=0;gb_sound_advance(35112);gb_sound_emulate();
 assert(GB.audio.pos==1064||GB.audio.pos==1066);
 memcpy(output,pcm,GB.audio.pos*sizeof(int16_t));return GB.audio.pos/2;
}
'''
pre = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>
#include <algorithm>
extern "C" {void setup_mixer(int);size_t mix_frame(int16_t*);uint32_t gbcore_audio_sample_rate(void);}
constexpr int ESP_OK=0;using esp_err_t=int;using TickType_t=uint32_t;
#define pdMS_TO_TICKS(x) (x)
uint64_t clockUs=0;double queued=0;constexpr double capacity=6*256;
int rate=0,limit=0,error=0,zero=0,unaligned=0,oversize=0,slow=0,gains=0,generated=0;
std::vector<int16_t> output;
int64_t esp_timer_get_time(){return clockUs;}
void advance(uint64_t us){clockUs+=us;queued=std::max(0.0,queued-us*rate/1000000);}
void vTaskDelay(unsigned ticks){advance(ticks*1000);}
namespace lilka {struct {
 int getVolume(){return 100;}
 void adjustVolume(void*,size_t,int,int){++gains;}
} audio;}
namespace esp_i2s {
constexpr int I2S_NUM_0=0;
int i2s_write(int,const void*p,size_t bytes,size_t*w,unsigned timeout){
 if(slow){advance(std::min(slow,int(timeout*1000)));if(slow>int(timeout*1000)){*w=0;return 1;}}
 if(error){*w=0;return 1;}if(zero){*w=0;return 0;}if(unaligned){*w=2;return 0;}if(oversize){*w=bytes+4;return 0;}
 *w=limit?std::min(bytes,size_t(limit)):bytes;double frames=*w/4;
 if(queued+frames>capacity)advance(uint64_t((queued+frames-capacity)*1000000/rate)+1);
 queued+=frames;auto*s=static_cast<const int16_t*>(p);output.insert(output.end(),s,s+*w/2);return 0;
}}
size_t gbcore_audio_sample_frames(){return 640;}
bool serialPcm=false;
size_t gbcore_render_audio(void*,int16_t*data){
 ++generated;if(!serialPcm)return mix_frame(data);
 for(int i=0;i<1064;++i){data[i]=int16_t(i);}
 return 532;
}
struct GameBoyApp {
 int16_t storage[1280];int16_t*audioFrame=storage;void*core=nullptr;bool audioReady=true;
 void stopAudio(){audioReady=false;}bool writeAudio();
};
'''
checks = r'''
int main(){
 rate=gbcore_audio_sample_rate();assert(rate==31775);
 for(int color=0;color<2;++color){
  setup_mixer(color);clockUs=0;queued=capacity;GameBoyApp gb;int64_t nextFrameAt=0;
  for(int frame=0;frame<6000;++frame){
   // Core/video work with occasional slower rendered frames; skips recover slack.
   advance(frame%3==0?18000:5000);assert(queued>0);
   const bool fed=gb.writeAudio();assert(fed);pace(fed,nextFrameAt);assert(queued>0);
  }
  assert(clockUs>100000000&&clockUs<101000000);
 }
 // Negative control: original 32000 Hz drains this mixer at true GB frame timing.
 double old=capacity;
 for(int frame=0;frame<6000;++frame)old=std::max(0.0,old+35112.0/66-32000.0*35112/2097152);
 assert(old==0);
 serialPcm=true;limit=12;output.clear();GameBoyApp gb;int before=generated,volume=gains;
 assert(gb.writeAudio());assert(generated==before+1&&gains==volume+1&&output.size()==1064);
 for(int i=0;i<1064;++i)assert(output[i]==int16_t(i));
 limit=0;
 for(int fault=0;fault<4;++fault){
  GameBoyApp bad;error=fault==0;zero=fault==1;unaligned=fault==2;oversize=fault==3;
  assert(!bad.writeAudio()&&!bad.audioReady);
 }
 error=zero=unaligned=oversize=0;slow=15000;limit=4;auto at=clockUs;GameBoyApp bad;
 assert(!bad.writeAudio()&&!bad.audioReady&&clockUs-at<=51000);
 GameBoyApp absent;absent.audioReady=false;before=generated;assert(!absent.writeAudio()&&generated==before);
 puts("PASS: real DMG/CGB mixer + production PCM writer >100s, matched I2S clock, partial order, one gain/batch, bounded faults");
}
'''
with tempfile.TemporaryDirectory(prefix='keira-gb-audio-') as directory:
    p = Path(directory)
    (p / 'mixer.c').write_text(mixer + method(ROOT / 'src/apps/gameboy/gbcore.c', 'uint32_t gbcore_audio_sample_rate('))
    app = (ROOT / 'src/apps/gameboy/gameboyapp.cpp').read_text()
    begin = app.index('        nextFrameAt += kFrameUs;')
    end = app.index('\n    }\n\n    stopAudio();', begin)
    pace = '\nvoid pace(bool audioFed,int64_t& nextFrameAt){constexpr int64_t kFrameUs=16743;\n' + app[begin:end] + '\n}\n'
    (p / 'writer.cpp').write_text(pre + method(ROOT / 'src/apps/gameboy/gameboyapp.cpp', 'bool GameBoyApp::writeAudio(') + pace + checks)
    flags = ['-g', '-fsanitize=address,undefined', '-fno-pie', '-no-pie']
    objects = []
    for source in [p / 'mixer.c', CORE / 'sound.c']:
        obj = p / (source.stem + '.o')
        subprocess.run(['gcc', '-std=gnu11', *flags, '-I' + str(CORE), '-c', str(source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    subprocess.run(['g++', '-std=c++11', '-Wall', '-Wextra', '-Werror', *flags,
                    str(p / 'writer.cpp'), *objects, '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
