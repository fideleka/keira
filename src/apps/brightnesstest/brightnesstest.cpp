#include "brightnesstest.h"

#if defined(KEIRA_BACKLIGHT_TEST) && LILKA_VERSION == 2

#include "control.h"
#include <esp32-hal-ledc.h>

namespace {
// Dedicated boot test only: no GPIO manager or user scripts may share timer 3.
constexpr uint8_t pwmChannel = 7;
constexpr uint32_t pwmFrequency = 20000;
constexpr uint8_t pwmBits = 8;

void renderTest(lilka::Canvas* canvas, int percent, bool pwmReady) {
    canvas->fillScreen(lilka::colors::Black);
    canvas->setFont(FONT_8x13);
    canvas->setTextColor(lilka::colors::White);
    canvas->setCursor(12, 30);
    canvas->print("MODIFIED LILKA ONLY");
    canvas->setCursor(12, 58);
    canvas->print("Backlight: ");
    canvas->print(percent);
    canvas->print("%");
    canvas->setCursor(12, 86);
    canvas->print("Hold SEL + Left/Right");
    canvas->setCursor(12, 110);
    canvas->print("At 0%: SEL + Right wakes");
    canvas->setCursor(12, 134);
    canvas->print(pwmReady ? "RAM only; reboot = 100%" : "PWM setup failed");
    canvas->fillRect(12, 165, 70, 45, lilka::colors::White);
    canvas->fillRect(94, 165, 70, 45, lilka::colors::Red);
    canvas->fillRect(176, 165, 70, 45, lilka::colors::Blue);
}
} // namespace

BrightnessTestApp::BrightnessTestApp() : App("Brightness test") {
    setFlags(APP_FLAG_FULLSCREEN);
}

void BrightnessTestApp::run() {
    // The amplifier's SD connection MUST be isolated before this firmware is used.
    // Do not call board power-saving/display sleep APIs: only PWM the backlight.
    BrightnessTestControl control;
    const bool pwmReady = ledcSetup(pwmChannel, pwmFrequency, pwmBits) != 0;
    if (pwmReady) {
        // Set full duty before attaching to avoid an initial dark pulse.
        ledcWrite(pwmChannel, control.duty());
        ledcAttachPin(LILKA_SLEEP, pwmChannel);
    } else {
        pinMode(LILKA_SLEEP, OUTPUT);
        digitalWrite(LILKA_SLEEP, HIGH);
    }
    renderTest(canvas, control.percent(), pwmReady);
    queueDraw();

    while (1) {
        // Use debounced level state, not justPressed flags reset by other tasks.
        const lilka::State state = lilka::controller.peekState();
        if (pwmReady && control.scan(
                            state.selectHeld, state.left.pressed, state.right.pressed, state.start.pressed,
                            millis()
                        )) {
            ledcWrite(pwmChannel, control.duty());
            lilka::serial.log("Backlight test: %d%%", control.percent());
            renderTest(canvas, control.percent(), pwmReady);
            queueDraw();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

#endif
