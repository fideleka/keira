#pragma once

#include "keira/service.h"
#include "keira/mutex.h"

constexpr char CLOCK_TIMEZONE_UTC[] = "UTC0";
constexpr char CLOCK_TIMEZONE_KYIV[] = "EET-2EEST,M3.5.0/3,M10.5.0/4";
constexpr char CLOCK_TIMEZONE_KHARKIV[] = "EET-2EEST,M3.5.0/3,M10.5.0/4";
constexpr char CLOCK_TIMEZONE_TORONTO[] = "EST5EDT,M3.2.0/2,M11.1.0/2";
constexpr char CLOCK_TIMEZONE_LOS_ANGELES[] = "PST8PDT,M3.2.0/2,M11.1.0/2";
constexpr char CLOCK_TIMEZONE_WARSAW[] = "CET-1CEST,M3.5.0/2,M10.5.0/3";
constexpr char CLOCK_TIMEZONE_MADRID[] = "CET-1CEST,M3.5.0/2,M10.5.0/3";
constexpr char CLOCK_TIMEZONE_ISTANBUL[] = "TRT-3";
constexpr char CLOCK_TIMEZONE_BEIJING[] = "CST-8";
constexpr char CLOCK_DEFAULT_TIMEZONE[] = "EET-2EEST,M3.5.0/3,M10.5.0/4";

class ClockService : public Service {
public:
    ClockService();

    struct tm getTime();
    String getTimezone();
    int getTimezonePresetId();
    void setTimezone(const String& timezone, bool persist = true, int presetId = -1);

private:
    void run() override;
    void applyTimezone(const String& timezone);

    String timezone = CLOCK_DEFAULT_TIMEZONE;
    int timezonePresetId = -1;
    SemaphoreHandle_t timezoneMutex = xSemaphoreCreateMutex();
};
