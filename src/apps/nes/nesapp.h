#pragma once
#include "apps/emulator/systemmenu.h"
#include "preferences.h"

class NesApp : public EmulatorMenuApp {
public:
    void rememberSuccessfulLaunch();
    explicit NesApp(String path);
    ~NesApp();
    bool audioPaused = false; // guarded by OSD sound mutex

private:
    void run() override;

    char* argv[1];
    bool launchRemembered = false;
};
