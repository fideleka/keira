#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace displayFrame {
// GNU may_alias permits word access to RGB565 storage without violating strict
// aliasing. Xtensa requires aligned word loads; clipped/odd rows use halfwords.
typedef uint32_t PixelWord __attribute__((__may_alias__));
inline bool copyCompareRow(
    uint16_t* output, const uint16_t* source, const uint16_t* previous, size_t pixels, bool force
) {
    if (force) {
        memcpy(output, source, pixels * sizeof(uint16_t));
        return true;
    }
    const bool inPlace = output == previous;
    size_t x = 0;
    if (!((reinterpret_cast<uintptr_t>(output) | reinterpret_cast<uintptr_t>(source) |
           reinterpret_cast<uintptr_t>(previous)) &
          3)) {
        auto* destination = reinterpret_cast<PixelWord*>(output);
        const auto* incoming = reinterpret_cast<const PixelWord*>(source);
        const auto* retained = reinterpret_cast<const PixelWord*>(previous);
        const size_t words = pixels / 2;
        for (size_t i = 0; i < words; ++i) {
            const uint32_t next = incoming[i];
            if (next != retained[i]) {
                destination[i] = next;
                x = (i + 1) * 2;
                // Once a row is dirty, more comparisons serve no purpose. Copy
                // only the suffix, not the already compared/copy-written prefix.
                memcpy(output + x, source + x, (pixels - x) * sizeof(uint16_t));
                return true;
            }
            if (!inPlace) destination[i] = next;
        }
        x = words * 2;
    }
    for (; x < pixels; ++x) {
        const uint16_t next = source[x];
        if (next != previous[x]) {
            output[x] = next;
            ++x;
            memcpy(output + x, source + x, (pixels - x) * sizeof(uint16_t));
            return true;
        }
        if (!inPlace) output[x] = next;
    }
    return false;
}
} // namespace displayFrame
