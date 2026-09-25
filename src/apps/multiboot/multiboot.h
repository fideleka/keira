#pragma once
#include "keira/app.h"

class MultiBootApp : public App {
public:
    explicit MultiBootApp(
        const String& path = "", const String& command = "", const String& gameManifest = "",
        const String& gameTitle = ""
    );

    void run() override;

private:
    void fileLoadAsRom(const String& path);
    String firmwarePath = "";
    String command;
    String gameManifest;
    String gameTitle;
};
