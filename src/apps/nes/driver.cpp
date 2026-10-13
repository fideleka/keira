#include "driver.h"
#include "services/screenshot/request.h"
#include "keira/ksystem.h"
#include "keira/keira_lang.h"

NesApp* Driver::app;
int16_t Driver::w, Driver::h, Driver::frame_x, Driver::frame_y, Driver::frame_x_offset, Driver::frame_width,
    Driver::frame_height, Driver::frame_line_pixels;
int64_t Driver::last_render = 0;
int64_t Driver::last_frame_duration = 0;
uint8_t Driver::rotation = LILKA_DISPLAY_ROTATION; // not actually same value

void Driver::setNesApp(NesApp* app) {
    Driver::app = app;
}

int Driver::init(int width, int height) {
    w = app->canvas->width();
    h = app->canvas->height();
    rotation = app->canvas->getRotation();
    nofrendo_log_printf("display w: %d, h: %d\n", w, h);
    frame_width = w < NES_SCREEN_WIDTH ? w : NES_SCREEN_WIDTH;
    frame_height = h < NES_SCREEN_HEIGHT ? h : NES_SCREEN_HEIGHT;
    frame_x = (w - frame_width) / 2;
    frame_y = (h - frame_height) / 2;
    frame_x_offset = (NES_SCREEN_WIDTH - frame_width) / 2;
    frame_line_pixels = frame_width;

    return 0;
}

void Driver::shutdown() {
}

int Driver::setMode(int width, int height) {
    return 0;
}

void Driver::setPalette(rgb_t* pal) {
    uint16 c;

    int i;

    for (i = 0; i < 256; i++) {
        c = (pal[i].b >> 3) + ((pal[i].g >> 2) << 5) + ((pal[i].r >> 3) << 11);
        //myPalette[i]=(c>>8)|((c&0xff)<<8);
        nesPalette[i] = c;
    }
}

void Driver::clear(uint8 color) {
    app->canvas->fillScreen(0);
}

void Driver::customBlit(bitmap_t* bmp, int numDirties, rect_t* dirtyRects) {
#ifdef NES_FPS_COUNTER
    last_frame_duration = micros() - last_render;
    last_render = micros();
#endif

    lilka::Canvas* canvas = app->canvas;
    // Restore only the uncovered margins on every alternate app buffer. The
    // compositor may have stamped a toast there since this buffer's last use.
    canvas->fillRect(0, 0, w, frame_y, lilka::colors::Black);
    canvas->fillRect(0, frame_y + frame_height, w, h - frame_y - frame_height, lilka::colors::Black);
    canvas->fillRect(0, frame_y, frame_x, frame_height, lilka::colors::Black);
    canvas->fillRect(frame_x + frame_width, frame_y, w - frame_x - frame_width, frame_height, lilka::colors::Black);

    auto fb = canvas->getFramebuffer();
    const int stride = (rotation & 1) ? h : w;
    const int sourceY = (NES_SCREEN_HEIGHT - frame_height) / 2;
    for (int y = 0; y < frame_height; ++y) {
        const uint8_t* src = bmp->line[sourceY + y] + frame_x_offset;
        const int dy = frame_y + y;
        uint16_t* dst;
        int step;
        switch (rotation) {
            case 1:
                dst = fb + frame_x * stride + h - 1 - dy;
                step = stride;
                break;
            case 2:
                dst = fb + (h - 1 - dy) * stride + w - 1 - frame_x;
                step = -1;
                break;
            case 3:
                dst = fb + (w - 1 - frame_x) * stride + dy;
                step = -stride;
                break;
            default:
                dst = fb + dy * stride + frame_x;
                step = 1;
                break;
        }
        for (int x = 0; x < frame_width; ++x) {
            dst[x * step] = nesPalette[src[x]];
        }
    }

    // Serial.println("Draw 1 took " + String(micros() - last_render) + "us");
#ifdef NES_FPS_COUNTER
    if (last_frame_duration > 0) {
        canvas->fillRect(80, canvas->height() - 20, 80, 20, lilka::colors::Black);
        canvas->setCursor(80, canvas->height() - 4);
        canvas->setTextSize(1);
        canvas->setTextColor(lilka::colors::Graygrey);
        canvas->print("FPS: ");
        canvas->print(1000000 / last_frame_duration);
    }
#endif

    // Serial.println("Draw 2 took " + String(micros() - last_render) + "us");

    if (app->screenshotOnNextFrame) {
        app->screenshotOnNextFrame = false;
        if (!screenshot::request(canvas)) ksystem.apps.startToast(K_S_SCREENSHOT_SAVE_ERROR);
    }
    app->queueDraw();
}

uint16 Driver::nesPalette[256];
viddriver_t Driver::driver = {
    "Lilka", /* name */
    Driver::init, /* init */
    Driver::shutdown, /* shutdown */
    Driver::setMode, /* set_mode */
    Driver::setPalette, /* set_palette */
    Driver::clear, /* clear */
    NULL, /* lock_write */
    NULL, /* free_write */
    Driver::customBlit, /* custom_blit */
    false /* invalidate flag */
};
