#pragma once

#include "keira/service.h"
#include "keira/mutex.h"

constexpr char CLOCK_TIMEZONE_UTC[] = "UTC0";
constexpr char CLOCK_TIMEZONE_KYIV[] = "EET-2EEST,M3.5.0/3,M10.5.0/4";
constexpr char CLOCK_TIMEZONE_TORONTO[] = "EST5EDT,M3.2.0/2,M11.1.0/2";
constexpr char CLOCK_DEFAULT_TIMEZONE[] = "EET-2EEST,M3.5.0/3,M10.5.0/4";

class ClockService : public Service {
public:
    ClockService();

    struct tm getTime();
    String getTimezone();
    void setTimezone(const String& timezone);

private:
    void run() override;
    void applyTimezone(const String& timezone);

    String timezone = CLOCK_DEFAULT_TIMEZONE;
    SemaphoreHandle_t timezoneMutex = xSemaphoreCreateMutex();
};
