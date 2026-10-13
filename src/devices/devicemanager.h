#pragma once
#include "keira/service.h"
#include "../../drivers/driver_api.h"
#include <lilka/dynloader.h>
#include <driver/spi_master.h>
#include <atomic>

class DeviceManager : public Service {
public:
    static constexpr unsigned MaxDevices = 4;
    enum Mode : uint8_t { Internal, External, Both };
    struct Info {
        char name[48], uuid[37], version[24], status[80];
        bool enabled, active;
        uint32_t settingCount;
    };
    struct SettingInfo {
        char label[48], choice[32];
        int32_t value, minimum, maximum, step;
        uint32_t kind;
    };
    DeviceManager();
    ~DeviceManager();
    void discover(bool skip);
    bool hasRegisteredDriver() const;
    bool extensionPinOwned(int pin) const;
    unsigned count() const {
        return deviceCount;
    }
    bool info(unsigned index, Info& result);
    bool settingInfo(unsigned device, unsigned field, SettingInfo& result);
    bool setEnabled(unsigned index, bool enabled);
    bool changeSetting(unsigned index, unsigned field, int direction);
    bool resetSettings(unsigned index);
    Mode displayMode() const {
        return mode.load();
    }
    bool setDisplayMode(Mode value);
    bool externalOutput() const {
        return workerReady.load() && routed.load();
    }
    bool internalOutput() const {
        return !workerReady.load() || internal.load();
    }
    uint32_t invalidation() const {
        return generation.load();
    }
    // Caller holds compositor layer locks. Never blocks on driver transfers.
    bool capture(lilka::Canvas* panel, lilka::Canvas* app, const lilka::VolumeOverlaySnapshot& overlay, bool sleeping);
    // Both mode mirrors at most 15 FPS; check before taking any canvas locks.
    bool captureDue() const {
        return displayMode() != Both || millis() - lastCaptureMs.load() >= 67;
    }

private:
    struct Slot {
        DeviceManager* owner = nullptr;
        kd_host host = {};
        lilka_elf_t elf = {};
        const kd_device* descriptor = nullptr;
        char driverUuid[37] = {}, uuid[37] = {}, name[48] = {}, status[80] = {};
        uint32_t schema = 0;
        int32_t values[KD_MAX_SETTINGS] = {};
        bool loaded = false, enabled = false, active = false, readOnly = false;
        void* state = nullptr;
    };
    Slot slots[MaxDevices];
    unsigned deviceCount = 0;
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    std::atomic<Mode> mode{Internal};
    std::atomic<bool> internal{true};
    std::atomic<bool> routed{false};
    std::atomic<bool> workerReady{false};
    std::atomic<uint32_t> ownedPins{0};
    std::atomic<uint32_t> generation{1};
    std::atomic<uint32_t> lastCaptureMs{0};
    int active = -1;
    uint16_t* frame = nullptr;
    size_t frameCapacity = 0;
    uint16_t width = 0, height = 0;
    bool pending = false, bootSkipped = false, frameValid = false;
    uint32_t dirtyRows[10] = {};
    uint32_t unchangedCaptures = 0, fullTransfers = 0, rowTransfers = 0, transferPixels = 0, spiHz = 0;
    int presentPending(Slot& slot);
    uint32_t captureCount = 0, transferCount = 0, captureUs = 0, transferUs = 0;
    spi_device_handle_t spi = nullptr;
    uint8_t* dma = nullptr;
    Slot* spiOwner = nullptr;
    bool spiFault = false;
    spi_transaction_t transaction = {};
    int transmit(uint32_t bits);
    static int32_t fail(Slot& slot, const char* operation, int32_t error);
    int dcPin = -1, resetPin = -1, clkPin = -1, mosiPin = -1;
    void run() override;
    bool load(Slot& slot, const char* path);
    bool restore(Slot& slot);
    bool persist(Slot& slot);
    bool activate(unsigned index);
    void deactivate();
    bool route();
    static int32_t registerDevice(void* context, const kd_device* descriptor);
    static int32_t setting(void* context, uint32_t id);
    static int32_t spiOpen(void* context, int32_t clk, int32_t mosi, int32_t dc, int32_t reset, uint32_t hz);
    static int32_t gpioWrite(void* context, int32_t pin, int32_t level);
    static int32_t spiWrite(void* context, int32_t data, const uint8_t* bytes, uint32_t size);
    static int32_t spiPixels(void* context, const uint16_t* pixels, uint32_t count);
    static void spiClose(void* context);
    static void delayMs(uint32_t ms);
};
