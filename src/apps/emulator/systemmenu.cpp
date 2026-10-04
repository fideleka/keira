#include "systemmenu.h"
#include "keira/keira.h"
#include "keira/utils/string.h"
#include "keira/keira_lang.h"

namespace {
bool anyPressed(const lilka::State& state) {
    return state.a.pressed || state.b.pressed || state.c.pressed || state.d.pressed || state.up.pressed ||
           state.down.pressed || state.left.pressed || state.right.pressed || state.select.pressed ||
           state.start.pressed;
}
} // namespace

void EmulatorMenuApp::waitForRelease() {
    nesmenu::ReleaseGate gate;
    while (gate.blocked(anyPressed(lilka::controller.getState())))
        vTaskDelay(pdMS_TO_TICKS(10));
    lilka::controller.resetState();
}

void EmulatorMenuApp::showNotice(const String& title) {
    waitForRelease();
    lilka::Menu notice(title);
    notice.addItem(K_S_EMU_CONTINUE);
    do {
        notice.update();
        notice.draw(canvas);
        queueDraw();
    } while (!notice.isFinished());
    waitForRelease();
}

void EmulatorMenuApp::loadPreferences() {
    char path[nesmenu::MAX_PATH_BYTES];
    if (!nesmenu::configPath(romConfigPath.c_str(), path, sizeof(path))) {
        configWarning = K_S_EMU_CONFIG_PATH;
    } else {
        configFile = path;
        auto result = nesmenu::loadConfig(path, preferences);
        // Preserve and recover backup settings after a failed promotion/rollback.
        String backup = configFile + ".bak";
        nesmenu::Preferences recovered;
        auto backupResult = nesmenu::loadConfig(backup.c_str(), recovered);
        if (backupResult != nesmenu::ConfigResult::Missing) {
            if (result == nesmenu::ConfigResult::Missing && backupResult == nesmenu::ConfigResult::Ok)
                preferences = recovered;
            configWarning = K_S_EMU_CONFIG_BACKUP;
        } else if (result != nesmenu::ConfigResult::Ok && result != nesmenu::ConfigResult::Missing) {
            configWarning = result == nesmenu::ConfigResult::Unsupported ? K_S_EMU_CONFIG_VERSION
                            : result == nesmenu::ConfigResult::Malformed ? K_S_EMU_CONFIG_INVALID
                                                                         : K_S_EMU_CONFIG_IO;
            lilka::serial.err(K_S_EMU_CONFIG_LOAD_LOG, int(result), configFile.c_str());
        }
    }
    if (configWarning.length()) {
        lilka::serial.err(K_S_EMU_CONFIG_LOG, configWarning.c_str());
        showNotice(configWarning);
    }
}

void EmulatorMenuApp::savePreferences() {
    directionFilter.reset();
    verticalFilter.reset();
    auto result =
        configFile.length() ? nesmenu::saveConfig(configFile.c_str(), preferences) : nesmenu::ConfigResult::IoError;
    if (result != nesmenu::ConfigResult::Ok) {
        configWarning = K_S_EMU_CONFIG_UNSAVED;
        lilka::serial.err(K_S_EMU_CONFIG_WRITE_LOG, int(result), configFile.c_str());
        showNotice(configWarning);
    } else {
        configWarning = "";
    }
}

