#include "ili9341poc.h"

#if defined(KEIRA_ILI9341_MIRROR_POC) && KEIRA_ILI9341_MIRROR_POC

#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

Ili9341Poc::Ili9341Poc(Arduino_DataBus* bus, int8_t reset) :
    Arduino_TFT(bus, reset, 1, false, 240, 320, 0, 0, 0, 0) {
    _override_datamode = 0; // SPI mode 0, MSB first.
}

void Ili9341Poc::command(uint8_t code, std::initializer_list<uint8_t> data) {
    _bus->beginWrite();
    _bus->writeCommand(code);
    for (uint8_t value : data) _bus->write(value);
    _bus->endWrite();
}

void Ili9341Poc::tftInit() {
    gpio_set_direction(static_cast<gpio_num_t>(_rst), GPIO_MODE_OUTPUT);
    gpio_set_level(static_cast<gpio_num_t>(_rst), 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(static_cast<gpio_num_t>(_rst), 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(static_cast<gpio_num_t>(_rst), 1);
    vTaskDelay(pdMS_TO_TICKS(120));
    command(0x01); // Software reset.
    vTaskDelay(pdMS_TO_TICKS(120));
    command(0xef, {0x03, 0x80, 0x02});
    command(0xcf, {0x00, 0xc1, 0x30});
    command(0xed, {0x64, 0x03, 0x12, 0x81});
    command(0xe8, {0x85, 0x00, 0x78});
    command(0xcb, {0x39, 0x2c, 0x00, 0x34, 0x02});
    command(0xf7, {0x20});
    command(0xea, {0x00, 0x00});
    command(0xc0, {0x23});
    command(0xc1, {0x10});
    command(0xc5, {0x3e, 0x28});
    command(0xc7, {0x86});
    command(0x3a, {0x55}); // RGB565, 16-bit pixels.
    command(0xb1, {0x00, 0x18});
    command(0xb6, {0x08, 0x82, 0x27});
    command(0xf2, {0x00});
    command(0x26, {0x01});
    command(0xe0, {0x0f, 0x31, 0x2b, 0x0c, 0x0e, 0x08, 0x4e, 0xf1, 0x37, 0x07, 0x10, 0x03, 0x0e, 0x09, 0x00});
    command(0xe1, {0x00, 0x0e, 0x14, 0x03, 0x11, 0x07, 0x31, 0xc1, 0x48, 0x08, 0x0f, 0x0c, 0x31, 0x36, 0x0f});
    command(0x20); // Inversion off for this non-IPS reference panel.
    command(0x11); // Sleep out.
    vTaskDelay(pdMS_TO_TICKS(120));
    command(0x29); // Display on.
    vTaskDelay(pdMS_TO_TICKS(20));
}

void Ili9341Poc::setRotation(uint8_t rotation) {
    Arduino_TFT::setRotation(rotation & 3);
    const uint8_t madctl[] = {0x48, 0x28, 0x88, 0xe8}; // BGR and orientation.
    command(0x36, {madctl[rotation & 3]});
}

void Ili9341Poc::writeAddrWindow(int16_t x, int16_t y, uint16_t width, uint16_t height) {
    _bus->writeC8D16D16(0x2a, x, x + width - 1);
    _bus->writeC8D16D16(0x2b, y, y + height - 1);
    _bus->writeCommand(0x2c);
}

#endif
