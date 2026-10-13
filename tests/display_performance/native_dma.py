"""Execute SDK DMA implementation with wire-completion/Arduino bus boundaries.
Not a hardware/register timing simulation: physical SD/audio/LCD soak is required.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
SDK=ROOT.parent/'sdk/lib/lilka/src/lilka'
mock=r'''
#pragma once
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <map>
#include <stdexcept>
#define ESP_OK 0
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_DMA 2
#define ETS_SPI3_INTR_SOURCE 7
#define GDMA_CHANNEL_DIRECTION_TX 1
#define LILKA_DISPLAY_DC 14
#define LILKA_DISPLAY_CS 12
#define LILKA_SPI_SCK 11
#define LILKA_SPI_MOSI 10
#define LILKA_SPI_MISO 9
#define pdFALSE 0
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
#define portMAX_DELAY 99999
#define portYIELD_FROM_ISR() do{}while(0)
using BaseType_t=int;
using SemaphoreHandle_t=int*;using intr_handle_t=void*;using gdma_channel_handle_t=void*;
struct gdma_channel_alloc_config_t{int direction;};struct gdma_trigger_t{int instance;};
#define GDMA_MAKE_TRIGGER(periph,id) gdma_trigger_t{id}
struct lldesc_t{unsigned size=0,length=0,owner=0,eof=0;volatile uint8_t*buf=nullptr;};
struct Pulse{bool value=false;Pulse&operator=(int v){value=v;return *this;}operator bool(){value=false;return false;}};
struct Hardware{
 struct{Pulse update;int usr=0;}cmd;
 union{uint32_t val;struct{uint32_t dma_rx_ena:1,dma_tx_ena:1,dma_afifo_rst:1;};}dma_conf{};
 union{uint32_t val;struct{uint32_t usr_mosi_highpart:1,usr_miso:1,usr_mosi:1;};}user{};
 union{uint32_t val;struct{uint32_t ms_data_bitlen:18;};}ms_dlen{};
 struct{int trans_done=0;}dma_int_raw,dma_int_ena,dma_int_clr;
}GPSPI3;
inline bool locked=false,failMemory=false,failChannel=false,failConnect=false,failIRQ=false,timeout=false,failStart=false;
inline int cpuCalls=0,starts=0,stops=0,resets=0,connects=0,channels=0,semaphores=0,interrupts=0;
inline std::map<void*,size_t>allocations;
inline void*heap_caps_malloc(size_t size,int caps){assert(caps==3);if(failMemory)return nullptr;void*p=malloc(size);allocations[p]=size;return p;}
inline void heap_caps_free(void*p){assert(allocations.erase(p));free(p);}
inline void* xTaskGetCurrentTaskHandle(){return (void*)1;}
inline void vTaskDelay(unsigned){throw std::runtime_error("delay");}
inline void esp_restart(){throw std::runtime_error("restart");}
inline void(*irq)(void*)=nullptr;inline void*irqContext=nullptr;inline lldesc_t*active=nullptr;
inline std::vector<uint8_t>wire,submitted;
inline SemaphoreHandle_t xSemaphoreCreateBinary(){++semaphores;return new int(0);}
inline void vSemaphoreDelete(SemaphoreHandle_t p){--semaphores;delete p;}
inline void xSemaphoreGiveFromISR(SemaphoreHandle_t p,BaseType_t*){++*p;}
inline int xSemaphoreTake(SemaphoreHandle_t p,unsigned ticks){assert(ticks==100);assert(locked);if(timeout)return 0;
 assert(active&&GPSPI3.cmd.usr&&GPSPI3.dma_int_ena.trans_done);assert(!*p);
 assert(!memcmp((void*)active->buf,submitted.data(),submitted.size()));
 assert(GPSPI3.ms_dlen.ms_data_bitlen+1==active->length*8);
 wire.insert(wire.end(),submitted.begin(),submitted.end());GPSPI3.cmd.usr=0;GPSPI3.dma_int_raw.trans_done=1;irq(irqContext);GPSPI3.dma_int_raw.trans_done=0;
 assert(*p==1);--*p;return 1;}
inline int gdma_new_channel(gdma_channel_alloc_config_t*,gdma_channel_handle_t*p){if(failChannel)return -1;++channels;*p=(void*)2;return 0;}
inline int gdma_connect(void*,gdma_trigger_t trigger){assert(trigger.instance==3);if(failConnect)return -1;++connects;return 0;}
inline int gdma_disconnect(void*){--connects;return 0;}
inline int gdma_del_channel(void*){--channels;return 0;}
inline int gdma_reset(void*){assert(!GPSPI3.cmd.usr);++resets;return 0;}
inline int gdma_start(void*,intptr_t address){if(failStart)return -1;active=(lldesc_t*)address;
 assert(active->owner==1&&active->eof==1&&active->length<=4092&&!(active->size&3));
 submitted.assign((const uint8_t*)active->buf,(const uint8_t*)active->buf+active->length);++starts;return 0;}
inline int gdma_stop(void*){assert(!GPSPI3.cmd.usr);++stops;return 0;}
inline int esp_intr_alloc(int source,int flags,void(*f)(void*),void*c,intr_handle_t*p){assert(source==7&&!flags);if(failIRQ)return -1;irq=f;irqContext=c;*p=(void*)3;++interrupts;return 0;}
class Arduino_HWSPI{public:template<typename...T>Arduino_HWSPI(T...){}virtual void beginWrite(){assert(!locked);locked=true;}virtual void endWrite(){assert(locked&&!GPSPI3.cmd.usr);locked=false;}virtual void writePixels(uint16_t*,uint32_t){++cpuCalls;}};
namespace lilka{inline int SPI1;struct Serial{template<typename...T>void err(T...){}};inline Serial serial;}
'''
checks=r'''
#include "lilka/pixel_dma.cpp"
int main(){
 for(int kind=0;kind<4;++kind){lilka::PixelDMABus bus;failMemory=kind==0;failChannel=kind==1;failConnect=kind==2;failIRQ=kind==3;assert(!bus.enablePixelDMA());assert(allocations.empty()&&!channels&&!connects&&!semaphores);}
 failMemory=failChannel=failConnect=failIRQ=false;
 lilka::PixelDMABus bus;assert(bus.enablePixelDMA());assert(bus.enablePixelDMA());assert(channels==1&&interrupts==1&&allocations.size()==1);
 std::vector<uint16_t>pixels(67200);for(unsigned i=0;i<pixels.size();++i)pixels[i]=i*997;
 for(int offset: {0,1})for(int count: {0,1,63,64,65,2045,2046,2047,4093,67199}){
 wire.clear();bus.beginWrite();GPSPI3.dma_conf.val=0x90;GPSPI3.user.val=0x12;GPSPI3.ms_dlen.val=7;
 bus.writePixels(pixels.data()+offset,count);assert(GPSPI3.dma_conf.val==0x90&&GPSPI3.user.val==0x12&&GPSPI3.ms_dlen.val==7);
 assert(!GPSPI3.cmd.usr&&!GPSPI3.dma_int_ena.trans_done);bus.endWrite();
 if(count>=64){assert(wire.size()==count*2);for(int i=0;i<count;++i){assert(wire[2*i]==(pixels[offset+i]>>8));assert(wire[2*i+1]==(pixels[offset+i]&255));}}
 }
 assert(starts==stops&&starts==resets);assert(cpuCalls==6);
 timeout=true;bus.beginWrite();try{bus.writePixels(pixels.data(),2047);assert(false);}catch(const std::runtime_error&e){assert(!strcmp(e.what(),"restart"));}
 assert(locked&&GPSPI3.cmd.usr&&allocations.size()==1); // fail closed, no release/reuse
 puts("PASS: native DMA packing/chunk tails, wire-end waits, shared bus lock/register restore, init rollback and active timeout fail-closed");
}
'''
with tempfile.TemporaryDirectory() as t:
    t=Path(t)
    (t/'mock.h').write_text(mock)
    for name in ['Arduino_GFX_Library.h','lilka/spi.h','lilka/serial.h','esp_heap_caps.h','esp_intr_alloc.h','esp_system.h','esp_private/gdma.h','freertos/FreeRTOS.h','freertos/semphr.h','soc/spi_struct.h','soc/lldesc.h','soc/interrupts.h']:
        p=t/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('#include "mock.h"\n')
    (t/'lilka/config.h').write_text('#define LILKA_VERSION 2\n#define CONFIG_IDF_TARGET_ESP32S3 1\n')
    for name in ['pixel_dma.cpp','pixel_dma.h','rgb565_pack.h']:(t/'lilka'/name).write_text((SDK/name).read_text())
    (t/'test.cpp').write_text(checks)
    subprocess.run(['g++','-std=c++17','-O2','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(t),str(t/'test.cpp'),'-o',str(t/'test')],check=True)
    import os
    subprocess.run([str(t/'test')],env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'},check=True)
