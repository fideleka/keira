#pragma once

#include "gbcore.h"
#include "keira/app.h"

class GameBoyApp : public App {
public:
    explicit GameBoyApp(const String& path);
    ~GameBoyApp() override;

private:
    void run() override;
    static void drawLine(void* context, const uint8_t* pixels, uint8_t line, bool color, const uint16_t* palette);
    bool loadRom();
    bool loadSave();
    bool writeSave();
    bool initAudio();
    void writeAudio(uint8_t& fractionalSamples);
    void stopAudio();
    void releaseGame();

    String romPath;
    String savePath;
    uint8_t* rom = nullptr;
    size_t romSize = 0;
    uint8_t* save = nullptr;
    size_t saveSize = 0;
    GbCore* core = nullptr;
    int16_t* audioFrame = nullptr;
    bool audioReady = false;
    uint32_t volumeLevel = 100;
};
