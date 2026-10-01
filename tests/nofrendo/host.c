#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "nes/nes.h"
#include "nes/nesinput.h"
#include "osd.h"
#include "gui.h"
#include "nofrendo.h"
static bitmap_t* screen;
static rgb_t pal[256];
rgb_t gui_pal[GUI_TOTALCOLORS];
volatile int nofrendo_ticks;
static int frame, maxframes;
static nesinput_t pad = {INP_JOYPAD0, 0};
void* _my_malloc(int n) {
    return malloc(n);
}
void* mem_alloc(int n, bool fast) {
    return malloc(n);
}
void _my_free(void** p) {
    free(*p);
    *p = NULL;
}
char* _my_strdup(const char* s) {
    return strdup(s);
}
int nofrendo_log_printf(const char* f, ...) {
    va_list a;
    va_start(a, f);
    int n = vfprintf(stderr, f, a);
    va_end(a);
    return n;
}
int nofrendo_log_print(const char* s) {
    return fputs(s, stderr);
}
void gui_sendmsg(int c, char* f, ...) {
    va_list a;
    va_start(a, f);
    vfprintf(stderr, f, a);
    fputc('\n', stderr);
    va_end(a);
}
void gui_frame(bool d) {
}
void gui_tick(int t) {
}
static void (*audio_process)(void*, int);
void osd_setsound(void (*f)(void*, int)) {
    audio_process = f;
}
void osd_getsoundinfo(sndinfo_t* i) {
    i->sample_rate = 44100;
    i->bps = 16;
}
void osd_fullname(char* d, const char* s) {
    strcpy(d, s);
}
char* osd_newextension(char* s, char* e) {
    char* p = strrchr(s, '.');
    if (!p) p = s + strlen(s);
    strcpy(p, e);
    return s;
}
bitmap_t* vid_getbuffer(void) {
    return screen;
}
void vid_setpalette(rgb_t* p) {
    memcpy(pal, p, sizeof pal);
}
void vid_flush(void) {
    frame++;
    nes_t n;
    nes_getcontext(&n);
    unsigned counts[256] = {0};
    unsigned hash = 2166136261u;
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < 256; x++) {
            unsigned v = screen->line[y][x];
            counts[v]++;
            hash = (hash ^ v) * 16777619u;
        }
    int colors = 0, dominant = 0;
    for (int i = 0; i < 256; i++) {
        colors += counts[i] != 0;
        if (counts[i] > counts[dominant]) dominant = i;
    }
    if (frame <= 10 || frame % 60 == 0 || frame == maxframes)
        printf(
            "frame=%d pc=%04x cycles=%d jam=%d ctrl=%02x/%02x stat=%02x vaddr=%04x colors=%d dominant=%02x count=%u "
            "rgb=%d,%d,%d hash=%08x pal0=%02x\n",
            frame,
            n.cpu->pc_reg,
            n.cpu->total_cycles,
            n.cpu->jammed,
            n.ppu->ctrl0,
            n.ppu->ctrl1,
            n.ppu->stat,
            n.ppu->vaddr,
            colors,
            dominant,
            counts[dominant],
            pal[dominant].r,
            pal[dominant].g,
            pal[dominant].b,
            hash,
            n.ppu->palette[0]
        );
}
void osd_getinput(void) {
    short samples[735];
    if (audio_process) audio_process(samples, 735);
    pad.data = ((frame >= 300 && frame < 310) || (frame >= 600 && frame < 610) || (frame >= 900 && frame < 910))
                   ? INP_PAD_START
                   : 0;
    if (frame >= 1200 && frame < 1500) pad.data = INP_PAD_RIGHT | INP_PAD_A;
    if (frame >= maxframes) nes_poweroff();
}
#ifndef IRQ_UNIT_TEST
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    maxframes = argc > 2 ? atoi(argv[2]) : 1800;
    setvbuf(stdout, NULL, _IOLBF, 0);
    screen = bmp_create(256, 240, 16);
    if (!screen) return 3;
    input_register(&pad);
    nes_t* n = nes_create();
    if (!n || nes_insertcart(argv[1], n)) return 4;
    printf(
        "mapper=%d prg16k=%d chr8k=%d resetpc=%04x\n",
        n->rominfo->mapper_number,
        n->rominfo->rom_banks,
        n->rominfo->vrom_banks,
        nes_getcontextptr()->cpu->pc_reg
    );
    nes_getcontextptr()->autoframeskip = false;
    nes_emulate();
    return 0;
}

#endif
