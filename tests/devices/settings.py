#!/usr/bin/env python3
"""Exercise production driver settings and transactional Pins pages with scripted input."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/apps/devices/devices.cpp').read_text()
page = source[source.index('void DevicesApp::devicePage'):]
formatter = source[source.index('String settingValue'):source.index('} // namespace')]
mock = r'''
#include <cassert>
#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <cstdio>
#include <cstring>
struct String : std::string {
    using std::string::string;
    String(const std::string& s) : std::string(s) {
    }
    String(int n) : std::string(std::to_string(n)) {
    }
};
#define K_S_DEVICES_ENABLED           "Enabled"
#define K_S_ON                        "On"
#define K_S_OFF                       "Off"
#define K_S_STATUS                    "Status"
#define K_S_DEVICES_RESET             "Reset"
#define K_S_DEVICES_IDENTITY          "Identity"
#define K_S_ERROR                     "Error"
#define K_S_DEVICES_CHANGE_FAILED     "Failed"
#define K_S_DEVICES_RESET_CONFIRM     "Confirm"
#define K_S_DEVICES_PINS              "Pins"
#define K_S_DEVICES_APPLY             "Apply"
#define K_S_DEVICES_CANCEL            "Cancel"
#define K_S_DEVICES_PINS_APPLY_FAILED "Pins not applied"
#define KD_SETTING_CHOICE             3
#define KD_SETTING_BOOLEAN            2
#define KD_MAX_SETTINGS               12
#define pdMS_TO_TICKS(x)              x
void vTaskDelay(int) {
}
int alerts = 0;
void alert(const char*, String) {
    ++alerts;
}
bool confirm(const char*, const char*) {
    return true;
}
const char* driverText(const char* s) {
    return s;
}
namespace lilka {
enum class Button { A, B };
namespace colors {
int White = 1;
} // namespace colors
struct Key {
    bool justPressed = false;
};
struct State {
    Key up, down, left, right, d, a, b;
    bool selectHeld = false;
};
std::vector<State> script;
unsigned tick = 0, constructions = 0, updates = 0;
int lastScroll = 0;
struct Controller {
    State peekState() {
        return script.at(tick);
    }
} controller;
struct Menu {
    String title;
    int cursor = 0, scroll = 0, count = 0;
    bool finished = false, activate = true, horizontal = true;
    Button button = Button::A;
    Menu(const char* name) : title(name) {
        ++constructions;
    }
    void addItem(String name, void* = nullptr, int = 0, String value = "") {
        if (title == "Display") {
            assert(name.find(" pin") == String::npos);
            if (count == 2) assert(name == "Pins" && value == ">");
            if (count >= 3 && count < 8) assert(value.substr(0, 2) == "< " && value.substr(value.size() - 2) == " >");
        }
        if (title == "Pins" && count < 5) assert(value.substr(0, 2) == "< " && value.substr(value.size() - 2) == " >");
        ++count;
    }
    void addActivationButton(Button b) {
        if (b == Button::A) activate = true;
    }
    void removeActivationButton(Button b) {
        assert(b == Button::A);
        activate = false;
    }
    void setHorizontalNavigationEnabled(bool b) {
        horizontal = b;
    }
    void setCursor(int n) {
        cursor = n;
        scroll = std::max(0, cursor - 4);
    }
    int getCursor() {
        return cursor;
    }
    bool isFinished() {
        return finished;
    }
    Button getButton() {
        return button;
    }
    void setItem(int row, String, void*, int, String value) {
        assert(row == cursor && scroll == std::max(0, cursor - 4));
        assert(value.substr(0, 2) == "< " && value.substr(value.size() - 2) == " >");
        ++updates;
    }
    void update() {
        assert(!horizontal);
        auto s = script.at(tick++);
        if (s.down.justPressed && cursor + 1 < count) ++cursor;
        if (s.up.justPressed && cursor > 0) --cursor;
        scroll = std::max(0, cursor - 4);
        lastScroll = scroll;
        if (s.b.justPressed) {
            button = Button::B;
            finished = true;
        } else if (s.a.justPressed && activate) {
            button = Button::A;
            finished = true;
        }
    }
    void draw(void*) {
    }
};
} // namespace lilka
#define K_BTN_BACK lilka::Button::B
struct DeviceManager {
    struct Info {
        const char* name = "Display";
        const char* status = "Test error";
        const char* version = "1";
        const char* uuid = "id";
        bool enabled = true;
        unsigned settingCount = 10;
    };
    struct SettingInfo {
        char label[48] = {}, choice[32] = {};
        int32_t value = 0, minimum = 0, maximum = 2, step = 1;
        unsigned kind = KD_SETTING_CHOICE;
        bool pin = false;
    };
    struct SettingsDraft {
        unsigned count = 0;
        int32_t original[KD_MAX_SETTINGS] = {}, values[KD_MAX_SETTINGS] = {};
    };
    std::array<int32_t, KD_MAX_SETTINGS> values = {0, 1, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0};
    unsigned changes = 0, enabledChanges = 0, resets = 0, snapshots = 0, applies = 0;
    bool failChange = false, failApply = false, failSnapshot = false;
    std::vector<std::array<int32_t, KD_MAX_SETTINGS>> submitted;
    bool info(unsigned, Info&) {
        return true;
    }
    bool describe(unsigned field, int32_t value, SettingInfo& result) {
        const char* labels[] = {
            "Clock pin",
            "MOSI pin",
            "Reset pin",
            "DC pin",
            "Backlight pin",
            "Speed",
            "Orientation",
            "Refresh",
            "Drive",
            "Test"
        };
        std::snprintf(result.label, sizeof(result.label), "%s", labels[field]);
        result.value = value;
        result.pin = field < 5;
        result.maximum = field < 4 ? 5 : 2;
        if (value < 0 || value > result.maximum) return false;
        if (field < 4) {
            const int pins[] = {12, 13, 14, 21, 47, 48};
            std::snprintf(result.choice, sizeof(result.choice), "GPIO%d", pins[value]);
        } else std::snprintf(result.choice, sizeof(result.choice), "Choice%d", int(value));
        return true;
    }
    bool settingInfo(unsigned, unsigned field, SettingInfo& result) {
        return describe(field, values[field], result);
    }
    bool beginSettingsEdit(unsigned, SettingsDraft& draft) {
        ++snapshots;
        if (failSnapshot) return false;
        draft.count = 10;
        std::copy(values.begin(), values.end(), draft.original);
        std::copy(values.begin(), values.end(), draft.values);
        return true;
    }
    bool previewSetting(unsigned, unsigned field, const SettingsDraft& draft, SettingInfo& result) {
        return describe(field, draft.values[field], result);
    }
    bool applySettings(unsigned, const SettingsDraft& draft) {
        ++applies;
        std::array<int32_t, KD_MAX_SETTINGS> snapshot;
        std::copy(draft.values, draft.values + KD_MAX_SETTINGS, snapshot.begin());
        submitted.push_back(snapshot);
        if (failApply) {
            failApply = false;
            return false;
        }
        values = snapshot;
        return true;
    }
    bool changeSetting(unsigned, unsigned field, int direction) {
        assert(field >= 5);
        ++changes;
        if (failChange) {
            failChange = false;
            return false;
        }
        values[field] = (values[field] + direction + 3) % 3;
        return true;
    }
    bool setEnabled(unsigned, bool) {
        ++enabledChanges;
        return true;
    }
    bool resetSettings(unsigned) {
        ++resets;
        return true;
    }
} manager;
struct {
    DeviceManager* devices = &manager;
} ksystem;
struct DevicesApp {
    void* canvas = nullptr;
    void queueDraw() {
    }
    void devicePage(unsigned);
    void pinsPage(unsigned);
};
'''
checks = r'''
int main() {
    using namespace lilka;
    State down;
    down.down.justPressed = true;
    State a;
    a.a.justPressed = true;
    State left;
    left.left.justPressed = true;
    State right;
    right.right.justPressed = true;
    State b;
    b.b.justPressed = true;
    State select = a;
    select.selectHeld = true;
    State both = left;
    both.right.justPressed = true;
    DevicesApp app;
    auto reset = [&] {
        manager = DeviceManager{};
        script.clear();
        tick = constructions = updates = 0;
        alerts = 0;
    };
    auto enterPins = [&] { script = {down, down, a}; };
    // Non-pin values still cycle inline and preserve scroll on failures.
    reset();
    script.assign(7, down);
    script.insert(script.end(), {a, right, a, left, select, both, b});
    app.devicePage(0);
    assert(manager.changes == 4 && manager.values[9] == 2 && constructions == 1 && lastScroll == 3 && alerts == 0);
    tick = constructions = 0;
    manager.failChange = true;
    script.assign(7, down);
    script.insert(script.end(), {a, a, b});
    app.devicePage(0);
    assert(manager.changes == 6 && manager.values[9] == 0 && alerts == 1 && constructions == 1);
    // Ordinary actions still activate.
    reset();
    script = {a, b};
    app.devicePage(0);
    assert(manager.enabledChanges == 1 && constructions == 2);
    // Back cancels even a temporarily conflicting draft, with no Apply or live edits.
    reset();
    enterPins();
    script.insert(script.end(), {a, b, b});
    const auto original = manager.values;
    app.devicePage(0);
    assert(
        manager.snapshots == 1 && manager.applies == 0 && manager.changes == 0 && manager.values == original &&
        updates == 1
    );
    // Explicit Cancel has the same semantics.
    reset();
    enterPins();
    script.push_back(a);
    script.insert(script.end(), 6, down);
    script.insert(script.end(), {a, b});
    app.devicePage(0);
    assert(manager.applies == 0 && manager.changes == 0 && manager.values == original);
    // A complete pin swap is submitted once, not as intermediate changes.
    reset();
    enterPins();
    script.insert(script.end(), {a, down, left});
    script.insert(script.end(), 4, down);
    script.insert(script.end(), {a, b});
    app.devicePage(0);
    assert(
        manager.applies == 1 && manager.snapshots == 1 && manager.changes == 0 && manager.values[0] == 1 &&
        manager.values[1] == 0
    );
    for (unsigned i = 2; i < manager.values.size(); ++i)
        assert(manager.values[i] == original[i]);
    // Failed Apply retains draft and cursor; retry does not resnapshot or lose edits.
    reset();
    manager.failApply = true;
    enterPins();
    script.insert(script.end(), {a, down, left});
    script.insert(script.end(), 4, down);
    script.insert(script.end(), {a, a, b});
    app.devicePage(0);
    assert(
        manager.applies == 2 && manager.snapshots == 1 && alerts == 1 && manager.submitted[0] == manager.submitted[1]
    );
    assert(manager.values[0] == 1 && manager.values[1] == 0 && manager.changes == 0);
    // Select chords and conflicting direction keys do not edit the draft.
    reset();
    enterPins();
    script.insert(script.end(), {select, both, b, b});
    app.devicePage(0);
    assert(!updates && !manager.applies && !manager.changes && manager.values == original);
    // Failure to obtain a snapshot does not enter the pin editor or mutate settings.
    reset();
    manager.failSnapshot = true;
    enterPins();
    script.push_back(b);
    app.devicePage(0);
    assert(alerts == 1 && manager.snapshots == 1 && !manager.applies && !manager.changes);
    puts(
        "PASS: production Pins draft/apply/cancel/back, complete swap, failure retry retention, main-page pin "
        "exclusion and unchanged scalar controls"
    );
}
'''
with tempfile.TemporaryDirectory(prefix='keira-pin-settings-') as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(mock + formatter + page + checks)
    for flags in ([], ['-fsanitize=address,undefined', '-fno-pie', '-no-pie']):
        subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags,
                        str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
        subprocess.run([str(p / 'test')], check=True)
