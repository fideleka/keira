#pragma once
#include <cstdint>

// Bresenham-style tick periods: 60 intervals at 1 kHz total exactly 1000
// ticks, rather than 60 * 16. No floating point or per-frame allocation.
class FractionalTicks {
public:
    FractionalTicks(uint32_t ticks, uint32_t frequency) :
        base(ticks / frequency), fraction(ticks % frequency), frequency(frequency) {
    }
    uint32_t next() {
        remainder += fraction;
        const uint32_t extra = remainder >= frequency;
        if (extra) remainder -= frequency;
        return base + extra;
    }
    void reset() {
        remainder = 0;
    }

private:
    uint32_t base, fraction, frequency, remainder = 0;
};
