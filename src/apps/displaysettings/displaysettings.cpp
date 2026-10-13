#include "displaysettings.h"
#include "keira/keira.h"
#include "keira/ksystem.h"
#include "devices/devicemanager.h"

namespace {
const uint32_t timeoutChoices[] = {0, 30, 60, 120, 300, 600};

String timeoutText(uint32_t seconds) {
    if (!seconds) return K_S_DISPLAY_NEVER;
    if (seconds % 60 == 0) return String(seconds / 60) + " " + K_S_DISPLAY_MINUTES;
    return String(seconds) + " " + K_S_DISPLAY_SECONDS;
}

void changeTimeout(int direction, bool dim) {
    const uint32_t current =
        dim ? lilka::displaySettings.getDimTimeoutSeconds() : lilka::displaySettings.getTimeoutSeconds();
    uint32_t next = current;
    if (direction > 0) {
        for (auto value : timeoutChoices) {
            if (value > current) {
                next = value;
                break;
            }
        }
    } else {
        for (auto value : timeoutChoices) {
            if (value < current) next = value;
        }
    }
    if (dim) lilka::displaySettings.setDimTimeoutSeconds(next);
    else lilka::displaySettings.setTimeoutSeconds(next);
}
} // namespace

DisplayConfigApp::DisplayConfigApp() : App("DisplayConfig") {
}

void DisplayConfigApp::run() {
    lilka::Menu menu(K_S_LAUNCHER_DISPLAY);
    menu.addItem(K_S_DISPLAY_BRIGHTNESS);
    menu.addItem(K_S_DISPLAY_TIMEOUT);
    menu.addItem(K_S_DISPLAY_DIM);
    menu.addItem(K_S_DISPLAY_MODE);
    menu.addActivationButton(K_BTN_BACK);
    menu.removeActivationButton(lilka::Button::A);
    menu.setHorizontalNavigationEnabled(false);
    while (!menu.isFinished()) {
        menu.setItem(
            0,
            K_S_DISPLAY_BRIGHTNESS,
            nullptr,
            lilka::colors::White,
            lilka::brightness.isEnabled() ? String("< ") + String(lilka::brightness.getBrightness()) + "% >"
                                          : String(K_S_DISPLAY_UNAVAILABLE)
        );
        menu.setItem(
            1,
            K_S_DISPLAY_TIMEOUT,
            nullptr,
            lilka::colors::White,
            lilka::displaySettings.isAvailable()
                ? String("< ") + timeoutText(lilka::displaySettings.getTimeoutSeconds()) + " >"
                : String(K_S_DISPLAY_UNAVAILABLE)
        );
        menu.setItem(
            2,
            K_S_DISPLAY_DIM,
            nullptr,
            lilka::colors::White,
            lilka::displaySettings.isAvailable() && lilka::brightness.isEnabled()
                ? String("< ") + timeoutText(lilka::displaySettings.getDimTimeoutSeconds()) + " >"
                : String(K_S_DISPLAY_UNAVAILABLE)
        );
        if (ksystem.devices) {
            const char* labels[] = {K_S_DISPLAY_INTERNAL, K_S_DISPLAY_EXTERNAL, K_S_DISPLAY_BOTH};
            menu.setItem(
                3,
                K_S_DISPLAY_MODE,
                nullptr,
                lilka::colors::White,
                String("< ") + labels[ksystem.devices->displayMode()] + " >"
            );
        }
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
            else if (menu.getCursor() == 1) changeTimeout(direction, false);
            else if (menu.getCursor() == 3 && ksystem.devices) {
                const int next = (static_cast<int>(ksystem.devices->displayMode()) + direction + 3) % 3;
                if (!ksystem.devices->setDisplayMode(static_cast<DeviceManager::Mode>(next)))
                    alert(K_S_ERROR, K_S_DEVICES_CHANGE_FAILED);
                else if (next != DeviceManager::Internal && !ksystem.devices->externalOutput())
                    alert(K_S_DISPLAY_FALLBACK, K_S_DISPLAY_NO_ACTIVE);
            } else if (lilka::brightness.isEnabled()) changeTimeout(direction, true);
        }
        menu.update();
        menu.draw(canvas);
        queueDraw();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
