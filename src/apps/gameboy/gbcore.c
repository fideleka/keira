// Keira is built for size by default. Keep the emulator's instruction loop
// speed-optimized without changing optimization of the rest of the firmware.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif

#include "gbcore.h"

#include <esp_heap_caps.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct gb_s;
static uint8_t read_audio(struct gb_s* gb, uint16_t address);
static void write_audio(struct gb_s* gb, uint16_t address, uint8_t value);

// Walnut calls audio_read/write without a context argument. All its call sites
// are inside functions with a `gb` parameter, so adapt them at inclusion time
// without changing the vendored core or introducing a global current instance.
#define ENABLE_SOUND 1
#define audio_read(address) read_audio(gb, (address))
#define audio_write(address, value) write_audio(gb, (address), (value))
#include "vendor/walnut_cgb.h"
#undef audio_read
#undef audio_write

#define MINIGB_APU_AUDIO_FORMAT_S16SYS 1
#include "vendor/minigb_apu.h"

struct GbCore {
    struct gb_s gb;
    struct minigb_apu_ctx apu;
    bool internal_ram;
    const uint8_t* rom;
    size_t rom_size;
    uint8_t* rom_cache0;
    uint8_t* rom_cache1;
    uint16_t cached_bank1;
    uint32_t cache_reloads;
    uint8_t* save;
    size_t save_size;
    GbDrawLine draw;
    void* context;
};

static struct GbCore* owner(struct gb_s* gb) {
    return (struct GbCore*)gb->direct.priv;
}

static uint8_t read_audio(struct gb_s* gb, uint16_t address) {
    if (address < 0xFF10 || address > 0xFF3F) return 0xFF;
    return minigb_apu_audio_read(&owner(gb)->apu, address);
}

static void write_audio(struct gb_s* gb, uint16_t address, uint8_t value) {
    if (address >= 0xFF10 && address <= 0xFF3F) minigb_apu_audio_write(&owner(gb)->apu, address, value);
}

static const uint8_t* rom_bytes(struct GbCore* core, uint_fast32_t addr, size_t count) {
    if (addr >= core->rom_size || count > core->rom_size - addr) return NULL;
    const size_t offset = addr & (ROM_BANK_SIZE - 1);
    if (offset + count > ROM_BANK_SIZE) return core->rom + addr;
    const uint_fast32_t bank = addr / ROM_BANK_SIZE;
    if (bank == 0 && core->rom_cache0) return core->rom_cache0 + offset;
    if (bank != 0 && core->rom_cache1) {
        if (core->cached_bank1 != bank) {
            memcpy(core->rom_cache1, core->rom + bank * ROM_BANK_SIZE, ROM_BANK_SIZE);
            core->cached_bank1 = bank;
            ++core->cache_reloads;
        }
        return core->rom_cache1 + offset;
    }
    return core->rom + addr;
}

static uint8_t read_rom(struct gb_s* gb, const uint_fast32_t addr) {
    const uint8_t* bytes = rom_bytes(owner(gb), addr, 1);
    return bytes ? bytes[0] : 0xFF;
}

static uint16_t read_rom16(struct gb_s* gb, const uint_fast32_t addr) {
    const uint8_t* bytes = rom_bytes(owner(gb), addr, 2);
    if (bytes) return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
    return (uint16_t)read_rom(gb, addr) | ((uint16_t)read_rom(gb, addr + 1) << 8);
}

static uint32_t read_rom32(struct gb_s* gb, const uint_fast32_t addr) {
    const uint8_t* bytes = rom_bytes(owner(gb), addr, 4);
    if (bytes) {
        return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) |
               ((uint32_t)bytes[3] << 24);
    }
    return (uint32_t)read_rom16(gb, addr) | ((uint32_t)read_rom16(gb, addr + 2) << 16);
}

static uint8_t read_save(struct gb_s* gb, const uint_fast32_t addr) {
    struct GbCore* core = owner(gb);
    return core->save && addr < core->save_size ? core->save[addr] : 0xFF;
}

static void write_save(struct gb_s* gb, const uint_fast32_t addr, const uint8_t value) {
    struct GbCore* core = owner(gb);
    if (core->save && addr < core->save_size) core->save[addr] = value;
}