EmulatorMenuApp::SystemAction EmulatorMenuApp::showSystemMenu() {
    waitForRelease();
    lilka::Menu menu(menuTitle);
    // SDK Menu has five visible rows. Keep every escape/capture action on page one.
    menu.addItem(K_S_EMU_RESUME);
    menu.addItem(K_S_EMU_SCREENSHOT);
    menu.addItem(K_S_EMU_CONTROLS);
    menu.addItem(K_S_EMU_MORE);
    menu.addItem(K_S_EMU_EXIT);
    menu.addActivationButton(lilka::Button::B);
    while (true) {
        menu.update();
        menu.draw(canvas);
        queueDraw();
        if (!menu.isFinished()) continue;
        waitForRelease();
        if (menu.getButton() == lilka::Button::B) return SystemAction::Resume;
        switch (menu.getCursor()) {
            case 0:
                return SystemAction::Resume;
            case 2: {
                lilka::Menu controls(K_S_EMU_CONTROLS);
                controls.addItem(K_S_EMU_PRECISION);
                controls.addItem(K_S_EMU_DELAY_MINUS);
                controls.addItem(K_S_EMU_DELAY_PLUS);
                controls.addItem(K_S_EMU_BACK);
                controls.addActivationButton(lilka::Button::B);
                bool done = false;
                while (!done) {
                    controls.setItem(
                        0,
                        K_S_EMU_PRECISION,
                        nullptr,
                        lilka::colors::White,
                        preferences.precisionMode ? K_S_EMU_ON : K_S_EMU_OFF
                    );
                    controls.setTitle(StringFormat(K_S_EMU_DELAY_FMT, unsigned(preferences.directionDelayMs)));
                    controls.update();
                    controls.draw(canvas);
                    queueDraw();
                    if (!controls.isFinished()) continue;
                    waitForRelease();
                    uint32_t oldDelay = preferences.directionDelayMs;
                    if (controls.getButton() == lilka::Button::B || controls.getCursor() == 3) {
                        done = true;
                        continue;
                    }
                    if (controls.getCursor() == 0) {
                        preferences.precisionMode = !preferences.precisionMode;
                        savePreferences();
                    } else {
                        if (controls.getCursor() == 1)
                            preferences.directionDelayMs =
                                oldDelay >= nesmenu::MIN_DELAY_MS + 50 ? oldDelay - 50 : nesmenu::MIN_DELAY_MS;
                        else
                            preferences.directionDelayMs =
                                oldDelay <= nesmenu::MAX_DELAY_MS - 50 ? oldDelay + 50 : nesmenu::MAX_DELAY_MS;
                        if (oldDelay != preferences.directionDelayMs) savePreferences();
                    }
                }
                break;
            }
            case 4:
                return SystemAction::Exit;
            case 1:
                return SystemAction::Screenshot;
            case 3: {
                lilka::Menu actions(K_S_EMU_MORE);
                actions.addItem(K_S_EMU_SAVE);
                actions.addItem(K_S_EMU_LOAD);
                actions.addItem(K_S_EMU_TURBO_A);
                actions.addItem(K_S_EMU_TURBO_B);
                actions.addItem(K_S_EMU_RESET);
                if (hasFrameskipSetting) actions.addItem(K_S_EMU_FRAMESKIP);
                actions.addItem(K_S_EMU_BACK);
                actions.addActivationButton(lilka::Button::B);
                bool done = false;
                while (!done) {
                    actions.setItem(
                        2, K_S_EMU_TURBO_A, nullptr, lilka::colors::White, preferences.turboA ? K_S_EMU_ON : K_S_EMU_OFF
                    );
                    actions.setItem(
                        3, K_S_EMU_TURBO_B, nullptr, lilka::colors::White, preferences.turboB ? K_S_EMU_ON : K_S_EMU_OFF
                    );
                    if (hasFrameskipSetting)
                        actions.setItem(
                            5,
                            K_S_EMU_FRAMESKIP,
                            nullptr,
                            lilka::colors::White,
                            automaticFrameskip ? K_S_EMU_AUTO : K_S_EMU_OFF
                        );
                    actions.update();
                    actions.draw(canvas);
                    queueDraw();
                    if (!actions.isFinished()) continue;
                    waitForRelease();
                    int choice = actions.getCursor();
                    if (actions.getButton() == lilka::Button::B || choice == (hasFrameskipSetting ? 6 : 5)) {
                        done = true;
                        continue;
                    }
                    if (choice == 0) return SystemAction::Save;
                    if (choice == 1) return SystemAction::Load;
                    if (choice == 2) {
                        preferences.turboA = !preferences.turboA;
                        savePreferences();
                    }
                    if (choice == 3) {
                        preferences.turboB = !preferences.turboB;
                        savePreferences();
                    }
                    if (choice == 5) automaticFrameskip = !automaticFrameskip;
                    if (choice == 4) {
                        lilka::Menu confirm(K_S_EMU_RESET_CONFIRM);
                        confirm.addItem(K_S_EMU_CANCEL);
                        confirm.addItem(K_S_EMU_RESET);
                        confirm.addActivationButton(lilka::Button::B);
                        do {
                            confirm.update();
                            confirm.draw(canvas);
                            queueDraw();
                        } while (!confirm.isFinished());
                        waitForRelease();
                        if (confirm.getButton() != lilka::Button::B && confirm.getCursor() == 1)
                            return SystemAction::Reset;
                    }
                }
                break;
            }
        }
    }
}

// A Select-first Start press freezes gameplay immediately. Release either before
// two seconds opens menu; holding both exits. Wait gate consumes remaining keys.
bool EmulatorMenuApp::holdExitRequested() {
    const uint32_t began = millis();
    while (true) {
        const auto state = lilka::controller.getState();
        if (!state.select.pressed || !state.start.pressed) return false;
        if (uint32_t(millis() - began) >= 2000) return true;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
