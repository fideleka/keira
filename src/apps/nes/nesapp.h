#pragma once
#include "keira/app.h"
#include "preferences.h"

class NesApp : public App {
public:
    explicit NesApp(String path);
    ~NesApp();
    nesmenu::Preferences preferences;
    nesmenu::DirectionFilter directionFilter;
    bool screenshotOnNextFrame = false;
    bool audioPaused = false; // guarded by OSD sound mutex
    String configFile;
    String configWarning;
    void loadPreferences();
    void savePreferences();
    enum class SystemAction { Resume, Exit, Reset, Save, Load, Screenshot };
    SystemAction showSystemMenu();
    void waitForRelease();
    void showNotice(const String& title);

private:
    void run() override;

    char* argv[1];
};
