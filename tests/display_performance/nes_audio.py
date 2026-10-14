"""Execute production NES producer/task against a consumption-clock I2S boundary."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "src/apps/nes/osd.cpp").read_text()
def method(signature):
    a=source.index(signature);b=source.index('{',a);d=1;e=b+1
    while d:
        d+=(source[e]=='{')-(source[e]=='}');e+=1
    return source[a:e]
pre = r'''

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <algorithm>
#define LILKA_VERSION 2
#define pdMS_TO_TICKS(x) (x)
#define KEIRA_MEASURE 0
constexpr int HW_AUDIO_SAMPLERATE=22050,NES_REFRESH_RATE=60,DEFAULT_FRAGSIZE=64;
constexpr int ESP_OK=0;using esp_err_t=int;using TickType_t=uint32_t;
uint64_t clockUs=0;double queued=0;constexpr double capacity=7*256;
int64_t esp_timer_get_time(){return clockUs;}
std::vector<int16_t> output;int limit=0,error=0,zero=0,unaligned=0,oversize=0,slow=0;
int serial=0,generated=0,batches=0,delayCalls=0;bool paused=false;
int16_t storage[128];int16_t*audio_frame=storage;
void samples(void*p,int n){auto*s=static_cast<int16_t*>(p);for(int i=0;i<n;++i)s[i]=int16_t(serial++);generated+=n;}
void(*audio_callback)(void*,int)=samples;
namespace lilka{struct{int getVolume(){return 100;}void adjustVolume(void*,unsigned,int,int){}}audio;}
namespace esp_i2s{constexpr int I2S_NUM_0=0;}
void advance(uint64_t us){clockUs+=us;queued=std::max(0.0,queued-us*22050.0/1000000);}
int i2s_write(int,const void*p,size_t bytes,size_t*w,int timeout){
 if(slow){advance(std::min(slow,timeout*1000));if(slow>timeout*1000){*w=0;return 1;}}
 if(error){*w=0;return error;}if(zero){*w=0;return 0;}if(unaligned){*w=2;return 0;}if(oversize){*w=bytes+4;return 0;}
 *w=limit?std::min(bytes,size_t(limit)):bytes;
 double frames=*w/4;
 if(queued+frames>capacity)advance(uint64_t((queued+frames-capacity)*1000000/22050)+1);
 queued+=frames;
 auto*s=static_cast<const int16_t*>(p);output.insert(output.end(),s,s+*w/2);return 0;
}
void*xSoundMutex=nullptr,*audioStoppedSemaphore=nullptr,*audioTaskHandle=nullptr;
struct Acquire{Acquire(void*){}};
struct NesApp{bool audioPaused=false;void rememberSuccessfulLaunch(){}}app;
struct Driver{static NesApp*app;};NesApp*Driver::app=&::app;
void xSemaphoreTake(void*,int){}void xSemaphoreGive(void*){}void vTaskSuspend(void*){}
void vTaskDelay(unsigned ticks){++delayCalls;advance(ticks*1000);++batches;if(batches>=6000||app.audioPaused||error||zero)audio_frame=nullptr;}
void xTaskCreatePinnedToCore(void(*f)(void*),const char*,int,void*,int,void**,int){f(nullptr);}
'''
check = r'''

int main(){
 // The actual task/producer must sustain the I2S consumption clock for 100 s.
 osd_setsound(samples);assert(clockUs>99000000&&clockUs<101000000);
 assert(generated==6000*367&&queued>capacity-30&&delayCalls==6000);
 assert(output.size()==size_t(generated*2));
 for(size_t i=0;i<output.size();i+=2){assert(output[i]==output[i+1]);assert(output[i]==int16_t(i/2));}
 // Partial writes retain PCM order; APU data and gain are generated only once.
 audio_frame=storage;serial=generated=0;output.clear();limit=12;
 assert(do_audio_frame());assert(generated==367&&output.size()==734);
 for(unsigned i=0;i<367;++i)assert(output[i*2]==int16_t(i)&&output[i*2+1]==int16_t(i));
 limit=0;zero=1;assert(!do_audio_frame());zero=0;error=1;assert(!do_audio_frame());error=0;
 unaligned=1;assert(!do_audio_frame());unaligned=0;oversize=1;assert(!do_audio_frame());oversize=0;
 audio_callback=nullptr;assert(!do_audio_frame());audio_callback=samples;
 slow=15000;limit=4;auto faultAt=clockUs;assert(!do_audio_frame());assert(clockUs-faultAt<=51000);slow=limit=0;
 // Pause and fault use the bounded 10ms yield, not a tight failed-feed loop.
 audio_frame=storage;app.audioPaused=true;batches=delayCalls=0;auto at=clockUs;osd_setsound(samples);
 assert(delayCalls==1&&clockUs-at==10000);app.audioPaused=false;
 audio_frame=storage;error=1;batches=delayCalls=0;at=clockUs;osd_setsound(samples);
 assert(delayCalls==1&&clockUs-at==10000);
 // Negative control: fixed 60Hz * 367 samples cannot fill 22050Hz demand.
 assert(367*60==22020&&22050-367*60==30);
 double oldQueue=capacity;for(int i=0;i<6000;++i)oldQueue=std::max(0.0,oldQueue+367-367.5);
 assert(oldQueue==0);
 puts("PASS: production NES continuous I2S feed >99s, partial PCM order, failure/pause yield, stereo and no 60Hz underfeed");
}
'''
with tempfile.TemporaryDirectory() as directory:
    p=Path(directory)
    (p/'audio.cpp').write_text(pre+method('bool do_audio_frame()')+method('void osd_setsound(')+check)
    subprocess.run(['g++','-std=c++11','-O2','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-fno-pie','-no-pie',str(p/'audio.cpp'),'-o',str(p/'audio')],check=True)
    subprocess.run([str(p/'audio')],check=True)
