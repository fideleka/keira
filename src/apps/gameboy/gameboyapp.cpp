#include "gameboyapp.h"
#include "keira/keira_lang.h"

#include <esp_heap_caps.h>
#include <esp_timer.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/stat.h>

namespace {
constexpr size_t kMinimumRomSize = 0x150;
constexpr size_t kRomBankSize = 0x4000;
constexpr int64_t kFrameUs = 16743;
constexpr uint32_t kExitHoldMs = 1500;
constexpr uint16_t kDmgPalette[4] = {0xFFFF, 0xBDF7, 0x738E, 0x0000};
} // namespace

GameBoyApp::GameBoyApp(const String& path) : App("Game Boy"), romPath(path), savePath(path + ".sav") {
    setktStackSize(8192);
    setFlags(AppFlags::APP_FLAG_FULLSCREEN);
}

GameBoyApp::~GameBoyApp() {
    releaseGame();
}

bool GameBoyApp::loadRom() {
    struct stat info;
    if (stat(romPath.c_str(), &info) != 0 || info.st_size < static_cast<off_t>(kMinimumRomSize)) return false;

    FILE* file = fopen(romPath.c_str(), "rb");
    if (!file) return false;
    uint8_t header[kMinimumRomSize];
    bool valid = fread(header, 1, sizeof(header), file) == sizeof(header);
    if (valid) {
        const uint8_t bankCode = header[0x148];
        const uint8_t ramCode = header[0x149];
        valid = bankCode <= 8 && ramCode <= 5;
        if (valid) romSize = (static_cast<size_t>(2) << bankCode) * kRomBankSize;
        valid = valid && romSize <= static_cast<size_t>(info.st_size);
    }
    if (valid) {
        rom = static_cast<uint8_t*>(heap_caps_malloc(romSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        valid = rom && fseek(file, 0, SEEK_SET) == 0 && fread(rom, 1, romSize, file) == romSize;
    }
    fclose(file);
    return valid;
}

bool GameBoyApp::loadSave() {
    saveSize = gbcore_save_size(core);
    if (!saveSize) return true;
    save = static_cast<uint8_t*>(heap_caps_malloc(saveSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!save) return false;
    memset(save, 0xFF, saveSize);

    FILE* file = fopen(savePath.c_str(), "rb");
    if (file) {
        fread(save, 1, saveSize, file);
        fclose(file);
    }
    gbcore_set_save(core, save, saveSize);
    return true;
}

bool GameBoyApp::writeSave() {
    if (!saveSize) return true;
    const String temporary = savePath + ".tmp";
    const String backup = savePath + ".bak";
    FILE* file = fopen(temporary.c_str(), "wb");
    if (!file) return false;
    bool written = fwrite(save, 1, saveSize, file) == saveSize && fflush(file) == 0;
    if (fclose(file) != 0) written = false;
    if (!written) return false;

    if (rename(temporary.c_str(), savePath.c_str()) == 0) return true;
    struct stat info;
    if (stat(savePath.c_str(), &info) != 0 || stat(backup.c_str(), &info) == 0) return false;
    if (rename(savePath.c_str(), backup.c_str()) != 0) return false;
    if (rename(temporary.c_str(), savePath.c_str()) != 0) {
        rename(backup.c_str(), savePath.c_str());
        return false;
    }
    remove(backup.c_str());
    return true;
}

void GameBoyApp::releaseGame() {
    gbcore_destroy(core);
    core = nullptr;
    free(save);
    save = nullptr;
    free(rom);
    rom = nullptr;
}

void GameBoyApp::drawLine(
    void* context, const uint8_t* pixels, uint8_t line, bool color, const uint16_t* palette
) {
    auto* app = static_cast<GameBoyApp*>(context);
    auto* framebuffer = app->canvas->getFramebuffer();
    const int width = app->canvas->width();
    const int left = (width - 240) / 2;
    const int top = (app->canvas->height() - 216) / 2;
    const int firstRow = (line * 3) / 2;
    const int lastRow = ((line + 1) * 3) / 2;

    for (int row = firstRow; row < lastRow; ++row) {
        uint16_t* output = framebuffer + (top + row) * width + left;
        for (int x = 0; x < 240; ++x) {
            const uint8_t pixel = pixels[(x * 2) / 3];
            output[x] = color ? palette[pixel & 0x3F] : kDmgPalette[pixel & 0x03];
        }
    }
}

void GameBoyApp::run() {
    if (!loadRom()) {
        alert("Game Boy", K_S_GB_ROM_LOAD_FAILED);
        releaseGame();
        return;
    }
    core = gbcore_create(rom, romSize, drawLine, this);
    if (!core || !loadSave()) {
        alert("Game Boy", K_S_GB_UNSUPPORTED_ROM);
        releaseGame();
        return;
    }

    time_t now = time(nullptr);
    struct tm clock;
    if (localtime_r(&now, &clock)) gbcore_set_clock(core, &clock);

    uint32_t exitStartedAt = 0;
    int64_t nextFrameAt = esp_timer_get_time();
    while (true) {
        const lilka::State state = lilka::controller.getState();
        const bool exitChord = state.select.pressed && state.start.pressed;
        if (exitChord) {
            if (!exitStartedAt) exitStartedAt = millis();
            if (millis() - exitStartedAt >= kExitHoldMs) break;
        } else {
            exitStartedAt = 0;
        }

        uint8_t buttons = 0;
        if (state.a.pressed) buttons |= 0x01;
        if (state.b.pressed) buttons |= 0x02;
        if (state.select.pressed && !exitChord) buttons |= 0x04;
        if (state.start.pressed && !exitChord) buttons |= 0x08;
        if (state.right.pressed) buttons |= 0x10;
        if (state.left.pressed) buttons |= 0x20;
        if (state.up.pressed) buttons |= 0x40;
        if (state.down.pressed) buttons |= 0x80;
        gbcore_set_buttons(core, buttons);

        gbcore_run_frame(core);
        queueDraw();
        nextFrameAt += kFrameUs;
        const int64_t waitUs = nextFrameAt - esp_timer_get_time();
        if (waitUs >= 1000) vTaskDelay(pdMS_TO_TICKS(waitUs / 1000));
        else if (waitUs < -kFrameUs) nextFrameAt = esp_timer_get_time();
    }

    if (!writeSave()) alert("Game Boy", K_S_GB_SAVE_FAILED);
    releaseGame();
}
