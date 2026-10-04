#pragma once
#include "keira/app.h"

class NesApp : public App {
public:
    void rememberSuccessfulLaunch();
    explicit NesApp(String path);
    ~NesApp();

private:
    void run() override;

    char* argv[1];
    bool launchRemembered = false;
};
