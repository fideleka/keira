#include "devicemanager.h"
#if LILKA_VERSION != 1
#    include "elfcheck.h"
#    include "keira/ksystem.h"
#    include "keira/utils/string.h"
#    include <ArduinoJson.h>
#    include <dirent.h>
#    include <sys/stat.h>
#    include <esp_heap_caps.h>
#    include <esp_timer.h>
#    include <soc/soc.h>
#    include <driver/gpio.h>
#    include <cstring>
#    include <new>

namespace {
// Bound ArduinoJson's elastic allocator, including malicious/irrelevant manifest fields.
class ManifestAllocator : public ArduinoJson::Allocator {
    size_t used = 0;

public:
    void* allocate(size_t size) override {
        if (size > 8192 - used) return nullptr;
        auto* block =
            static_cast<size_t*>(heap_caps_malloc(size + sizeof(size_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!block) return nullptr;
        *block = size;
        used += size;
        return block + 1;
    }
    void deallocate(void* pointer) override {
        if (!pointer) return;
        auto* block = static_cast<size_t*>(pointer) - 1;
        used -= *block;
        heap_caps_free(block);
    }
    void* reallocate(void* pointer, size_t size) override {
        if (!pointer) return allocate(size);
        auto* old = static_cast<size_t*>(pointer) - 1;
        const size_t length = *old;
        if (size > 8192 - used + length) return nullptr;
        auto* block =
            static_cast<size_t*>(heap_caps_realloc(old, size + sizeof(size_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!block) return nullptr;
        used = used - length + size;
        *block = size;
        return block + 1;
    }
};
constexpr size_t FramePixels = 320 * 240;
constexpr unsigned RecordCount = 32;
struct Record {
    uint32_t magic, version, schema, count;
    char uuid[37], driverUuid[37];
    uint8_t enabled, reserved[5];
    struct Field {
        uint32_t id;
        int32_t value;
    } fields[KD_MAX_SETTINGS];
};
static_assert(sizeof(Record) < 256, "NVS record budget");
bool uuidValid(const char* uuid) {
    if (!uuid || strlen(uuid) != 36) return false;
    for (int i = 0; i < 36; ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (uuid[i] != '-') return false;
        } else if (!((uuid[i] >= '0' && uuid[i] <= '9') || (uuid[i] >= 'a' && uuid[i] <= 'f'))) return false;
    }
    return true;
}
bool pinAllowed(int pin) {
    return pin == 12 || pin == 13 || pin == 14 || pin == 21 || pin == 47 || pin == 48;
}
bool bounded(const char* text, size_t maximum) {
    return text && strnlen(text, maximum) < maximum;
}
struct FrameTarget {
    uint16_t* pixels;
    int width, height;
    void fillRect(int x, int y, int w, int h, uint16_t color) {
        const int left = x < 0 ? 0 : x, top = y < 0 ? 0 : y;
        const int right = x + w < width ? x + w : width, bottom = y + h < height ? y + h : height;
        for (int row = top; row < bottom; ++row)
            for (int col = left; col < right; ++col)
                pixels[row * width + col] = color;
    }
    void drawRect(int x, int y, int w, int h, uint16_t color) {
        fillRect(x, y, w, 1, color);
        fillRect(x, y + h - 1, w, 1, color);
        fillRect(x, y, 1, h, color);
        fillRect(x + w - 1, y, 1, h, color);
    }
};
void copyLayer(uint16_t* pixels, int width, int height, lilka::Canvas* canvas) {
    if (!canvas || !canvas->getFramebuffer()) return;
    for (int row = 0; row < canvas->height() && canvas->y() + row < height; ++row) {
        if (canvas->x() >= width) break;
        const int count = canvas->width() < width - canvas->x() ? canvas->width() : width - canvas->x();
        memcpy(
            pixels + (canvas->y() + row) * width + canvas->x(),
            canvas->getFramebuffer() + row * canvas->width(),
            count * sizeof(uint16_t)
        );
    }
}
} // namespace

DeviceManager::DeviceManager() : Service("devices") {
    setktStackSize(6144);
}
DeviceManager::~DeviceManager() {
    // Lifecycle owner must stop/join service before destroying it.
    deactivate();
    for (auto& slot : slots)
        if (slot.loaded) lilka_elf_deinit(&slot.elf);
    heap_caps_free(frame);
    vSemaphoreDelete(mutex);
}
void DeviceManager::delayMs(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms > 500 ? 500 : ms));
}
void DeviceManager::discover(bool skip) {
    bootSkipped = skip;
    NVS_LOCK;
    Preferences prefs;
    if (prefs.begin("devices", true)) {
        const uint8_t saved = prefs.getUChar("displayMode", Internal);
        if (saved <= Both) mode = static_cast<Mode>(saved);
        prefs.end();
    }
    NVS_UNLOCK;
    DIR* directory = opendir("/sd/drivers");
    if (!directory) return;
    unsigned scanned = 0;
    while (auto* item = readdir(directory)) {
        if (deviceCount == MaxDevices || ++scanned > 64) break;
        if (item->d_name[0] == '.' || strlen(item->d_name) > 48) continue;
        const String base = String("/sd/drivers/") + item->d_name;
        FILE* file = fopen((base + "/manifest.json").c_str(), "rb");
        if (!file) continue;
        auto* manifest = static_cast<char*>(heap_caps_malloc(4097, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!manifest) {
            fclose(file);
            break;
        }
        const size_t length = fread(manifest, 1, 4097, file);
        fclose(file);
        if (!length || length > 4096) {
            heap_caps_free(manifest);
            continue;
        }
        ManifestAllocator allocator;
        JsonDocument document(&allocator);
        const auto error = deserializeJson(
            document, static_cast<const char*>(manifest), length, DeserializationOption::NestingLimit(4)
        );
        // Force copied JSON strings by parsing a const buffer before freeing input.
        if (error) {
            heap_caps_free(manifest);
            continue;
        }
        heap_caps_free(manifest);
        auto& slot = slots[deviceCount++];
        slot.owner = this;
        strlcpy(slot.name, document["name"] | item->d_name, sizeof(slot.name));
        strlcpy(slot.uuid, document["device_uuid"] | "", sizeof(slot.uuid));
        strlcpy(slot.driverUuid, document["driver_uuid"] | "", sizeof(slot.driverUuid));
        strlcpy(slot.status, "Invalid manifest", sizeof(slot.status));
        if (!document["abi"].is<unsigned>() || document["abi"].as<unsigned>() != KD_ABI_VERSION ||
            !uuidValid(document["device_uuid"] | "") || !uuidValid(document["driver_uuid"] | "") ||
            strcmp(document["type"] | "", "display") || strcmp(document["module"] | "", "driver.so"))
            continue;
        document.clear(); // Metadata is copied; release parser memory before ELF relocation.
        bool duplicate = false;
        for (unsigned i = 0; i + 1 < deviceCount; ++i)
            if (!strcmp(slots[i].uuid, slot.uuid)) duplicate = true;
        if (duplicate) {
            strlcpy(slot.status, "Duplicate device UUID", sizeof(slot.status));
            continue;
        }
        if (skip) {
            strlcpy(slot.status, "Skipped: Select held at boot", sizeof(slot.status));
            restore(slot);
            continue;
        }
        if (!load(slot, (base + "/driver.so").c_str())) {
            restore(slot);
            continue;
        }
        restore(slot);
    }
    closedir(directory);
    // Prefer the previously active UUID, independent of filesystem enumeration order.
    char preferred[37] = {};
    NVS_LOCK;
    if (prefs.begin("devices", true)) {
        prefs.getString("activeDisplay", "").toCharArray(preferred, sizeof(preferred));
        prefs.end();
    }
    NVS_UNLOCK;
    for (unsigned i = 0; i < deviceCount; ++i)
        if (slots[i].enabled && slots[i].descriptor && !strcmp(preferred, slots[i].uuid) && activate(i)) return;
    for (unsigned i = 0; i < deviceCount; ++i)
        if (slots[i].enabled && slots[i].descriptor && activate(i)) return;
}
bool DeviceManager::load(Slot& slot, const char* path) {
    strlcpy(slot.status, "ELF load/validation failed", sizeof(slot.status));
    FILE* file = fopen(path, "rb");
    if (!file) {
        strlcpy(slot.status, "Missing driver.so", sizeof(slot.status));
        return false;
    }
    fseek(file, 0, SEEK_END);
    const long length = ftell(file);
    rewind(file);
    auto* bytes = length > 0 && length <= 65536
                      ? static_cast<uint8_t*>(heap_caps_malloc(length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))
                      : nullptr;
    bool valid =
        bytes && fread(bytes, 1, length, file) == static_cast<size_t>(length) && validateDriverElf(bytes, length);
    fclose(file);
    if (valid && (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 32768 ||
                  heap_caps_get_largest_free_block(MALLOC_CAP_EXEC) < 8192))
        valid = false;
    if (valid) valid = lilka_elf_relocate(&slot.elf, bytes, length) == 0;
    heap_caps_free(bytes); // ELF no longer needed; do not retain a second copy.
    const uintptr_t code = reinterpret_cast<uintptr_t>(slot.elf.ptext);
    // The existing loader's PSRAM instruction mirror cannot safely execute L32R literals.
    if (valid && !(code >= SOC_IRAM_LOW && code < SOC_IRAM_HIGH)) valid = false;
    slot.host = {
        sizeof(kd_host),
        KD_ABI_VERSION,
        &slot,
        registerDevice,
        setting,
        spiOpen,
        gpioWrite,
        spiWrite,
        spiPixels,
        spiClose,
        delayMs
    };
    if (valid) {
        char* arguments[] = {reinterpret_cast<char*>(&slot.host)};
        valid = lilka_elf_run(&slot.elf, 1, arguments) == 0 && slot.descriptor;
    }
    if (!valid) {
        lilka_elf_deinit(&slot.elf);
        slot.descriptor = nullptr;
        return false;
    }
    slot.loaded = true;
    strlcpy(slot.status, "Disabled", sizeof(slot.status));
    return true;
}
int32_t DeviceManager::registerDevice(void* context, const kd_device* d) {
    auto& slot = *static_cast<Slot*>(context);
    if (slot.descriptor || !d || d->size != sizeof(kd_device) || d->abi != KD_ABI_VERSION ||
        d->type != KD_TYPE_DISPLAY || !bounded(d->driver_uuid, 37) || !bounded(d->device_uuid, 37) ||
        strcmp(d->driver_uuid, slot.driverUuid) || strcmp(d->device_uuid, slot.uuid) || !bounded(d->name, 48) ||
        !bounded(d->version, 24) || !d->settings_version || d->setting_count > KD_MAX_SETTINGS ||
        (d->setting_count && !d->settings) || d->state_size > 256 || !d->enable || !d->disable || !d->present)
        return -1;
    for (unsigned i = 0; i < d->setting_count; ++i) {
        const auto& f = d->settings[i];
        if (!f.id || !bounded(f.label, 48) || f.kind < KD_SETTING_INTEGER || f.kind > KD_SETTING_CHOICE ||
            f.minimum > f.maximum || f.default_value < f.minimum || f.default_value > f.maximum || f.step <= 0 ||
            static_cast<int64_t>(f.maximum) - f.minimum > 100000000 ||
            (f.kind == KD_SETTING_BOOLEAN && (f.minimum != 0 || f.maximum != 1)) ||
            (f.kind == KD_SETTING_CHOICE && (f.minimum != 0 || f.maximum > 15 || !f.choices)))
            return -1;
        if (f.kind == KD_SETTING_CHOICE)
            for (int choice = 0; choice <= f.maximum; ++choice)
                if (!bounded(f.choices[choice], 32)) return -1;
        for (unsigned j = 0; j < i; ++j)
            if (d->settings[j].id == f.id) return -1;
        slot.values[i] = f.default_value;
    }
    slot.schema = d->settings_version;
    slot.descriptor = d;
    strlcpy(slot.name, d->name, sizeof(slot.name));
    return 0;
}
int32_t DeviceManager::setting(void* context, uint32_t id) {
    const auto& slot = *static_cast<Slot*>(context);
    if (slot.descriptor)
        for (unsigned i = 0; i < slot.descriptor->setting_count; ++i)
            if (slot.descriptor->settings[i].id == id) return slot.values[i];
    return 0;
}
bool DeviceManager::restore(Slot& slot) {
    bool good = true;
    NVS_LOCK;
    Preferences prefs;
    if (prefs.begin("devices", true)) {
        for (unsigned i = 0; i < RecordCount; ++i) {
            const String key = StringFormat("d%02u", i);
            if (!prefs.isKey(key.c_str())) continue;
            Record r = {};
            if (prefs.getBytesLength(key.c_str()) != sizeof(r) ||
                prefs.getBytes(key.c_str(), &r, sizeof(r)) != sizeof(r))
                continue;
            if (!memchr(r.uuid, 0, sizeof(r.uuid)) || strcmp(r.uuid, slot.uuid)) continue;
            if (r.magic != 0x4b445631 || r.version != 1 || r.count > KD_MAX_SETTINGS || r.enabled > 1 ||
                !memchr(r.driverUuid, 0, sizeof(r.driverUuid)) || strcmp(r.driverUuid, slot.driverUuid) ||
                (slot.descriptor && r.schema != slot.schema)) {
                slot.readOnly = true;
                good = false;
                break;
            }
            slot.enabled = r.enabled;
            for (unsigned f = 0; slot.descriptor && f < slot.descriptor->setting_count; ++f)
                for (unsigned j = 0; j < r.count; ++j)
                    if (slot.descriptor->settings[f].id == r.fields[j].id) {
                        const auto& definition = slot.descriptor->settings[f];
                        if (r.fields[j].value >= definition.minimum && r.fields[j].value <= definition.maximum)
                            slot.values[f] = r.fields[j].value;
                    }
            break;
        }
        prefs.end();
    }
    NVS_UNLOCK;
    if (!good && slot.descriptor)
        strlcpy(slot.status, "Unsupported saved settings; reset required", sizeof(slot.status));
    return good;
}
bool DeviceManager::persist(Slot& slot) {
    if (slot.readOnly || !slot.descriptor) return false;
    Record r = {};
    r.magic = 0x4b445631;
    r.version = 1;
    r.schema = slot.schema;
    r.count = slot.descriptor->setting_count;
    r.enabled = slot.enabled;
    strlcpy(r.uuid, slot.uuid, sizeof(r.uuid));
    strlcpy(r.driverUuid, slot.driverUuid, sizeof(r.driverUuid));
    for (unsigned f = 0; f < r.count; ++f) {
        r.fields[f].id = slot.descriptor->settings[f].id;
        r.fields[f].value = slot.values[f];
    }
    bool good = false;
    NVS_LOCK;
    Preferences prefs;
    if (prefs.begin("devices", false)) {
        int selected = -1, free = -1;
        for (unsigned i = 0; i < RecordCount; ++i) {
            const String key = StringFormat("d%02u", i);
            if (!prefs.isKey(key.c_str())) {
                if (free < 0) free = i;
                continue;
            }
            Record old = {};
            if (prefs.getBytesLength(key.c_str()) == sizeof(old) &&
                prefs.getBytes(key.c_str(), &old, sizeof(old)) == sizeof(old) &&
                memchr(old.uuid, 0, sizeof(old.uuid)) && !strcmp(old.uuid, slot.uuid)) {
                selected = i;
                break;
            }
        }
        if (selected < 0) selected = free;
        if (selected >= 0) good = prefs.putBytes(StringFormat("d%02u", selected).c_str(), &r, sizeof(r)) == sizeof(r);
        prefs.end();
    }
    NVS_UNLOCK;
    if (!good) strlcpy(slot.status, "NVS save failed / registry full", sizeof(slot.status));
    return good;
}
bool DeviceManager::route() {
    const bool external = active >= 0 && mode != Internal;
    if (active >= 0 && slots[active].descriptor->set_output &&
        slots[active].descriptor->set_output(&slots[active].host, slots[active].state, external)) {
        internal = true;
        routed = false;
        return false;
    }
    if (external && !frame) {
        const int w = lilka::display.width(), h = lilka::display.height();
        const size_t pixels = w > 0 && h > 0 ? static_cast<size_t>(w) * h : 0;
        if (pixels && pixels <= FramePixels) {
            frame = static_cast<uint16_t*>(
                heap_caps_malloc(pixels * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
            );
            if (frame) frameCapacity = pixels;
        }
    }
    if (external && !frame) {
        internal = true;
        routed = false;
        return false;
    }
    if (!external) {
        heap_caps_free(frame);
        frame = nullptr;
        frameCapacity = 0;
        pending = false;
    }
    routed = external;
    internal = !external || mode != External;
    ++generation;
    return true;
}
void DeviceManager::deactivate() {
    if (active >= 0) {
        auto& slot = slots[active];
        slot.descriptor->disable(&slot.host, slot.state);
        spiClose(&slot);
        heap_caps_free(slot.state);
        slot.state = nullptr;
        slot.active = false;
        strlcpy(slot.status, slot.enabled ? "Enabled, inactive" : "Disabled", sizeof(slot.status));
    }
    active = -1;
    pending = false;
    route();
}
bool DeviceManager::activate(unsigned index) {
    auto& slot = slots[index];
    if (!slot.descriptor || slot.readOnly || bootSkipped) return false;
    deactivate();
    slot.state = heap_caps_calloc(1, slot.descriptor->state_size ? slot.descriptor->state_size : 1, MALLOC_CAP_8BIT);
    if (!slot.state || slot.descriptor->enable(&slot.host, slot.state)) {
        if (slot.state) slot.descriptor->disable(&slot.host, slot.state);
        spiClose(&slot);
        heap_caps_free(slot.state);
        slot.state = nullptr;
        strlcpy(slot.status, "Activation failed: pins/SPI/memory", sizeof(slot.status));
        return false;
    }
    slot.active = true;
    active = index;
    if (!route()) {
        deactivate();
        strlcpy(slot.status, "PSRAM frame allocation failed", sizeof(slot.status));
        return false;
    }
    strlcpy(slot.status, "Active", sizeof(slot.status));
    return true;
}
bool DeviceManager::hasRegisteredDriver() const {
    for (unsigned i = 0; i < deviceCount; ++i)
        if (slots[i].descriptor) return true;
    return false;
}
bool DeviceManager::info(unsigned index, Info& result) {
    if (index >= deviceCount) return false;
    KMTX_LOCK(mutex);
    const auto& slot = slots[index];
    strlcpy(result.name, slot.name, sizeof(result.name));
    strlcpy(result.uuid, slot.uuid, sizeof(result.uuid));
    strlcpy(result.version, slot.descriptor ? slot.descriptor->version : "", sizeof(result.version));
    strlcpy(result.status, slot.status, sizeof(result.status));
    result.enabled = slot.enabled;
    result.active = slot.active && workerReady;
    if (slot.active && !workerReady)
        strlcpy(result.status, "Worker not running; internal fallback", sizeof(result.status));
    result.settingCount = slot.descriptor ? slot.descriptor->setting_count : 0;
    KMTX_UNLOCK(mutex);
    return true;
}
bool DeviceManager::settingInfo(unsigned index, unsigned field, SettingInfo& result) {
    if (index >= deviceCount) return false;
    KMTX_LOCK(mutex);
    const auto& slot = slots[index];
    bool good = slot.descriptor && field < slot.descriptor->setting_count;
    if (good) {
        const auto& f = slot.descriptor->settings[field];
        strlcpy(result.label, f.label, sizeof(result.label));
        result.value = slot.values[field];
        result.minimum = f.minimum;
        result.maximum = f.maximum;
        result.step = f.step;
        result.kind = f.kind;
        strlcpy(result.choice, f.kind == KD_SETTING_CHOICE ? f.choices[result.value] : "", sizeof(result.choice));
    }
    KMTX_UNLOCK(mutex);
    return good;
}
bool DeviceManager::setEnabled(unsigned index, bool enabled) {
    if (index >= deviceCount) return false;
    KMTX_LOCK(mutex);
    auto& slot = slots[index];
    const bool old = slot.enabled;
    const int previous = active;
    bool good = slot.descriptor && !slot.readOnly && (!enabled || workerReady);
    if (enabled && !workerReady)
        strlcpy(slot.status, "Driver worker unavailable; internal fallback", sizeof(slot.status));
    if (good && enabled) good = activate(index);
    if (good) {
        slot.enabled = enabled;
        good = persist(slot);
        if (!good) {
            slot.enabled = old;
            if (active == static_cast<int>(index)) deactivate();
        } else if (!enabled && active == static_cast<int>(index)) deactivate();
        if (good && enabled) {
            // Other enabled preferences remain intact, but only this display is active.
            NVS_LOCK;
            Preferences prefs;
            if (prefs.begin("devices", false)) {
                good = prefs.putString("activeDisplay", slot.uuid) > 0;
                prefs.end();
            } else good = false;
            NVS_UNLOCK;
        }
    }
    if (!good && previous >= 0 && active != previous) activate(previous);
    ++generation;
    KMTX_UNLOCK(mutex);
    return good;
}
bool DeviceManager::changeSetting(unsigned index, unsigned field, int direction) {
    if (index >= deviceCount || !direction) return false;
    KMTX_LOCK(mutex);
    auto& slot = slots[index];
    bool good = slot.descriptor && !slot.readOnly && field < slot.descriptor->setting_count;
    if (good) {
        const auto& f = slot.descriptor->settings[field];
        const int32_t old = slot.values[field];
        int64_t next = static_cast<int64_t>(old) + (direction > 0 ? f.step : -f.step);
        if (next > f.maximum) next = f.minimum;
        if (next < f.minimum) next = f.maximum;
        slot.values[field] = next;
        const bool wasActive = slot.active;
        good = !wasActive || activate(index);
        if (good) good = persist(slot);
        if (!good) {
            char error[80];
            strlcpy(error, slot.status, sizeof(error));
            slot.values[field] = old;
            if (wasActive) activate(index);
            strlcpy(slot.status, error, sizeof(slot.status));
        }
    }
    ++generation;
    KMTX_UNLOCK(mutex);
    return good;
}
bool DeviceManager::resetSettings(unsigned index) {
    if (index >= deviceCount) return false;
    KMTX_LOCK(mutex);
    auto& slot = slots[index];
    bool good = slot.descriptor;
    if (good) {
        int32_t previous[KD_MAX_SETTINGS];
        memcpy(previous, slot.values, sizeof(previous));
        const bool readOnly = slot.readOnly, wasActive = slot.active;
        slot.readOnly = false;
        for (unsigned f = 0; f < slot.descriptor->setting_count; ++f)
            slot.values[f] = slot.descriptor->settings[f].default_value;
        good = !wasActive || activate(index);
        if (good) good = persist(slot);
        if (!good) {
            char error[80];
            strlcpy(error, slot.status, sizeof(error));
            memcpy(slot.values, previous, sizeof(previous));
            slot.readOnly = readOnly;
            if (wasActive) activate(index);
            strlcpy(slot.status, error, sizeof(slot.status));
        }
    }
    ++generation;
    KMTX_UNLOCK(mutex);
    return good;
}
bool DeviceManager::setDisplayMode(Mode value) {
    if (value > Both) return false;
    KMTX_LOCK(mutex);
    const Mode previous = mode;
    mode = value;
    bool good = route();
    if (good) {
        NVS_LOCK;
        Preferences prefs;
        if (prefs.begin("devices", false)) {
            good = prefs.putUChar("displayMode", value) == 1;
            prefs.end();
        } else good = false;
        NVS_UNLOCK;
    }
    if (!good) {
        mode = previous;
        route();
    }
    KMTX_UNLOCK(mutex);
    return good;
}
bool DeviceManager::capture(
    lilka::Canvas* panel, lilka::Canvas* app, const lilka::VolumeOverlaySnapshot& overlay, bool sleeping
) {
    if (xSemaphoreTake(mutex, 0) != pdTRUE) return false;
    bool good = frame && active >= 0 && mode != Internal;
    const int w = lilka::display.width(), h = lilka::display.height();
    if (good && w > 0 && h > 0 && static_cast<size_t>(w) * h <= frameCapacity) {
        const int64_t start = esp_timer_get_time();
        memset(frame, 0, w * h * sizeof(uint16_t));
        if (!sleeping) {
            copyLayer(frame, w, h, panel);
            copyLayer(frame, w, h, app);
            FrameTarget target{frame, w, h};
            lilka::drawVolumeOverlay(target, overlay, w, h, millis());
        }
        width = w;
        height = h;
        pending = true;
        ++captureCount;
        captureUs = esp_timer_get_time() - start;
    } else good = false;
    KMTX_UNLOCK(mutex);
    return good;
}
void DeviceManager::run() {
    workerReady = true;
    ++generation;
    uint32_t previousLog = millis(), previousTransfer = 0, loggedTransfers = 0;
    while (true) {
        KMTX_LOCK(mutex);
        const uint32_t interval =
            active >= 0 && slots[active].descriptor->frame_interval_ms
                ? slots[active].descriptor->frame_interval_ms(&slots[active].host, slots[active].state)
                : 0;
        if (pending && frame && active >= 0 && mode != Internal && millis() - previousTransfer >= interval) {
            previousTransfer = millis();
            pending = false;
            const int64_t start = esp_timer_get_time();
            auto& slot = slots[active];
            const int result = slot.descriptor->present(&slot.host, slot.state, frame, width, height);
            transferUs = esp_timer_get_time() - start;
            ++transferCount;
            if (result) {
                deactivate();
                strlcpy(
                    slot.status,
                    spiFault ? "SPI timeout; restart required" : "Transfer failed; internal fallback",
                    sizeof(slot.status)
                );
            }
        }
        if (millis() - previousLog >= 10000 && active >= 0 && mode != Internal) {
            lilka::serial.log(
                "devices: captures=%u transfers=%u fps-x1000=%u capture=%uus transfer=%uus internal-free=%u largest=%u "
                "min=%u "
                "psram-free=%u stack=%u",
                captureCount,
                transferCount,
                static_cast<uint32_t>((uint64_t(transferCount - loggedTransfers) * 1000000) / (millis() - previousLog)),
                captureUs,
                transferUs,
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                uxTaskGetStackHighWaterMark(nullptr)
            );
            loggedTransfers = transferCount;
            previousLog = millis();
        }
        KMTX_UNLOCK(mutex);
        vTaskDelay(pdMS_TO_TICKS(routed ? 2 : 100));
    }
}
int32_t DeviceManager::spiOpen(void* context, int32_t clk, int32_t mosi, int32_t dc, int32_t reset, uint32_t hz) {
    auto& slot = *static_cast<Slot*>(context);
    auto& manager = *slot.owner;
    if (!slot.state || manager.spiOwner || !pinAllowed(clk) || !pinAllowed(mosi) || !pinAllowed(dc) ||
        !pinAllowed(reset) || clk == mosi || clk == dc || clk == reset || mosi == dc || mosi == reset || dc == reset ||
        hz < 1000000 || hz > 40000000)
        return -1;
    spi_bus_config_t bus = {};
    bus.mosi_io_num = mosi;
    bus.miso_io_num = -1;
    bus.sclk_io_num = clk;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 4096;
    if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return -1;
    manager.spiOwner = &slot;
    manager.clkPin = clk;
    manager.mosiPin = mosi;
    manager.dcPin = dc;
    manager.resetPin = reset;
    manager.dma = static_cast<uint8_t*>(heap_caps_malloc(4096, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    spi_device_interface_config_t device = {};
    device.clock_speed_hz = hz;
    device.spics_io_num = -1;
    device.queue_size = 1;
    if (!manager.dma || spi_bus_add_device(SPI2_HOST, &device, &manager.spi) != ESP_OK) {
        spiClose(context);
        return -1;
    }
    gpio_set_direction(static_cast<gpio_num_t>(dc), GPIO_MODE_OUTPUT);
    gpio_set_direction(static_cast<gpio_num_t>(reset), GPIO_MODE_OUTPUT);
    return 0;
}
int32_t DeviceManager::gpioWrite(void* context, int32_t pin, int32_t level) {
    const auto& manager = *static_cast<Slot*>(context)->owner;
    if (manager.spiOwner != context || (pin != manager.dcPin && pin != manager.resetPin)) return -1;
    return gpio_set_level(static_cast<gpio_num_t>(pin), !!level) == ESP_OK ? 0 : -1;
}
int32_t DeviceManager::spiWrite(void* context, int32_t data, const uint8_t* bytes, uint32_t size) {
    auto& manager = *static_cast<Slot*>(context)->owner;
    if (manager.spiFault || manager.spiOwner != context || !manager.spi || !bytes || size > 4096) return -1;
    gpio_set_level(static_cast<gpio_num_t>(manager.dcPin), !!data);
    memcpy(manager.dma, bytes, size);
    return manager.transmit(size * 8);
}
int32_t DeviceManager::spiPixels(void* context, const uint16_t* pixels, uint32_t count) {
    auto& manager = *static_cast<Slot*>(context)->owner;
    if (manager.spiFault || manager.spiOwner != context || !manager.spi || !pixels || count > FramePixels) return -1;
    gpio_set_level(static_cast<gpio_num_t>(manager.dcPin), 1);
    while (count) {
        const uint32_t length = count < 2048 ? count : 2048;
        for (unsigned i = 0; i < length; ++i) {
            manager.dma[i * 2] = pixels[i] >> 8;
            manager.dma[i * 2 + 1] = pixels[i] & 255;
        }
        if (manager.transmit(length * 16)) return -1;
        pixels += length;
        count -= length;
    }
    return 0;
}
void DeviceManager::spiClose(void* context) {
    auto& manager = *static_cast<Slot*>(context)->owner;
    if (manager.spiOwner != context || manager.spiFault) return;
    if (manager.spi) spi_bus_remove_device(manager.spi);
    spi_bus_free(SPI2_HOST);
    for (int pin : {manager.clkPin, manager.mosiPin, manager.dcPin, manager.resetPin})
        gpio_reset_pin(static_cast<gpio_num_t>(pin));
    heap_caps_free(manager.dma);
    manager.dma = nullptr;
    manager.spi = nullptr;
    manager.spiOwner = nullptr;
    manager.clkPin = manager.mosiPin = manager.dcPin = manager.resetPin = -1;
}

int DeviceManager::transmit(uint32_t bits) {
    transaction = {};
    transaction.length = bits;
    transaction.tx_buffer = dma;
    if (spi_device_polling_start(spi, &transaction, pdMS_TO_TICKS(100)) != ESP_OK) return -1;
    if (spi_device_polling_end(spi, pdMS_TO_TICKS(100)) != ESP_OK) {
        // Retain transaction/DMA/bus until reboot; never free an in-flight buffer.
        spiFault = true;
        return -1;
    }
    return 0;
}
#endif
