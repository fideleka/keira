#pragma once

#include "keira/app.h"

class ScummVMManagerApp : public App {
public:
    explicit ScummVMManagerApp(const String& manifestPath, bool confirmLaunch = true);

    void run() override;

private:
    String manifestPath;
    bool confirmLaunch;
};
