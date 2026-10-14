#pragma once
#include <cstddef>
#include <cstdint>

namespace nesPixels {
typedef uint32_t PixelWord __attribute__((__may_alias__));
// RGB565 remains in host byte order. SPI packing owns wire byte swapping.
inline void convertRow(uint16_t* output, const uint8_t* source, const uint16_t* palette, size_t pixels) {
    if (!pixels) return;
    if (reinterpret_cast<uintptr_t>(output) & 3) {
        *output++ = palette[*source++];
        --pixels;
    }
    auto* words = reinterpret_cast<PixelWord*>(__builtin_assume_aligned(output, 4));
    // Read/convert one eight-pixel group before issuing its four consecutive
    // PSRAM word stores. Normal 256-pixel rows need 32 loop iterations, not 128.
    // Keep the small tail for clipped/odd rows; no additional buffer or LUT.
    while (pixels >= 8) {
        const uint32_t a = uint32_t(palette[source[0]]) | (uint32_t(palette[source[1]]) << 16);
        const uint32_t b = uint32_t(palette[source[2]]) | (uint32_t(palette[source[3]]) << 16);
        const uint32_t c = uint32_t(palette[source[4]]) | (uint32_t(palette[source[5]]) << 16);
        const uint32_t d = uint32_t(palette[source[6]]) | (uint32_t(palette[source[7]]) << 16);
        words[0] = a;
        words[1] = b;
        words[2] = c;
        words[3] = d;
        words += 4;
        source += 8;
        pixels -= 8;
    }
    while (pixels >= 2) {
        *words++ = uint32_t(palette[source[0]]) | (uint32_t(palette[source[1]]) << 16);
        source += 2;
        pixels -= 2;
    }
    if (pixels) *reinterpret_cast<uint16_t*>(words) = palette[*source];
}
} // namespace nesPixels
