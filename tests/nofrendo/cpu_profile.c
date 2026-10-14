/* Real frame sequencer with a deterministic clock boundary, not a mock core. */
#include "nes/nes.c"
#include <assert.h>
#include <stdio.h>
extern void host_set_screen(bitmap_t* bitmap);
int host_external_ram = 1;
static unsigned clock_calls, frame_samples, sampled_cpu, sampled_ppu;
static int enabled = 1;
static unsigned bg_us, sprites_us, ppu_parts;
uint64_t osd_measure_clock(void) {
    return ++clock_calls;
}
int osd_measure_core_sample(int draw) {
    return enabled;
}
void osd_measure_core(int draw, uint64_t cpu_us, uint64_t ppu_us) {
    ++frame_samples;
    sampled_cpu = cpu_us;
    sampled_ppu = ppu_us;
}
void osd_measure_ppu_parts(uint64_t bg, uint64_t sprites) {
    bg_us = bg;
    sprites_us = sprites;
    ++ppu_parts;
}
void osd_measure_frame(int draw, uint64_t us) {
}
void osd_measure_core_memory(void) {
}
void osd_measure_report(void) {
}
int main(void) {
    static uint8 ram[65536];
    static mapintf_t mapper;
    static mmc_t mmc;
    static nes6502_memread reads[] = {{(uint32)-1, (uint32)-1, NULL}};
    static nes6502_memwrite writes[] = {{(uint32)-1, (uint32)-1, NULL}};
    nes_t* machine = nes_create();
    assert(machine);
    bitmap_t* framebuffer = bmp_create(256, 240, 8);
    assert(framebuffer);
    host_set_screen(framebuffer);
    nes = *machine;
    nes.mmc = &mmc;
    mmc.intf = &mapper;
    memset(ram, 0xea, sizeof ram);
    ram[0x8000] = 0x4c; /* JMP $8000: do not run off synthetic ROM. */
    ram[0x8001] = 0x00;
    ram[0x8002] = 0x80;
    nes6502_context cpu = {0};
    for (int i = 0; i < NES6502_NUMBANKS; ++i)
        cpu.mem_page[i] = ram + i * NES6502_BANKSIZE;
    cpu.pc_reg = 0x8000;
    cpu.p_reg = I_FLAG | R_FLAG;
    cpu.s_reg = 0xff;
    cpu.read_handler = reads;
    cpu.write_handler = writes;
    nes6502_setcontext(&cpu);
    ppu_setcontext(machine->ppu);
    nes.scanline = 0;
    nes_renderframe(true);
    assert(frame_samples == 1 && sampled_cpu == 263 && sampled_ppu == 1222 && clock_calls == 2010);
    assert(ppu_parts == 1 && bg_us == 240 && sprites_us == 240);
    nes_renderframe(false);
    assert(frame_samples == 2 && sampled_cpu == 263 && sampled_ppu == 262 && clock_calls == 3060);
    enabled = 0;
    nes_renderframe(false);
    assert(frame_samples == 2 && clock_calls == 3060); /* unsampled: zero clock calls */
    puts("PASS: real 262-line CPU/PPU sampling, draw/skip and zero-clock unsampled path");
}
