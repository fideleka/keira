#include "devices.h"
#include "keira/keira.h"
#include "keira/ksystem.h"
#include "devices/devicemanager.h"

#include <cstring>

namespace {
// Presentation-only localization keeps the freestanding driver ABI language-neutral.
const char* driverText(const char* original) {
    static const struct {
        const char* source;
        const char* translated;
    } strings[] = {
        {"Clock pin", K_S_DRIVER_CLOCK},
        {"MOSI pin", K_S_DRIVER_MOSI},
        {"Reset pin", K_S_DRIVER_RESET},
        {"DC pin", K_S_DRIVER_DC},
        {"Backlight pin", K_S_DRIVER_BACKLIGHT},
        {"SPI speed", K_S_DRIVER_SPEED},
        {"Orientation", K_S_DRIVER_ORIENTATION},
        {"Refresh limit", K_S_DRIVER_REFRESH},
        {"Panel drive", K_S_DRIVER_DRIVE},
        {"SPI test", K_S_DRIVER_TEST},
        {"Automatic", K_S_DRIVER_AUTO},
        {"Automatic, flipped", K_S_DRIVER_FLIPPED},
        {"Disconnected", K_S_DRIVER_DISCONNECTED},
        {"Off", K_S_DRIVER_TEST_OFF},
        {"Fixed image + writes", K_S_DRIVER_WRITES},
        {"Fixed image, hold", K_S_DRIVER_HOLD},
        {"79Hz default", K_S_DRIVER_DRIVE_DEFAULT},
        {"70Hz line", K_S_DRIVER_DRIVE_70},
        {"61Hz line", K_S_DRIVER_DRIVE_61},
        {"79Hz frame", K_S_DRIVER_DRIVE_FRAME},
        {"15 FPS", K_S_DRIVER_FPS_15},
        {"20 FPS", K_S_DRIVER_FPS_20},
        {"30 FPS", K_S_DRIVER_FPS_30},
        {"60 FPS", K_S_DRIVER_FPS_60},
    };
    for (const auto& text : strings)
        if (!strcmp(original, text.source)) return text.translated;
    return original;
}
} // namespace

DevicesApp::DevicesApp() : App("Devices") {
}
void DevicesApp::run() {
    auto* manager = ksystem.devices;
    if (!manager || !manager->count()) {
        alert(K_S_DEVICES, K_S_DEVICES_NONE);
        return;
    }
    int cursor = 0;
    while (true) {
        lilka::Menu menu(K_S_DEVICES);
        menu.addActivationButton(K_BTN_BACK);
        for (unsigned i = 0; i < manager->count(); ++i) {
            DeviceManager::Info info;
            manager->info(i, info);
            menu.addItem(
                info.name,
                nullptr,
                lilka::colors::White,
                info.active    ? K_S_DEVICES_ACTIVE
                : info.enabled ? K_S_ON
                               : K_S_OFF
            );
        }
        menu.setCursor(cursor);
        while (!menu.isFinished()) {
            menu.update();
            menu.draw(canvas);
            queueDraw();
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        if (menu.getButton() == K_BTN_BACK) return;
        cursor = menu.getCursor();
        devicePage(cursor);
    }
}
void DevicesApp::devicePage(unsigned index) {
    auto* manager = ksystem.devices;
    int cursor = 0;
    while (true) {
        DeviceManager::Info info;
        if (!manager->info(index, info)) return;
        lilka::Menu menu(info.name);
        menu.addActivationButton(K_BTN_BACK);
        menu.addItem(K_S_DEVICES_ENABLED, nullptr, lilka::colors::White, info.enabled ? K_S_ON : K_S_OFF);
        menu.addItem(K_S_STATUS);
        for (unsigned f = 0; f < info.settingCount; ++f) {
            DeviceManager::SettingInfo setting;
            manager->settingInfo(index, f, setting);
            menu.addItem(
                driverText(setting.label),
                nullptr,
                lilka::colors::White,
                setting.kind == KD_SETTING_CHOICE    ? String(driverText(setting.choice))
                : setting.kind == KD_SETTING_BOOLEAN ? String(setting.value ? K_S_ON : K_S_OFF)
                                                     : String(setting.value)
            );
        }
        menu.addItem(K_S_DEVICES_RESET);
        menu.addItem(K_S_DEVICES_IDENTITY);
        menu.setCursor(cursor);
        while (!menu.isFinished()) {
            menu.update();
            menu.draw(canvas);
            queueDraw();
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        if (menu.getButton() == K_BTN_BACK) return;
        cursor = menu.getCursor();
        bool good = true;
        if (cursor == 0) good = manager->setEnabled(index, !info.enabled);
        else if (cursor == 1) {
            manager->info(index, info);
            alert(K_S_STATUS, info.status);
        } else if (cursor < static_cast<int>(info.settingCount) + 2) settingPage(index, cursor - 2);
        else if (cursor == static_cast<int>(info.settingCount) + 2) {
            if (confirm(K_S_DEVICES_RESET, K_S_DEVICES_RESET_CONFIRM)) good = manager->resetSettings(index);
        } else alert(K_S_DEVICES_IDENTITY, String(info.name) + "\n" + info.version + "\n" + info.uuid);
        if (!good) {
            manager->info(index, info);
            alert(K_S_ERROR, String(K_S_DEVICES_CHANGE_FAILED) + "\n" + info.status);
        }
    }
}
void DevicesApp::settingPage(unsigned index, unsigned field) {
    auto* manager = ksystem.devices;
    DeviceManager::SettingInfo setting;
    if (!manager->settingInfo(index, field, setting)) return;
    lilka::Menu menu(driverText(setting.label));
    menu.addItem(driverText(setting.label));
    menu.addActivationButton(K_BTN_BACK);
    menu.removeActivationButton(lilka::Button::A);
    menu.setHorizontalNavigationEnabled(false);
    while (!menu.isFinished()) {
        manager->settingInfo(index, field, setting);
        const String value = setting.kind == KD_SETTING_CHOICE    ? String(driverText(setting.choice))
                             : setting.kind == KD_SETTING_BOOLEAN ? String(setting.value ? K_S_ON : K_S_OFF)
                                                                  : String(setting.value);
        menu.setItem(0, driverText(setting.label), nullptr, lilka::colors::White, String("< ") + value + " >");
        const auto state = lilka::controller.peekState();
        if (!state.selectHeld) {
            const bool less = state.left.justPressed || state.d.justPressed,
                       more = state.right.justPressed || state.a.justPressed;
            if (less != more && !manager->changeSetting(index, field, more ? 1 : -1)) {
                alert(K_S_ERROR, K_S_DEVICES_CHANGE_FAILED);
            }
        }
        menu.update();
        menu.draw(canvas);
        queueDraw();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
