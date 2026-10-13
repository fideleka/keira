#pragma once

#if defined(KEIRA_ILI9341_MIRROR_POC) && KEIRA_ILI9341_MIRROR_POC

#include "keira/service.h"
#include <Arduino_GFX_Library.h>
#include <lilka/spi.h>
#include "ili9341poc.h"

// PoC wiring only; do not change the SDK's internal display pin definitions.
#define K_POC_DISPLAY_CLK 12
#define K_POC_DISPLAY_MOSI 13
#define K_POC_DISPLAY_RES 14
#define K_POC_DISPLAY_DC 21

class DisplayMirrorService : public Service {
public:
    DisplayMirrorService();
    ~DisplayMirrorService() override;

private:
    void run() override;
    bool prepareFrame();

    Arduino_HWSPI bus;
    Ili9341Poc output;
    lilka::Canvas* frame = nullptr;
    bool blanked = false;
};

#endif
