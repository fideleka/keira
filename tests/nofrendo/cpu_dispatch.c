/* Differential execution of two actual CPU objects, with exact RAM/state parity. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cpu/nes6502.h"
extern void reference_nes6502_setcontext(nes6502_context*);
extern void reference_nes6502_getcontext(nes6502_context*);
extern int reference_nes6502_execute(int);
extern void reference_nes6502_setirq(uint8, bool);
static uint8 memory[2][65536];
static unsigned active, trace;
static void (*set_irq)(uint8, bool);
static uint8 read_port(uint32 address) {
    trace = (trace ^ address) * 16777619u;
    if (address == 0x2020) set_irq(NES6502_IRQ_MAPPER, true);
    return memory[active][address];
}
static void write_port(uint32 address, uint8 value) {
    trace = ((trace ^ address) * 16777619u ^ value) * 16777619u;
    memory[active][address] = value;
    if (address == 0xe000) set_irq(NES6502_IRQ_MAPPER, false);
}
int main(void) {
    nes6502_memread reads[] = {{0x2000, 0x7fff, read_port}, {(uint32)-1, (uint32)-1, NULL}};
    nes6502_memwrite writes[] = {{0x2000, 0xffff, write_port}, {(uint32)-1, (uint32)-1, NULL}};
    unsigned cases = 0;
    for (unsigned opcode = 0; opcode < 256; ++opcode)
        for (unsigned flags = 0; flags < 8; ++flags)
            for (unsigned sources = 0; sources < 8; ++sources)
                for (unsigned budget = 1; budget <= 31; budget += 10) {
                    nes6502_context states[2];
                    int cycles[2];
                    unsigned traces[2];
                    for (active = 0; active < 2; ++active) {
                        memset(memory[active], 0xea, 65536);
                        memset(memory[active] + 0x100, 0x80, 256);
                        memory[active][0x8000] = opcode;
                        memory[active][0x8001] = memory[active][0x8002] = 0x20;
                        memory[active][0xfffe] = 0;
                        memory[active][0xffff] = 0x90;
                        memory[active][0x9000] = 0x8d; /* STA E000, acknowledge in handler */
                        memory[active][0x9001] = 0;
                        memory[active][0x9002] = 0xe0;
                        memory[active][0x9003] = 0x40; /* RTI */
                        nes6502_context c = {0};
                        for (unsigned page = 0; page < NES6502_NUMBANKS; ++page)
                            c.mem_page[page] = memory[active] + page * NES6502_BANKSIZE;
                        c.read_handler = reads;
                        c.write_handler = writes;
                        c.pc_reg = 0x8000;
                        c.s_reg = 0xfd;
                        c.a_reg = 0x37;
                        c.x_reg = 3;
                        c.y_reg = 5;
                        c.p_reg =
                            R_FLAG | (flags & 1 ? I_FLAG : 0) | (flags & 2 ? C_FLAG : 0) | (flags & 4 ? Z_FLAG : 0);
                        c.irq_sources = sources;
                        c.int_pending = flags & 1;
                        c.burn_cycles = flags & 2 ? 3 : 0;
                        trace = 2166136261u;
                        if (!active) {
                            set_irq = reference_nes6502_setirq;
                            reference_nes6502_setcontext(&c);
                            cycles[active] = reference_nes6502_execute(budget);
                            reference_nes6502_getcontext(&states[active]);
                        } else {
                            set_irq = nes6502_setirq;
                            nes6502_setcontext(&c);
                            cycles[active] = nes6502_execute(budget);
                            nes6502_getcontext(&states[active]);
                        }
                        traces[active] = trace;
                        /* Strip pointers so the remaining object bytes can be compared. */
                        memset(states[active].mem_page, 0, sizeof c.mem_page);
                        states[active].read_handler = NULL;
                        states[active].write_handler = NULL;
                    }
                    assert(cycles[0] == cycles[1] && traces[0] == traces[1]);
                    assert(!memcmp(&states[0], &states[1], sizeof states[0]));
                    assert(!memcmp(memory[0], memory[1], 65536));
                    ++cases;
                }
    printf(
        "PASS: %u actual CPU cases, all opcodes, IRQ sources/masking, partial budgets, cycles/registers/exact "
        "RAM/handler trace parity\n",
        cases
    );
}
