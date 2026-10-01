/* Source-only tests: real core, synthetic RAM/opcodes; no cartridge bytes. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
/* Exposes the real frame sequencer locally, not a production test hook. */
#include "nes/nes.c"

extern mapintf_t map4_intf;
static uint8 memory[65536];
static nes6502_context context;
static nes6502_memread reads[] = {{(uint32)-1, (uint32)-1, NULL}};
static void irq_ack(uint32 address, uint8 value) {
    map4_intf.mem_write[0].write_func(address, value);
}
static nes6502_memwrite writes[] = {{0xe000, 0xe000, irq_ack}, {(uint32)-1, (uint32)-1, NULL}};

static uint8 sources(void) {
    nes6502_getcontext(&context);
    return context.irq_sources;
}

static void fixture(void) {
    memset(memory, 0xea, sizeof(memory));
    memory[0x10] = 0;
    memory[0x8000] = 0x58; /* CLI */
    memory[0x8001] = 0x4c;
    memory[0x8002] = 1;
    memory[0x8003] = 0x80;
    memory[0x9000] = 0xe6;
    memory[0x9001] = 0x10; /* INC $10 */
    memory[0x9002] = 0x40; /* RTI */
    memory[0xfffe] = 0;
    memory[0xffff] = 0x90;
    memset(&context, 0, sizeof(context));
    for (int i = 0; i < NES6502_NUMBANKS; i++)
        context.mem_page[i] = memory + i * NES6502_BANKSIZE;
    context.pc_reg = 0x8000;
    context.s_reg = 0xff;
    context.p_reg = R_FLAG | I_FLAG;
    context.read_handler = reads;
    context.write_handler = writes;
    nes6502_setcontext(&context);
    nes.fiq_occurred = false;
    nes_setfiq(0);
}

static void assert_frame(void) {
    nes_checkfiq((int)NES_FIQ_PERIOD);
    assert(nes.fiq_occurred && (sources() & NES6502_IRQ_FRAME));
}

static void assert_dmc(void) {
    apu_write(0x4010, 0x8f);
    apu_write(0x4012, 0);
    apu_write(0x4013, 0); /* one-byte sample from synthetic RAM */
    apu_write(0x4015, 0x10);
    short samples[735];
    apu_process(samples, 735);
    assert((apu_read(0x4015) & 0x80) && (sources() & NES6502_IRQ_DMC));
}

static void mapper_write(uint32 address, uint8 value) {
    map4_intf.mem_write[0].write_func(address, value);
}

int main(void) {
    nes_t* machine = nes_create();
    assert(machine);
    nes = *machine;
    apu_setcontext(machine->apu);
    ppu_setcontext(machine->ppu);

    fixture();
    assert_frame();
    nes_setfiq(0x40);
    assert(!nes.fiq_occurred && sources() == 0);
    nes6502_execute(60);
    assert(memory[0x10] == 0); /* stale IRQ cannot fire on CLI */
    assert(!(apu_read(0x4015) & 0x40));
    nes_checkfiq((int)NES_FIQ_PERIOD * 2);
    assert(sources() == 0);

    fixture();
    assert_frame();
    nes_setfiq(0x80); /* five-step mode doesn't acknowledge an existing flag */
    assert(nes.fiq_occurred && sources() == NES6502_IRQ_FRAME);
    assert(apu_read(0x4015) & 0x40);
    assert(!(apu_read(0x4015) & 0x40));
    assert(sources() == 0);
    nes6502_execute(60);
    assert(memory[0x10] == 0);

    fixture();
    assert_frame();
    nes6502_getcontext(&context);
    context.pc_reg = 0x8001;
    nes6502_setcontext(&context);
    nes6502_execute(60);
    assert(memory[0x10] == 0); /* masked */
    nes6502_getcontext(&context);
    context.pc_reg = 0x8000;
    nes6502_setcontext(&context);
    nes6502_execute(60);
    assert(memory[0x10] > 1); /* still asserted after RTI */
    assert(sources() == NES6502_IRQ_FRAME);
    apu_read(0x4015);
    unsigned count = memory[0x10];
    nes6502_execute(60);
    assert(memory[0x10] <= count + 1); /* at most already-entered handler */

    fixture();
    assert_dmc();
    assert_frame();
    nes6502_setirq(NES6502_IRQ_MAPPER, true);
    assert(sources() == 7);
    assert(apu_read(0x4015) == 0xC0);
    assert(sources() == 6);
    assert(apu_read(0x4015) & 0x80);
    assert(sources() == 6);
    assert_frame();
    nes_setfiq(0x40);
    assert(sources() == 6);
    mapper_write(0xe000, 0);
    assert(sources() == NES6502_IRQ_DMC);
    nes6502_execute(60);
    assert(memory[0x10] > 1);
    apu_write(0x4010, 0);
    assert(sources() == 0);

    fixture();
    assert_dmc();
    assert_frame();
    nes6502_setirq(NES6502_IRQ_MAPPER, true);
    apu_write(0x4015, 0);
    assert(sources() == 5); /* only DMC ack */
    nes_setfiq(0x40);
    assert(sources() == 4);
    nes6502_execute(60);
    assert(memory[0x10] > 1);
    mapper_write(0xe000, 0);
    assert(sources() == 0);

    fixture();
    map4_intf.init();
    ppu_write(0x2001, 0x18);
    mapper_write(0xc000, 0);
    mapper_write(0xc001, 0);
    mapper_write(0xe001, 0);
    map4_intf.hblank(0);
    assert(sources() == NES6502_IRQ_MAPPER); /* actual MMC3 assertion */
    assert_frame();
    assert_dmc(); /* DMC status read only acknowledges frame */
    assert(sources() == 6);
    assert_frame();
    assert(sources() == 7);
    mapper_write(0xe000, 0);
    assert(sources() == 3); /* E000 must leave frame + DMC asserted */
    assert(apu_read(0x4015) == 0xc0);
    assert(sources() == 2);
    apu_write(0x4015, 0);
    assert(sources() == 0);

    fixture();
    assert_frame();
    nes6502_irq(); /* unrelated legacy mapper pulse */
    nes_setfiq(0x40);
    nes6502_getcontext(&context);
    assert(context.int_pending == 1 && context.irq_sources == 0);
    nes6502_execute(60);
    assert(memory[0x10] == 1);

    fixture();
    nes6502_setirq(NES6502_IRQ_MAPPER, true);
    memory[0x9002] = 0xa9;
    memory[0x9003] = 0; /* LDA #0 */
    memory[0x9004] = 0x8d;
    memory[0x9005] = 0;
    memory[0x9006] = 0xe0; /* STA E000 */
    memory[0x9007] = 0x40; /* RTI */
    nes6502_execute(100);
    assert(memory[0x10] == 1 && sources() == 0); /* in-handler ack keeps live PC intact */

    fixture();
    nes6502_setirq(NES6502_IRQ_DMC, true);
    memory[0x8000] = 0x28; /* PLP unmask at a real instruction boundary */
    memory[0x100] = R_FLAG;
    context.s_reg = 0xff;
    nes6502_execute(60);
    assert(memory[0x10] > 1);
    nes6502_reset();
    assert(sources() == 0);
    puts("PASS frame inhibit/status, masking/CLI/PLP/RTI, overlapping DMC/MMC3/legacy IRQs, E000, reset");
    /* Process-owned fixture allocation; no cartridge/MMC exists to destroy. */
    apu_destroy(&machine->apu);
    ppu_destroy(&machine->ppu);
    free(machine->cpu->mem_page[0]);
    free(machine->cpu);
    free(machine);
    return 0;
}
