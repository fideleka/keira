/* Compare real renderers, including overdraw, priorities and latch order. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nes/nes_ppu.c"
int host_external_ram = 1;
static uint8 vram[16384];
static unsigned seed = 123, latches, latch_hash;
static unsigned random_byte(void) {
    seed = seed * 1664525u + 1013904223u;
    return seed >> 24;
}
static void latch(uint32 address, uint8 tile) {
    ++latches;
    latch_hash = (latch_hash ^ address) * 16777619u;
    latch_hash = (latch_hash ^ tile) * 16777619u;
    /* Exercise mapper-side CHR mutation at the original call boundary. */
    vram[(address + tile * 16) & 16383] ^= tile;
}
int main(void) {
    for (unsigned layout = 0; layout < 4; ++layout) {
        bitmap_t* bitmap = bmp_create(256, 240, layout == 1 ? 16 : 8);
        assert(bitmap);
        for (unsigned test = 0; test < 2048; ++test) {
            memset(&ppu, 0, sizeof ppu);
            for (unsigned i = 0; i < sizeof vram; ++i)
                vram[i] = random_byte();
            for (unsigned i = 0; i < sizeof ppu.oam; ++i)
                ppu.oam[i] = random_byte();
            for (unsigned i = 0; i < sizeof ppu.palette; ++i)
                ppu.palette[i] = random_byte() & 63;
            for (unsigned i = 0; i < 16; ++i)
                ppu.page[i] = vram;
            ppu.palette[0] |= BG_TRANS;
            ppu.bg_on = !!(test & 1);
            ppu.obj_on = !!(test & 2);
            ppu.bg_mask = !!(test & 4);
            ppu.obj_mask = !!(test & 8);
            ppu.drawsprites = !!(test & 16);
            ppu.obj_height = test & 32 ? 16 : 8;
            ppu.obj_base = test & 64 ? 4096 : 0;
            ppu.bg_base = test & 128 ? 4096 : 0;
            ppu.strikeflag = !!(test & 256);
            ppu.tile_xofs = (test >> 1) & 7;
            ppu.vaddr = (random_byte() << 8 | random_byte()) & 0x7fff;
            ppu.vaddr_latch = (random_byte() << 8 | random_byte()) & 0x7fff;
            ppu.latchfunc = test & 512 ? latch : NULL;
            latches = 0;
            latch_hash = 2166136261u;
            const int scanline = test % 240;
            /* Force sprites at both edges and sprite-zero on the active line. */
            ppu.oam[0] = scanline ? scanline - 1 : 255;
            ppu.oam[3] = test & 1 ? 255 : 0;
            for (unsigned i = 0; i < bitmap->pitch * bitmap->height; ++i)
                bitmap->data[i] = random_byte();
            bitmap->hardware = layout == 2;
            host_external_ram = layout != 3;
            ppu_renderscanline(bitmap, scanline, !(test & 1024));
            unsigned hash = 2166136261u;
            for (unsigned i = 0; i < bitmap->pitch * bitmap->height; ++i)
                hash = (hash ^ bitmap->data[i]) * 16777619u;
            printf(
                "%u %u %08x %04x %02x %d %u %u %08x\n",
                layout,
                test,
                hash,
                ppu.vaddr,
                ppu.stat,
                ppu.strike_cycle,
                latches,
                ppu.strikeflag,
                latch_hash
            );
        }
        bitmap->hardware = false;
        bmp_destroy(&bitmap);
    }
}
