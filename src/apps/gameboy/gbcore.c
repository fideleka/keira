#include "gbcore.h"

#include <esp_heap_caps.h>
#include <stdlib.h>
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
    const uint8_t* rom;
    size_t rom_size;
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

static uint8_t read_rom(struct gb_s* gb, const uint_fast32_t addr) {
    struct GbCore* core = owner(gb);
    return addr < core->rom_size ? core->rom[addr] : 0xFF;
}

static uint16_t read_rom16(struct gb_s* gb, const uint_fast32_t addr) {
    return (uint16_t)read_rom(gb, addr) | ((uint16_t)read_rom(gb, addr + 1) << 8);
}

static uint32_t read_rom32(struct gb_s* gb, const uint_fast32_t addr) {
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
    struct GbCore* core = heap_caps_calloc(1, sizeof(struct GbCore), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!core) return NULL;
    core->rom = rom;
    core->rom_size = rom_size;
    core->draw = draw;
    core->context = context;
    minigb_apu_audio_init(&core->apu);
    if (gb_init(&core->gb, read_rom, read_rom16, read_rom32, read_save, write_save, core_error, core) !=
        GB_INIT_NO_ERROR) {
        free(core);
        return NULL;
    }
    gb_init_lcd(&core->gb, draw_line);
    return core;
}

void gbcore_destroy(GbCore* core) {
    free(core);
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
}

void gbcore_run_frame(GbCore* core) {
    gb_run_frame(&core->gb);
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
