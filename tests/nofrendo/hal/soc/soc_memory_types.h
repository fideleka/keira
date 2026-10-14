#pragma once
#include <stdbool.h>
/* Host boundary for the ESP-IDF address-classification predicate only. */
extern int host_external_ram;
static inline bool esp_ptr_external_ram(const void* pointer) {
    return host_external_ram != 0;
}
