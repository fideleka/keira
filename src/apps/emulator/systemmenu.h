#pragma once
#include "keira/app.h"
#include "apps/nes/preferences.h"
class EmulatorMenuApp : public App {
public:
    EmulatorMenuApp(const char* name, const String& romPath, const String& title) :
        App(name), romConfigPath(romPath), menuTitle(title) {
    }
    nesmenu::Preferences preferences;
    nesmenu::DirectionFilter directionFilter, verticalFilter;
    bool screenshotOnNextFrame = false;
    bool hasFrameskipSetting = false;
    bool automaticFrameskip = true;
    String configFile, configWarning;
    void loadPreferences();
    void savePreferences();
    enum class SystemAction { Resume, Exit, Reset, Save, Load, Screenshot };
    SystemAction showSystemMenu();
    void waitForRelease();
    void showNotice(const String& title);
    void clearGameCanvases() {
        canvas->fillScreen(lilka::colors::Black);
        backCanvas->fillScreen(lilka::colors::Black);
        directionFilter.reset();
        verticalFilter.reset();
    }

private:
    String romConfigPath, menuTitle;
};
