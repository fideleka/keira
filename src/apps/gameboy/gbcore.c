#include "gbcore.h"

#include <esp_heap_caps.h>
#include <stdlib.h>
#include <string.h>

#include <gnuboy.h>
#include <hw.h>

#define GB_FRAME_WIDTH 160
#define GB_FRAME_HEIGHT 144
#define GB_AUDIO_CAPACITY_FRAMES 640

struct GbCore {
    const uint8_t* rom;
    bool fast_ram;
    uint8_t* indexed_frame;
    int16_t* audio;
    uint8_t* save;
    size_t save_size;
    int last_pad;
    GbDrawLine draw;
    void* context;
};

static void release_gnuboy(void) {
    gnuboy_free_rom();
    gnuboy_free_bios();
    free(GB.rambanks);
    free(GB.vbanks);
    GB.rambanks = NULL;
    GB.vbanks = NULL;
}

GbCore* gbcore_create(const uint8_t* rom, size_t rom_size, GbDrawLine draw, void* context) {
    GbCore* core = calloc(1, sizeof(GbCore));
    if (!core) return NULL;
    core->rom = rom;
    core->draw = draw;
    core->context = context;
    core->last_pad = -1;

    if (gnuboy_init(32000, GB_AUDIO_STEREO_S16, GB_PIXEL_PALETTED, NULL, NULL) < 0) {
        release_gnuboy();
        free(core);
        return NULL;
    }
    const uint32_t fast_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    if (heap_caps_get_free_size(fast_caps) > 48 * 1024 + 64 * 1024) {
        void* wram = heap_caps_calloc(8, 4096, fast_caps);
        void* vram = heap_caps_calloc(2, 8192, fast_caps);
        if (wram && vram) {
            free(GB.rambanks);
            free(GB.vbanks);
            GB.rambanks = wram;
            GB.vbanks = vram;
            core->fast_ram = true;
        } else {
            free(wram);
            free(vram);
        }
    }
    if (gnuboy_load_rom(rom, rom_size) < 0) {
        release_gnuboy();
        free(core);
        return NULL;
    }
    core->indexed_frame = heap_caps_malloc(GB_FRAME_WIDTH * GB_FRAME_HEIGHT, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    core->audio = malloc(GB_AUDIO_CAPACITY_FRAMES * 2 * sizeof(int16_t));
    if (!core->indexed_frame || !core->audio) {
        free(core->audio);
        free(core->indexed_frame);
        release_gnuboy();
        free(core);
        return NULL;
    }
    memset(core->indexed_frame, 0, GB_FRAME_WIDTH * GB_FRAME_HEIGHT);
    gnuboy_set_framebuffer(core->indexed_frame);
    gnuboy_set_soundbuffer(core->audio, GB_AUDIO_CAPACITY_FRAMES * 2);
    gnuboy_reset(true);
    return core;
}

void gbcore_destroy(GbCore* core) {
    if (!core) return;
    release_gnuboy();
    free(core->audio);
    free(core->indexed_frame);
    free(core);
}

bool gbcore_uses_internal_ram(const GbCore* core) {
    return core->fast_ram;
}

unsigned gbcore_cached_rom_banks(const GbCore* core) {
    (void)core;
    return 0; // Gnuboy maps cartridge banks directly into the emulated address space.
}

uint32_t gbcore_take_cache_reloads(GbCore* core) {
    (void)core;
    return 0;
}

size_t gbcore_save_size(GbCore* core) {
    (void)core;
    if (!cart.has_battery) return 0;
    return cart.mbc == MBC_MBC2 ? 512 : cart.ramsize * 8192;
}

void gbcore_set_save(GbCore* core, uint8_t* data, size_t size) {
    core->save = data;
    core->save_size = size;
    if (data && cart.rambanks) memcpy(cart.rambanks, data, size);
}

void gbcore_copy_save(GbCore* core) {
    if (core->save && cart.rambanks) memcpy(core->save, cart.rambanks, core->save_size);
}

void gbcore_set_buttons(GbCore* core, uint8_t buttons) {
    int pad = 0;
    if (!(buttons & 0x01)) pad |= GB_PAD_A;
    if (!(buttons & 0x02)) pad |= GB_PAD_B;
    if (!(buttons & 0x04)) pad |= GB_PAD_SELECT;
    if (!(buttons & 0x08)) pad |= GB_PAD_START;
    if (!(buttons & 0x10)) pad |= GB_PAD_RIGHT;
    if (!(buttons & 0x20)) pad |= GB_PAD_LEFT;
    if (!(buttons & 0x40)) pad |= GB_PAD_UP;
    if (!(buttons & 0x80)) pad |= GB_PAD_DOWN;
    if (pad != core->last_pad) {
        gnuboy_set_pad(pad);
        core->last_pad = pad;
    }
}

void gbcore_run_frame(GbCore* core) {
    gnuboy_run(true);
    if (!(R_LCDC & 0x80)) memset(core->indexed_frame, 0, GB_FRAME_WIDTH * GB_FRAME_HEIGHT);
    if (core->draw) {
        for (int y = 0; y < GB_FRAME_HEIGHT; ++y) {
            core->draw(core->context, core->indexed_frame + y * GB_FRAME_WIDTH, y, true, GB.video.palette);
        }
    }
}

size_t gbcore_audio_sample_frames(void) {
    return GB_AUDIO_CAPACITY_FRAMES;
}

size_t gbcore_render_audio(GbCore* core, int16_t* samples) {
    size_t frames = GB.audio.pos / 2;
    if (frames > GB_AUDIO_CAPACITY_FRAMES) frames = GB_AUDIO_CAPACITY_FRAMES;
    memcpy(samples, core->audio, frames * 2 * sizeof(int16_t));
    return frames;
}

void gbcore_set_clock(GbCore* core, const struct tm* time) {
    (void)core;
    if (time) gnuboy_set_time(time->tm_yday, time->tm_hour, time->tm_min, time->tm_sec);
}
