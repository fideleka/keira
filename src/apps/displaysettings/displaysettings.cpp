#include "displaysettings.h"
#include "keira/keira.h"

namespace {
const uint32_t timeoutChoices[] = {0, 30, 60, 120, 300, 600};

String timeoutText(uint32_t seconds) {
    if (!seconds) return K_S_DISPLAY_NEVER;
    if (seconds % 60 == 0) return String(seconds / 60) + " " + K_S_DISPLAY_MINUTES;
    return String(seconds) + " " + K_S_DISPLAY_SECONDS;
}

void changeTimeout(int direction) {
    const uint32_t current = lilka::displaySettings.getTimeoutSeconds();
    uint32_t next = current;
    if (direction > 0) {
        for (auto value : timeoutChoices) {
            if (value > current) { next = value; break; }
        }
    } else {
        for (auto value : timeoutChoices) {
            if (value < current) next = value;
        }
    }
    lilka::displaySettings.setTimeoutSeconds(next);
}
} // namespace

DisplayConfigApp::DisplayConfigApp() : App("DisplayConfig") {
}

void DisplayConfigApp::run() {
    lilka::Menu menu(K_S_LAUNCHER_DISPLAY);
    menu.addItem(K_S_DISPLAY_BRIGHTNESS);
    menu.addItem(K_S_DISPLAY_TIMEOUT);
    menu.addActivationButton(K_BTN_BACK);
    menu.removeActivationButton(lilka::Button::A);
    menu.setHorizontalNavigationEnabled(false);
    while (!menu.isFinished()) {
        menu.setItem(
            0, K_S_DISPLAY_BRIGHTNESS, nullptr, lilka::colors::White,
            lilka::brightness.isEnabled() ? String("< ") + String(lilka::brightness.getBrightness()) + "% >" :
                                           String(K_S_DISPLAY_UNAVAILABLE)
        );
        menu.setItem(
            1, K_S_DISPLAY_TIMEOUT, nullptr, lilka::colors::White,
            lilka::displaySettings.isAvailable() ?
                String("< ") + timeoutText(lilka::displaySettings.getTimeoutSeconds()) + " >" : String(K_S_DISPLAY_UNAVAILABLE)
        );
        // Observe adjustment keys before Menu::update consumes event flags.
        const auto state = lilka::controller.peekState();
        int direction = 0;
        if (!state.selectHeld) {
            const bool less = state.left.justPressed || state.d.justPressed;
            const bool more = state.right.justPressed || state.a.justPressed;
            if (less != more) direction = more ? 1 : -1;
        }
        if (direction) {
            if (menu.getCursor() == 0) lilka::brightness.stepBrightnessShortcut(direction);
            else changeTimeout(direction);
        }
        menu.update();
        menu.draw(canvas);
        queueDraw();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
