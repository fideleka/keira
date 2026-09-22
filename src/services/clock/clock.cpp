#include "clock.h"

#include <stdlib.h>
#include <time.h>

#include "services/network/network.h"
#include "keira/ksystem.h"

namespace {
constexpr char CLOCK_NVS_TIMEZONE_KEY[] = "timezone";
constexpr size_t CLOCK_MAX_TIMEZONE_LENGTH = 63;
} // namespace

ClockService::ClockService() : Service("clock") {
    NVS_LOCK;
    Preferences prefs;
    prefs.begin(getName(), true);
    String savedTimezone = prefs.getString(CLOCK_NVS_TIMEZONE_KEY, CLOCK_DEFAULT_TIMEZONE);
    prefs.end();
    NVS_UNLOCK;

    if (!savedTimezone.isEmpty() && savedTimezone.length() <= CLOCK_MAX_TIMEZONE_LENGTH) {
        timezone = savedTimezone;
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

void ClockService::setTimezone(const String& newTimezone) {
    if (newTimezone.isEmpty() || newTimezone.length() > CLOCK_MAX_TIMEZONE_LENGTH) {
        return;
    }

    KMTX_LOCK(timezoneMutex);
    if (timezone == newTimezone) {
        KMTX_UNLOCK(timezoneMutex);
        return;
    }
    timezone = newTimezone;
    applyTimezone(timezone);
    KMTX_UNLOCK(timezoneMutex);

    NVS_LOCK;
    Preferences prefs;
    prefs.begin(getName(), false);
    prefs.putString(CLOCK_NVS_TIMEZONE_KEY, newTimezone);
    prefs.end();
    NVS_UNLOCK;
}

void ClockService::applyTimezone(const String& newTimezone) {
    setenv("TZ", newTimezone.c_str(), 1);
    tzset();
}
