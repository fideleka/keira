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
    while (pixels >= 2) {
        *words++ = uint32_t(palette[source[0]]) | (uint32_t(palette[source[1]]) << 16);
        source += 2;
        pixels -= 2;
    }
    if (pixels) *reinterpret_cast<uint16_t*>(words) = palette[*source];
}
} // namespace nesPixels
