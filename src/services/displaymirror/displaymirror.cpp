#include "displaymirror.h"

#if defined(KEIRA_ILI9341_MIRROR_POC) && KEIRA_ILI9341_MIRROR_POC

#include <new>
#include "keira/ksystem.h"

DisplayMirrorService::DisplayMirrorService() :
    Service("display-mirror-poc"),
    bus(K_POC_DISPLAY_DC, -1, K_POC_DISPLAY_CLK, K_POC_DISPLAY_MOSI, -1, &lilka::SPI2, true),
    output(&bus, K_POC_DISPLAY_RES) {
    setktStackSize(8192);
}

DisplayMirrorService::~DisplayMirrorService() {
    delete frame;
}

bool DisplayMirrorService::prepareFrame() {
    const int width = lilka::display.width();
    const int height = lilka::display.height();
    if (width <= 0 || height <= 0 || width * height > 240 * 320) return false;
    if (frame && frame->width() == width && frame->height() == height) return true;

    delete frame;
    frame = new (std::nothrow) lilka::Canvas(width, height);
    if (!frame || !frame->getFramebuffer()) {
        delete frame;
        frame = nullptr;
        return false;
    }
    frame->fillScreen(lilka::colors::Black);
    output.setRotation(width > height ? 1 : 0);
    if (width > output.width() || height > output.height()) return false;
    output.fillScreen(lilka::colors::Black);
    return true;
}

void DisplayMirrorService::run() {
    // Own the user SPI bus, never the internal display/SD bus. No CS or MISO.
    // begin() cannot verify physical presence on this write-only connection.
    if (!output.begin(20000000)) {
        lilka::serial.err("ILI9341 PoC: initialization failed; built-in screen unchanged");
        return;
    }
    lilka::serial.log("ILI9341 PoC: CLK12 MOSI13 RES14 DC21; 20MHz; no CS/MISO/BLK control");

    // External-only startup diagnostic separates power/backlight from composition.
    for (uint16_t color : {lilka::colors::Red, lilka::colors::Green, lilka::colors::Blue}) {
        output.fillScreen(color);
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    output.fillScreen(lilka::colors::Black);

    while (1) {
        if (!prepareFrame()) {
            lilka::serial.err("ILI9341 PoC: framebuffer unavailable or dimensions unsupported");
            return;
        }
        if (lilka::displaySettings.isSleeping()) {
            if (!blanked) output.fillScreen(lilka::colors::Black);
            blanked = true;
        } else {
            blanked = false;
            // Snapshot locks are released before any external SPI transfer.
            // No app canvas mutation, internal LCD access or new NVS setting.
            ksystem.apps.renderToCanvas(frame);
            const uint32_t now = millis();
            auto overlay = lilka::audio.getVolumeOverlay();
            const auto brightness = lilka::brightness.getOverlay();
            if (brightness.visible(now) &&
                (!overlay.visible(now) || now - brightness.adjustedAt < now - overlay.adjustedAt)) {
                overlay = brightness;
            }
            lilka::drawVolumeOverlay(*frame, overlay, frame->width(), frame->height(), now);
            output.draw16bitRGBBitmap(
                (output.width() - frame->width()) / 2,
                (output.height() - frame->height()) / 2,
                frame->getFramebuffer(),
                frame->width(),
                frame->height()
            );
        }
        // At most 10 snapshots/sec, lower effective FPS with synchronous transfer.
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

#endif
