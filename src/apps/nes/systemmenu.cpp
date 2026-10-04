#include "nesapp.h"
#include "keira/keira.h"

namespace {
bool anyPressed(const lilka::State& state) {
    return state.a.pressed || state.b.pressed || state.c.pressed || state.d.pressed || state.up.pressed ||
           state.down.pressed || state.left.pressed || state.right.pressed || state.select.pressed ||
           state.start.pressed;
}
} // namespace

void NesApp::waitForRelease() {
    nesmenu::ReleaseGate gate;
    while (gate.blocked(anyPressed(lilka::controller.getState())))
        vTaskDelay(pdMS_TO_TICKS(10));
    lilka::controller.resetState();
}

void NesApp::showNotice(const String& title) {
    waitForRelease();
    lilka::Menu notice(title);
    notice.addItem("Continue (A)");
    do {
        notice.update();
        notice.draw(canvas);
        queueDraw();
    } while (!notice.isFinished());
    waitForRelease();
}

void NesApp::loadPreferences() {
    char path[nesmenu::MAX_PATH_BYTES];
    if (!nesmenu::configPath(argv[0], path, sizeof(path))) {
        configWarning = "Config path too long";
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
            configWarning = "Config backup exists; writes blocked";
        } else if (result != nesmenu::ConfigResult::Ok && result != nesmenu::ConfigResult::Missing) {
            configWarning = result == nesmenu::ConfigResult::Unsupported ? "Config version unsupported; defaults"
                            : result == nesmenu::ConfigResult::Malformed ? "Config malformed/oversized; defaults"
                                                                         : "Config unreadable; defaults";
            lilka::serial.err("NES config load failed (%d): %s", int(result), configFile.c_str());
        }
    }
    if (configWarning.length()) {
        lilka::serial.err("NES: %s", configWarning.c_str());
        showNotice(configWarning);
    }
}

void NesApp::savePreferences() {
    directionFilter.reset();
    auto result =
        configFile.length() ? nesmenu::saveConfig(configFile.c_str(), preferences) : nesmenu::ConfigResult::IoError;
    if (result != nesmenu::ConfigResult::Ok) {
        configWarning = "Config NOT saved; session only";
        lilka::serial.err("NES config write refused/failed (%d): %s", int(result), configFile.c_str());
        showNotice(configWarning);
    } else {
        configWarning = "";
    }
}

NesApp::SystemAction NesApp::showSystemMenu() {
    waitForRelease();
    lilka::Menu menu("NES paused");
    menu.addItem("Resume");
    menu.addItem("Controls");
    menu.addItem("Turbo A (C)");
    menu.addItem("Turbo B (D)");
    menu.addItem("Save state");
    menu.addItem("Load state");
    menu.addItem("Screenshot");
    menu.addItem("Reset...");
    menu.addItem("Exit to launcher");
    menu.addActivationButton(lilka::Button::B);
    while (true) {
        menu.setItem(2, "Turbo A (C)", nullptr, lilka::colors::White, preferences.turboA ? "ON" : "OFF");
        menu.setItem(3, "Turbo B (D)", nullptr, lilka::colors::White, preferences.turboB ? "ON" : "OFF");
        menu.update();
        menu.draw(canvas);
        queueDraw();
        if (!menu.isFinished()) continue;
        waitForRelease();
        if (menu.getButton() == lilka::Button::B) return SystemAction::Resume;
        switch (menu.getCursor()) {
            case 0:
                return SystemAction::Resume;
            case 1: {
                lilka::Menu controls("Controls");
                controls.addItem("Precision L/R");
                controls.addItem("Delay -50ms");
                controls.addItem("Delay +50ms");
                controls.addItem("Back");
                controls.addActivationButton(lilka::Button::B);
                bool done = false;
                while (!done) {
                    controls.setItem(
                        0, "Precision L/R", nullptr, lilka::colors::White, preferences.precisionMode ? "ON" : "OFF"
                    );
                    controls.setTitle(String("L/R delay: ") + String(preferences.directionDelayMs) + "ms");
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
            case 2:
                preferences.turboA = !preferences.turboA;
                savePreferences();
                break;
            case 3:
                preferences.turboB = !preferences.turboB;
                savePreferences();
                break;
            case 4:
                return SystemAction::Save;
            case 5:
                return SystemAction::Load;
            case 6:
                return SystemAction::Screenshot;
            case 7: {
                lilka::Menu confirm("Reset game?");
                confirm.addItem("Cancel");
                confirm.addItem("Reset");
                confirm.addActivationButton(lilka::Button::B);
                do {
                    confirm.update();
                    confirm.draw(canvas);
                    queueDraw();
                } while (!confirm.isFinished());
                waitForRelease();
                if (confirm.getButton() != lilka::Button::B && confirm.getCursor() == 1) return SystemAction::Reset;
                break;
            }
            case 8:
                return SystemAction::Exit;
        }
    }
}
