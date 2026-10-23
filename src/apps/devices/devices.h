#pragma once
#include "keira/app.h"
class DevicesApp : public App {
public:
    DevicesApp();

private:
    void run() override;
    void devicePage(unsigned index);
};
