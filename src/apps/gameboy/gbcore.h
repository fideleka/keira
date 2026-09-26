#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GbCore GbCore;
typedef void (*GbDrawLine)(void* context, const uint8_t* pixels, uint8_t line, bool color, const uint16_t* palette);

// The ROM buffer must remain valid until gbcore_destroy().
GbCore* gbcore_create(const uint8_t* rom, size_t rom_size, GbDrawLine draw, void* context);
void gbcore_destroy(GbCore* core);
size_t gbcore_save_size(GbCore* core);
void gbcore_set_save(GbCore* core, uint8_t* data, size_t size);
void gbcore_set_buttons(GbCore* core, uint8_t buttons);
void gbcore_run_frame(GbCore* core);
// The renderer fills two interleaved int16_t channels per sample frame.
size_t gbcore_audio_sample_frames(void);
void gbcore_render_audio(GbCore* core, int16_t* samples);
void gbcore_set_clock(GbCore* core, const struct tm* time);

#ifdef __cplusplus
}
#endif
