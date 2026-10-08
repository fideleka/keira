#include "apps/brightnesstest/control.h"
#include <cassert>
#include <cstdint>

int main() {
    BrightnessTestControl c;
    assert(c.percent() == 100 && c.duty() == 255);
    assert(!c.scan(false, true, false, false, 0));
    assert(c.scan(true, true, false, false, 10) && c.percent() == 90);
    assert(!c.scan(true, true, false, false, 409));
    assert(c.scan(true, true, false, false, 410) && c.percent() == 80);
    assert(c.scan(true, true, false, false, 10000) && c.percent() == 70);
    assert(!c.scan(true, true, false, false, 10001)); // No catch-up bursts.
    assert(!c.scan(true, true, true, false, 10100));
    assert(!c.scan(true, false, true, true, 10200)); // Start has priority.
    assert(c.scan(true, false, true, false, 10300) && c.percent() == 80);
    assert(!c.scan(false, false, true, false, 10400));
    assert(c.scan(true, false, true, false, 10410) && c.percent() == 90);
    assert(c.scan(true, false, true, false, 10810) && c.percent() == 100);
    assert(!c.scan(true, false, true, false, 10910));
    c.scan(false, false, false, false, 11000);
    for (uint32_t i = 0; i < 15; ++i) c.scan(true, true, false, false, 12000 + i * 500);
    assert(c.percent() == 0 && c.duty() == 0);
    assert(c.scan(true, false, true, false, 20000) && c.percent() == 10);
    BrightnessTestControl reboot;
    assert(reboot.percent() == 100);
    const uint32_t nearWrap = UINT32_MAX - 100;
    assert(reboot.scan(true, true, false, false, nearWrap));
    assert(!reboot.scan(true, true, false, false, 298));
    assert(reboot.scan(true, true, false, false, 299));
    assert(reboot.percent() == 80);
}
