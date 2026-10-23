#pragma once

#include <Arduino.h>
#include <cstdint>

// Native menu pixels are owned for as long as the Applications menu exists.
struct GuestAppMetadata {
    String name;
    uint16_t pixels[24 * 24] = {};
    bool hasIcon = false;
};

void readGuestAppMetadata(const String& firmware, GuestAppMetadata& metadata);
