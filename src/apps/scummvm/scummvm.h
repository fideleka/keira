#pragma once

#include "keira/app.h"

class ScummVMManagerApp : public App {
public:
    explicit ScummVMManagerApp(const String& manifestPath);

    void run() override;

private:
    String manifestPath;
};
