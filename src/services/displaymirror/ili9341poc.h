#pragma once

#if defined(KEIRA_ILI9341_MIRROR_POC) && KEIRA_ILI9341_MIRROR_POC

#include <Arduino_TFT.h>
#include <initializer_list>

// Minimal local adapter: cached GFX dependencies contain ST7789 only.
// Keep the shared dependency and SDK unmodified for this temporary PoC.
class Ili9341Poc : public Arduino_TFT {
public:
    Ili9341Poc(Arduino_DataBus* bus, int8_t reset);
    void setRotation(uint8_t rotation) override;
    void writeAddrWindow(int16_t x, int16_t y, uint16_t width, uint16_t height) override;

protected:
    void tftInit() override;

private:
    void command(uint8_t code, std::initializer_list<uint8_t> data = {});
};

#endif
