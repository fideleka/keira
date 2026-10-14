#pragma once
#include "apps/emulator/systemmenu.h"
#include "preferences.h"
#include "keira/fractional_ticks.h"
#if LILKA_VERSION == 2
#    include <freertos/timers.h>
#endif

class NesApp : public EmulatorMenuApp {
public:
    void rememberSuccessfulLaunch();
    explicit NesApp(String path);
    ~NesApp();
#if LILKA_VERSION == 2
    // Timer-daemon-owned while active; reset only after the stop barrier.
    FractionalTicks timerPeriod{configTICK_RATE_HZ, 60};
    TickType_t timerDeadline = 0;
    TimerCallbackFunction_t timerCallback = nullptr;
    portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
    bool timerEnabled = false; // guarded with nonblocking timer-command enqueue
#endif
    bool audioPaused = false; // guarded by OSD sound mutex
#if KEIRA_MEASURE
    // Emulator-task-owned: sample each category independently, once in 16 frames.
    uint32_t coreSampleDraw = 0, coreSampleSkip = 0;
#endif

private:
    void run() override;

    char* argv[1];
    bool launchRemembered = false;
};
