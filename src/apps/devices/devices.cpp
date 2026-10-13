#include "devices.h"
#include "keira/ksystem.h"
#include "devices/devicemanager.h"

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
                setting.label,
                nullptr,
                lilka::colors::White,
                setting.kind == KD_SETTING_CHOICE    ? String(setting.choice)
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
    lilka::Menu menu(setting.label);
    menu.addItem(setting.label);
    menu.addActivationButton(K_BTN_BACK);
    menu.removeActivationButton(lilka::Button::A);
    menu.setHorizontalNavigationEnabled(false);
    while (!menu.isFinished()) {
        manager->settingInfo(index, field, setting);
        const String value = setting.kind == KD_SETTING_CHOICE    ? String(setting.choice)
                             : setting.kind == KD_SETTING_BOOLEAN ? String(setting.value ? K_S_ON : K_S_OFF)
                                                                  : String(setting.value);
        menu.setItem(0, setting.label, nullptr, lilka::colors::White, String("< ") + value + " >");
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
