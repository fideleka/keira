#pragma once
#include "keira/app.h"
#include "startupdiagnostics.h"

class NesApp : public App {
public:
    explicit NesApp(String path);
    ~NesApp();
    void reportStartupRender();

private:
    void run() override;

    char* argv[1];
    NesStartupDiagnostics startupDiagnostics;
};
