#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <functional>
#include <cassert>
#include <cstdarg>
#include <vector>
#include <utility>
inline uint32_t& hostMillis() {
    static uint32_t value = 0;
    return value;
}
inline uint32_t millis() {
    return hostMillis();
}
constexpr int WIFI_STORAGE_RAM = 0, ESP_OK = 0;
inline int esp_wifi_set_storage(int storage) {
    assert(storage == WIFI_STORAGE_RAM);
    return ESP_OK;
}
constexpr int WIFI_SCAN_RUNNING = -1, WIFI_SCAN_FAILED = -2;
inline int esp_wifi_scan_stop() {
    return 0;
}
using SemaphoreHandle_t = unsigned;
inline unsigned& nextMutex() {
    static unsigned value = 1;
    return value;
}
inline bool* lockedMutexes() {
    static bool values[32]{};
    return values;
}
inline SemaphoreHandle_t xSemaphoreCreateMutex() {
    return nextMutex()++;
}
constexpr int portMAX_DELAY = -1, portTICK_PERIOD_MS = 1;
#define pdMS_TO_TICKS(x) (x)
inline bool xSemaphoreTake(unsigned handle, int) {
    assert(handle < 32 && !lockedMutexes()[handle]);
    lockedMutexes()[handle] = true;
    return true;
}
inline void xSemaphoreGive(unsigned handle) {
    assert(lockedMutexes()[handle]);
    lockedMutexes()[handle] = false;
}
struct StopService {};
inline void vTaskDelay(int) {
    throw StopService{};
}
enum NetworkEvent {
    ARDUINO_EVENT_WIFI_STA_START,
    ARDUINO_EVENT_WIFI_STA_CONNECTED,
    ARDUINO_EVENT_WIFI_STA_DISCONNECTED,
    ARDUINO_EVENT_WIFI_STA_GOT_IP,
    ARDUINO_EVENT_WIFI_STA_GOT_IP6,
    ARDUINO_EVENT_WIFI_STA_LOST_IP,
    ARDUINO_EVENT_WIFI_STA_STOP
};
using WiFiEvent_t = NetworkEvent;
struct WiFiEventInfo_t {
    struct {
        int reason;
    } wifi_sta_disconnected;
};
using wifi_power_t = int;
using wifi_mode_t = int;
constexpr int WIFI_POWER_19_5dBm = 78, WIFI_STA = 1, WIFI_OFF = 0, WL_CONNECTED = 3, WL_DISCONNECTED = 6;
constexpr int ESP_ERR_WIFI_NOT_INIT = 1;
inline int esp_wifi_get_mode(int* mode) {
    *mode = WIFI_STA;
    return 0;
}
struct IPAddress {
    String toString() {
        return "192.0.2.1";
    }
};
struct WifiHAL {
    String name, password;
    int state = WL_DISCONNECTED;
    bool reconnect = true;
    unsigned disconnects = 0;
    int scanState = WIFI_SCAN_FAILED, scanStart = WIFI_SCAN_FAILED;
    std::vector<std::pair<String, int32_t>> visible;
    std::vector<String> attempts;
    int scanComplete() {
        return scanState;
    }
    int scanNetworks(bool) {
        scanState = scanStart;
        return scanState;
    }
    void scanDelete() {
        scanState = WIFI_SCAN_FAILED;
    }
    String SSID(int index) {
        return visible.at(index).first;
    }
    int32_t RSSI(int index) {
        return visible.at(index).second;
    }
    std::function<void(WiFiEvent_t, WiFiEventInfo_t)> callback;
    void persistent(bool value) {
        assert(!value);
    }
    void setTxPower(int) {
    }
    void macAddress(uint8_t* bytes) {
        for (unsigned i = 0; i < 6; ++i)
            bytes[i] = i;
    }
    void setHostname(const char*) {
    }
    template <class Callback>
    void onEvent(Callback value) {
        callback = value;
    }
    void mode(int) {
    }
    void disconnect(bool = false, bool = false) {
        ++disconnects;
        state = WL_DISCONNECTED;
    }
    void setAutoReconnect(bool value) {
        reconnect = value;
    }
    void begin(const char* ssid, const char* secret) {
        attempts.push_back(ssid);
        name = ssid;
        password = secret;
        state = WL_DISCONNECTED;
    }
    String SSID() {
        return name;
    }
    int status() {
        return state;
    }
    int RSSI() {
        return -55;
    }
    IPAddress localIP() {
        return {};
    }
    void event(NetworkEvent event) {
        if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) state = WL_CONNECTED;
        callback(event, {});
    }
};
extern WifiHAL WiFi;
namespace lilka {
struct SerialHAL {
    void log(const char*, ...) {
    }
    void err(const char*, ...) {
    }
};
extern SerialHAL serial;
} // namespace lilka
struct SystemHAL {
    unsigned nvsMTX = xSemaphoreCreateMutex();
};
extern SystemHAL ksystem;
class Service {
public:
    explicit Service(const char*) {
    }
    const char* getName() {
        return "network";
    }
    bool getEnabled() {
        return false;
    }

private:
    virtual void run() = 0;
};
