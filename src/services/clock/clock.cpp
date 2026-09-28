#include "clock.h"

#include <stdlib.h>
#include <time.h>

#include "services/network/network.h"
#include "keira/ksystem.h"

namespace {
constexpr char CLOCK_NVS_TIMEZONE_KEY[] = "timezone";
constexpr char CLOCK_NVS_TIMEZONE_PRESET_KEY[] = "timezonePreset";
constexpr size_t CLOCK_MAX_TIMEZONE_LENGTH = 63;
} // namespace

ClockService::ClockService() : Service("clock") {
    NVS_LOCK;
    Preferences prefs;
    String savedTimezone = CLOCK_DEFAULT_TIMEZONE;
    int savedPresetId = -1;
    if (prefs.begin(getName(), true)) {
        savedTimezone = prefs.getString(CLOCK_NVS_TIMEZONE_KEY, CLOCK_DEFAULT_TIMEZONE);
        savedPresetId = prefs.getInt(CLOCK_NVS_TIMEZONE_PRESET_KEY, -1);
        prefs.end();
    }
    NVS_UNLOCK;

    if (!savedTimezone.isEmpty() && savedTimezone.length() <= CLOCK_MAX_TIMEZONE_LENGTH) {
        timezone = savedTimezone;
        timezonePresetId = savedPresetId;
    }
    applyTimezone(timezone);
}

void ClockService::run() {
    while (1) {
        NetworkService* network = reinterpret_cast<NetworkService*>(ksystem.services["network"]);
        if (network == NULL) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        if (network->getnetworkState() == NetworkState::NETWORK_STATE_ONLINE) {
            lilka::serial.log("ClockService: Setting time from NTP server");
            KMTX_LOCK(timezoneMutex);
            configTzTime(timezone.c_str(), "pool.ntp.org", "time.nist.gov");
            KMTX_UNLOCK(timezoneMutex);
            // Delay for 12 hours
            vTaskDelay(1000 * 60 * 60 * 12 / portTICK_PERIOD_MS);
        } else {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }
}

struct tm ClockService::getTime() {
    struct timeval tv;
    struct tm timeinfo;
    gettimeofday(&tv, NULL);
    time_t now = tv.tv_sec;
    KMTX_LOCK(timezoneMutex);
    localtime_r(&now, &timeinfo);
    KMTX_UNLOCK(timezoneMutex);
    return timeinfo;
}

String ClockService::getTimezone() {
    KMTX_LOCK(timezoneMutex);
    String currentTimezone = timezone;
    KMTX_UNLOCK(timezoneMutex);
    return currentTimezone;
}

int ClockService::getTimezonePresetId() {
    KMTX_LOCK(timezoneMutex);
    int currentPresetId = timezonePresetId;
    KMTX_UNLOCK(timezoneMutex);
    return currentPresetId;
}

void ClockService::setTimezone(const String& newTimezone, bool persist, int presetId) {
    if (newTimezone.isEmpty() || newTimezone.length() > CLOCK_MAX_TIMEZONE_LENGTH) {
        return;
    }

    KMTX_LOCK(timezoneMutex);
    if (timezone != newTimezone) {
        timezone = newTimezone;
        applyTimezone(timezone);
    }
    timezonePresetId = presetId;
    KMTX_UNLOCK(timezoneMutex);

    if (!persist) {
        return;
    }

    NVS_LOCK;
    Preferences prefs;
    prefs.begin(getName(), false);
    prefs.putString(CLOCK_NVS_TIMEZONE_KEY, newTimezone);
    prefs.putInt(CLOCK_NVS_TIMEZONE_PRESET_KEY, presetId);
    prefs.end();
    NVS_UNLOCK;
}

void ClockService::applyTimezone(const String& newTimezone) {
    setenv("TZ", newTimezone.c_str(), 1);
    tzset();
}