static void core_error(struct gb_s* gb, const enum gb_error_e error, const uint16_t addr) {
    (void)gb;
    (void)error;
    (void)addr;
    // Walnut specifies that returning from this callback is undefined.
    abort();
}

static void draw_line(struct gb_s* gb, const uint8_t* pixels, const uint_fast8_t line) {
    struct GbCore* core = owner(gb);
    if (core->draw && line < LCD_HEIGHT) {
        core->draw(core->context, pixels, line, gb->cgb.cgbMode != 0, gb->cgb.fixPalette);
    }
}

GbCore* gbcore_create(const uint8_t* rom, size_t rom_size, GbDrawLine draw, void* context) {
    const uint32_t fast_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const size_t fast_reserve = 64 * 1024;
    struct GbCore* core = NULL;
    bool internal_ram = false;
    if (heap_caps_get_free_size(fast_caps) > sizeof(struct GbCore) + fast_reserve) {
        core = heap_caps_calloc(1, sizeof(struct GbCore), fast_caps);
        internal_ram = core != NULL;
    }
    if (!core) core = heap_caps_calloc(1, sizeof(struct GbCore), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!core) return NULL;
    core->internal_ram = internal_ram;
    core->rom = rom;
    core->rom_size = rom_size;
    core->cached_bank1 = UINT16_MAX;
    core->draw = draw;
    core->context = context;
    minigb_apu_audio_init(&core->apu);
    if (gb_init(&core->gb, read_rom, read_rom16, read_rom32, read_save, write_save, core_error, core) !=
        GB_INIT_NO_ERROR) {
        free(core);
        return NULL;
    }
    // Opcode fetches dominate a running game. Keep the fixed bank and, when
    // internal RAM permits, the currently selected bank out of PSRAM.
    const uint32_t cache_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const size_t cache_reserve = 48 * 1024;
    if (heap_caps_get_free_size(cache_caps) > ROM_BANK_SIZE + cache_reserve) {
        core->rom_cache0 = heap_caps_malloc(ROM_BANK_SIZE, cache_caps);
        if (core->rom_cache0) memcpy(core->rom_cache0, rom, ROM_BANK_SIZE);
    }
    if (rom_size > ROM_BANK_SIZE && heap_caps_get_free_size(cache_caps) > ROM_BANK_SIZE + cache_reserve) {
        core->rom_cache1 = heap_caps_malloc(ROM_BANK_SIZE, cache_caps);
    }
    gb_init_lcd(&core->gb, draw_line);
    return core;
}

void gbcore_destroy(GbCore* core) {
    if (!core) return;
    free(core->rom_cache1);
    free(core->rom_cache0);
    free(core);
}

bool gbcore_uses_internal_ram(const GbCore* core) {
    return core->internal_ram;
}

unsigned gbcore_cached_rom_banks(const GbCore* core) {
    return (core->rom_cache0 != NULL) + (core->rom_cache1 != NULL);
}

uint32_t gbcore_take_cache_reloads(GbCore* core) {
    const uint32_t count = core->cache_reloads;
    core->cache_reloads = 0;
    return count;
}

size_t gbcore_save_size(GbCore* core) {
    size_t size = 0;
    return gb_get_save_size_s(&core->gb, &size) == 0 ? size : 0;
}

void gbcore_set_save(GbCore* core, uint8_t* data, size_t size) {
    core->save = data;
    core->save_size = size;
}

void gbcore_set_buttons(GbCore* core, uint8_t buttons) {
    core->gb.direct.joypad = buttons;
    walnut_refresh_joypad(&core->gb, core->gb.hram_io[IO_JOYP]);
}

void gbcore_run_frame(GbCore* core) {
    gb_run_frame_dualfetch(&core->gb);
}

size_t gbcore_audio_sample_frames(void) {
    return AUDIO_SAMPLES;
}

void gbcore_render_audio(GbCore* core, int16_t* samples) {
    minigb_apu_audio_callback(&core->apu, samples);
}

void gbcore_set_clock(GbCore* core, const struct tm* time) {
    if (time) gb_set_rtc(&core->gb, time);
}
