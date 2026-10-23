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
String settingValue(const DeviceManager::SettingInfo& setting) {
    const String value = setting.kind == KD_SETTING_CHOICE    ? String(driverText(setting.choice))
                         : setting.kind == KD_SETTING_BOOLEAN ? String(setting.value ? K_S_ON : K_S_OFF)
                                                              : String(setting.value);
    return String("< ") + value + " >";
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
        unsigned fields[KD_MAX_SETTINGS], fieldCount = 0;
        bool hasPins = false;
        for (unsigned field = 0; field < info.settingCount && field < KD_MAX_SETTINGS; ++field) {
            DeviceManager::SettingInfo setting;
            if (!manager->settingInfo(index, field, setting)) continue;
            if (setting.pin) hasPins = true;
            else fields[fieldCount++] = field;
        }
        if (hasPins) menu.addItem(K_S_DEVICES_PINS, nullptr, lilka::colors::White, ">");
        const int firstSetting = hasPins ? 3 : 2;
        const int resetRow = firstSetting + fieldCount;
        for (unsigned f = 0; f < fieldCount; ++f) {
            DeviceManager::SettingInfo setting;
            if (manager->settingInfo(index, fields[f], setting))
                menu.addItem(driverText(setting.label), nullptr, lilka::colors::White, settingValue(setting));
        }
        menu.addItem(K_S_DEVICES_RESET);
        menu.addItem(K_S_DEVICES_IDENTITY);
        menu.setCursor(cursor);
        menu.setHorizontalNavigationEnabled(false);
        while (!menu.isFinished()) {
            const int row = menu.getCursor();
            const bool isSetting = row >= firstSetting && row < resetRow;
            if (isSetting) menu.removeActivationButton(lilka::Button::A);
            else menu.addActivationButton(lilka::Button::A);
            const auto state = lilka::controller.peekState();
            if (isSetting && !state.selectHeld) {
                const bool less = state.left.justPressed || state.d.justPressed;
                const bool more = state.right.justPressed || state.a.justPressed;
                if (less != more) {
                    if (!manager->changeSetting(index, fields[row - firstSetting], more ? 1 : -1)) {
                        alert(K_S_ERROR, K_S_DEVICES_CHANGE_FAILED);
                    }
                    DeviceManager::SettingInfo setting;
                    if (manager->settingInfo(index, fields[row - firstSetting], setting))
                        menu.setItem(
                            row, driverText(setting.label), nullptr, lilka::colors::White, settingValue(setting)
                        );
                }
            }
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
        } else if (hasPins && cursor == 2) {
            pinsPage(index);
        } else if (cursor == resetRow) {
            if (confirm(K_S_DEVICES_RESET, K_S_DEVICES_RESET_CONFIRM)) good = manager->resetSettings(index);
        } else alert(K_S_DEVICES_IDENTITY, String(info.name) + "\n" + info.version + "\n" + info.uuid);
        if (!good) {
            manager->info(index, info);
            alert(K_S_ERROR, String(K_S_DEVICES_CHANGE_FAILED) + "\n" + info.status);
        }
    }
}

void DevicesApp::pinsPage(unsigned index) {
    auto* manager = ksystem.devices;
    DeviceManager::SettingsDraft draft;
    if (!manager->beginSettingsEdit(index, draft)) {
        alert(K_S_ERROR, K_S_DEVICES_CHANGE_FAILED);
        return;
    }
    unsigned fields[KD_MAX_SETTINGS], pinCount = 0;
    for (unsigned field = 0; field < draft.count && field < KD_MAX_SETTINGS; ++field) {
        DeviceManager::SettingInfo setting;
        if (manager->previewSetting(index, field, draft, setting) && setting.pin) fields[pinCount++] = field;
    }
    if (!pinCount) return;
    int cursor = 0;
    while (true) {
        lilka::Menu menu(K_S_DEVICES_PINS);
        menu.addActivationButton(K_BTN_BACK);
        for (unsigned pin = 0; pin < pinCount; ++pin) {
            DeviceManager::SettingInfo setting;
            if (manager->previewSetting(index, fields[pin], draft, setting))
                menu.addItem(driverText(setting.label), nullptr, lilka::colors::White, settingValue(setting));
        }
        menu.addItem(K_S_DEVICES_APPLY);
        menu.addItem(K_S_DEVICES_CANCEL);
        menu.setCursor(cursor);
        menu.setHorizontalNavigationEnabled(false);
        while (!menu.isFinished()) {
            const int row = menu.getCursor();
            const bool isPin = row >= 0 && row < static_cast<int>(pinCount);
            if (isPin) menu.removeActivationButton(lilka::Button::A);
            else menu.addActivationButton(lilka::Button::A);
            const auto state = lilka::controller.peekState();
            if (isPin && !state.selectHeld) {
                const bool less = state.left.justPressed || state.d.justPressed;
                const bool more = state.right.justPressed || state.a.justPressed;
                if (less != more) {
                    const unsigned field = fields[row];
                    DeviceManager::SettingInfo setting;
                    if (manager->previewSetting(index, field, draft, setting)) {
                        const int32_t previous = draft.values[field];
                        int64_t next = static_cast<int64_t>(previous) + (more ? setting.step : -setting.step);
                        if (next > setting.maximum) next = setting.minimum;
                        if (next < setting.minimum) next = setting.maximum;
                        draft.values[field] = next;
                        if (manager->previewSetting(index, field, draft, setting))
                            menu.setItem(
                                row, driverText(setting.label), nullptr, lilka::colors::White, settingValue(setting)
                            );
                        else draft.values[field] = previous;
                    }
                }
            }
            menu.update();
            menu.draw(canvas);
            queueDraw();
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        cursor = menu.getCursor();
        if (menu.getButton() == K_BTN_BACK || cursor == static_cast<int>(pinCount) + 1) return;
        if (cursor == static_cast<int>(pinCount)) {
            if (manager->applySettings(index, draft)) return;
            DeviceManager::Info info;
            manager->info(index, info);
            alert(K_S_ERROR, String(K_S_DEVICES_PINS_APPLY_FAILED) + "\n" + info.status);
        }
    }
}
