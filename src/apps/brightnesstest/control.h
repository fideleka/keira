#pragma once

#include <stdint.h>

// RAM-only test control, independent of the display and Arduino runtime.
class BrightnessTestControl {
public:
    bool scan(bool select, bool left, bool right, bool start, uint32_t now) {
        const int direction = select && !start && (left != right) ? (right ? 1 : -1) : 0;
        if (!direction) {
            active = 0;
            return false;
        }
        if (direction != active) {
            active = direction;
            nextRepeat = now + 400;
            return step(direction);
        }
        if (static_cast<int32_t>(now - nextRepeat) >= 0) {
            nextRepeat = now + 100;
            return step(direction);
        }
        return false;
    }

    int percent() const {
        return level;
    }

    uint8_t duty() const {
        return static_cast<uint8_t>((level * 255 + 50) / 100);
    }

private:
    bool step(int direction) {
        int next = level + direction * 10;
        if (next < 0) next = 0;
        if (next > 100) next = 100;
        if (next == level) return false;
        level = next;
        return true;
    }

    int level = 100;
    int active = 0;
    uint32_t nextRepeat = 0;
};
