#include "gameboyapp.h"
#include "apps/launcher/recentroms.h"
#include "keira/keira_lang.h"
#include "keira/ksystem.h"
#include "services/screenshot/request.h"

#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <I2S.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/stat.h>

namespace {
constexpr size_t kMinimumRomSize = 0x150;
constexpr size_t kRomBankSize = 0x4000;
constexpr int64_t kFrameUs = 16743;

constexpr uint16_t kDmgPalette[4] = {0xFFFF, 0xBDF7, 0x738E, 0x0000};

uint16_t blend565(uint16_t a, uint16_t b) {
    return ((a & 0xF7DE) >> 1) + ((b & 0xF7DE) >> 1) + (a & b & 0x0821);
}
} // namespace

GameBoyApp::GameBoyApp(const String& path) :
    EmulatorMenuApp("Game Boy", path, "GB/GBC paused"),
    romPath(path),
    savePath(path + ".sav"),
    statePath(path + ".ss0") {
    hasFrameskipSetting = true;
    setktStackSize(8192);
    setCanvasBounds((lilka::display.width() - 240) / 2, (lilka::display.height() - 216) / 2, 240, 216);
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
    FILE* file = fopen(temporary.c_str(), "wb");
    if (!file) return false;
    bool written = fwrite(save, 1, saveSize, file) == saveSize && fflush(file) == 0;
    if (fclose(file) != 0) written = false;
    if (!written) return false;

    return promoteTemporaryFile(savePath);
}

bool GameBoyApp::saveState() {
    const String temporary = statePath + ".tmp";
    return gbcore_save_state(core, temporary.c_str()) && promoteTemporaryFile(statePath);
}

bool GameBoyApp::loadState() {
    if (!gbcore_load_state(core, statePath.c_str())) return false;
#if LILKA_VERSION == 2
    if (audioReady) esp_i2s::i2s_zero_dma_buffer(esp_i2s::I2S_NUM_0);
#endif
    return true;
}

bool GameBoyApp::promoteTemporaryFile(const String& path) {
    const String temporary = path + ".tmp";
    const String backup = path + ".bak";
    if (rename(temporary.c_str(), path.c_str()) == 0) return true;
    struct stat info;
    if (stat(path.c_str(), &info) != 0 || stat(backup.c_str(), &info) == 0) return false;
    if (rename(path.c_str(), backup.c_str()) != 0) return false;
    if (rename(temporary.c_str(), path.c_str()) != 0) {
        rename(backup.c_str(), path.c_str());
        return false;
    }
    remove(backup.c_str());
    return true;
}

bool GameBoyApp::initAudio() {
#if LILKA_VERSION == 2
    const size_t samples = gbcore_audio_sample_frames();
    audioFrame = static_cast<int16_t*>(malloc((samples + 1) * 2 * sizeof(int16_t)));
    if (!audioFrame) return false;

    lilka::audio.initPins();
    esp_i2s::i2s_config_t config = {
        .mode = (esp_i2s::i2s_mode_t)(esp_i2s::I2S_MODE_MASTER | esp_i2s::I2S_MODE_TX),
        .sample_rate = 32000,
        .bits_per_sample = esp_i2s::I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = esp_i2s::I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format =
            (esp_i2s::i2s_comm_format_t)(esp_i2s::I2S_COMM_FORMAT_I2S | esp_i2s::I2S_COMM_FORMAT_I2S_MSB),
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 6,
        .dma_buf_len = 256,
        .use_apll = false,
    };
    if (esp_i2s::i2s_driver_install(esp_i2s::I2S_NUM_0, &config, 0, nullptr) != ESP_OK) return false;
    audioReady = true;
    volumeLevel = lilka::audio.getVolume();
    esp_i2s::i2s_zero_dma_buffer(esp_i2s::I2S_NUM_0);
    return true;
#else
    return false;
#endif
}

void GameBoyApp::writeAudio() {
    if (!audioFrame) return;
    const size_t frames = gbcore_render_audio(core, audioFrame);
    if (!audioReady || !frames) return;

    const size_t bytes = frames * 2 * sizeof(int16_t);
    lilka::audio.adjustVolume(audioFrame, bytes, 16, volumeLevel);
    size_t written = 0;
    while (written < bytes) {
        size_t chunk = 0;
        const esp_err_t result = esp_i2s::i2s_write(
            esp_i2s::I2S_NUM_0,
            reinterpret_cast<uint8_t*>(audioFrame) + written,
            bytes - written,
            &chunk,
            pdMS_TO_TICKS(100)
        );
        if (result != ESP_OK || chunk == 0) {
            stopAudio();
            return;
        }
        written += chunk;
    }
}

void GameBoyApp::stopAudio() {
#if LILKA_VERSION == 2
    if (audioReady) {
        esp_i2s::i2s_zero_dma_buffer(esp_i2s::I2S_NUM_0);
        esp_i2s::i2s_driver_uninstall(esp_i2s::I2S_NUM_0);
        audioReady = false;
    }
#endif
    free(audioFrame);
    audioFrame = nullptr;
}

void GameBoyApp::releaseGame() {
    stopAudio();
    gbcore_destroy(core);
    core = nullptr;
    free(save);
    save = nullptr;
    free(rom);
    rom = nullptr;
}

void GameBoyApp::clearMissingLines() {
    if (drawnLineCount == 144) return;
    const int width = canvas->width();
    const int left = (width - 240) / 2;
    const int top = (canvas->height() - 216) / 2;
    if (drawnLineCount == 0) {
        canvas->fillRect(left, top, 240, 216, lilka::colors::Black);
        return;
    }

    uint16_t* framebuffer = canvas->getFramebuffer() + top * width + left;
    for (int pair = 0; pair < 72; ++pair) {
        const bool even = drawnLines[pair * 2];
        const bool odd = drawnLines[pair * 2 + 1];
        uint16_t* topRow = framebuffer + pair * 3 * width;
        if (!even) memset(topRow, 0, 240 * sizeof(uint16_t));
        if (!even || !odd) memset(topRow + width, 0, 240 * sizeof(uint16_t));
        if (!odd) memset(topRow + width * 2, 0, 240 * sizeof(uint16_t));
    }
}

void GameBoyApp::drawLine(void* context, const uint8_t* pixels, uint8_t line, bool color, const uint16_t* palette) {
    auto* app = static_cast<GameBoyApp*>(context);
    if (!app->drawnLines[line]) ++app->drawnLineCount;
    app->drawnLines[line] = true;
    auto* framebuffer = app->canvas->getFramebuffer();
    const int width = app->canvas->width();
    const int left = (width - 240) / 2;
    const int top = (app->canvas->height() - 216) / 2;
    const int row = top + (line / 2) * 3 + (line & 1 ? 2 : 0);
    uint16_t* output = framebuffer + row * width + left;
    for (int pair = 0; pair < 80; ++pair) {
        const uint8_t first = pixels[pair * 2];
        const uint8_t second = pixels[pair * 2 + 1];
        const uint16_t a = color ? palette[first & 0x3F] : kDmgPalette[first & 0x03];
        const uint16_t b = color ? palette[second & 0x3F] : kDmgPalette[second & 0x03];
        output[pair * 3] = a;
        output[pair * 3 + 1] = blend565(a, b);
        output[pair * 3 + 2] = b;
    }
    if (line & 1) {
        const uint16_t* upper = output - width * 2;
        uint16_t* middle = output - width;
        for (int x = 0; x < 240; ++x)
            middle[x] = blend565(upper[x], output[x]);
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
    rememberRecentRom(romPath);

    time_t now = time(nullptr);
    struct tm clock;
    if (localtime_r(&now, &clock)) gbcore_set_clock(core, &clock);

    if (!initAudio()) {
        alert("Game Boy", K_S_GB_AUDIO_UNAVAILABLE);
        canvas->fillScreen(lilka::colors::Black);
        backCanvas->fillScreen(lilka::colors::Black);
    }

    loadPreferences();
    clearGameCanvases();
    bool startWasPressed = false;
    bool startGameActive = false;
    uint8_t turboAFrame = 0, turboBFrame = 0;
    bool saveChordActive = false;
    bool loadChordActive = false;
    bool selectWasPressed = false;
    bool selectConsumed = false;
    int64_t nextFrameAt = esp_timer_get_time();
    uint8_t skippedInRow = 0;
    while (true) {
        const int64_t frameStart = esp_timer_get_time();
        const lilka::State state = lilka::controller.getState();
        if (state.start.pressed && !startWasPressed) {
            startGameActive = !state.select.pressed;
            if (!startGameActive && !selectConsumed && !state.c.pressed && !state.d.pressed) {
                gbcore_set_buttons(core, 0xFF);
#if LILKA_VERSION == 2
                if (audioReady) esp_i2s::i2s_zero_dma_buffer(esp_i2s::I2S_NUM_0);
#endif
                const auto action = showSystemMenu();
                waitForRelease();
                clearGameCanvases();
                startWasPressed = startGameActive = selectWasPressed = selectConsumed = false;
                saveChordActive = loadChordActive = false;
                turboAFrame = turboBFrame = 0;
                if (action == SystemAction::Exit) break;
                if (action == SystemAction::Reset) gbcore_reset(core);
                if (action == SystemAction::Save) {
                    if (!saveState()) showNotice("State save failed");
                }
                if (action == SystemAction::Load) {
                    if (!loadState()) showNotice("State load failed");
                }
                clearGameCanvases();
                screenshotOnNextFrame = action == SystemAction::Screenshot;
                nextFrameAt = esp_timer_get_time();
                skippedInRow = 0;
                continue; // no paused-time CPU/audio catch-up
            }
        }
        startWasPressed = state.start.pressed;
        if (!state.start.pressed) startGameActive = false;

        // As in NES, Select is a modifier for save/load and the system menu.
        // Suppress a game Select tap if it formed any chord, regardless of
        // which button was pressed first. Ambiguous C+D never fires a state.
        if (state.select.pressed &&
            ((state.c.pressed && state.d.pressed) || (state.start.pressed && (state.c.pressed || state.d.pressed))))
            selectConsumed = true;
        const bool saveChord =
            state.select.pressed && state.c.pressed && !state.d.pressed && !state.start.pressed && !selectConsumed;
        const bool loadChord =
            state.select.pressed && state.d.pressed && !state.c.pressed && !state.start.pressed && !selectConsumed;
        if (saveChord && !saveChordActive) {
            ksystem.apps.startToast(saveState() ? K_S_GB_STATE_SAVED : K_S_GB_STATE_SAVE_ERROR);
            selectConsumed = true;
            nextFrameAt = esp_timer_get_time();
            skippedInRow = 0;
        }
        if (loadChord && !loadChordActive) {
            ksystem.apps.startToast(loadState() ? K_S_GB_STATE_LOADED : K_S_GB_STATE_LOAD_ERROR);
            selectConsumed = true;
            nextFrameAt = esp_timer_get_time();
            skippedInRow = 0;
        }
        saveChordActive = saveChord;
        loadChordActive = loadChord;
        const bool selectTap = selectWasPressed && !state.select.pressed && !selectConsumed;
        if (!state.select.pressed) selectConsumed = false;
        selectWasPressed = state.select.pressed;

        // Keep CPU, input and audio at the Game Boy frame rate. If a rendered
        // frame missed its deadline, let Gnuboy skip LCD work on the next one.
        // Force a visible frame after at most two skips.
        const bool drawFrame =
            !automaticFrameskip || screenshotOnNextFrame || skippedInRow >= 2 || frameStart <= nextFrameAt + 1500;
        skippedInRow = drawFrame ? 0 : skippedInRow + 1;

        // The adapter accepts an active-low mask and maps it to Gnuboy's pad.
        const int horizontal = directionFilter.update(state.left.pressed, state.right.pressed, millis(), preferences);
        const int vertical = verticalFilter.update(state.up.pressed, state.down.pressed, millis(), preferences);
        const bool turboA =
            nesmenu::turboPulse(preferences.turboA && state.c.pressed && !state.select.pressed, turboAFrame);
        const bool turboB =
            nesmenu::turboPulse(preferences.turboB && state.d.pressed && !state.select.pressed, turboBFrame);
        uint8_t buttons = 0xFF;
        if (state.a.pressed || turboA) buttons &= ~0x01;
        if (state.b.pressed || turboB) buttons &= ~0x02;
        if (selectTap) buttons &= ~0x04;
        if (state.start.pressed && startGameActive) buttons &= ~0x08;
        if (preferences.precisionMode ? horizontal > 0 : state.right.pressed) buttons &= ~0x10;
        if (preferences.precisionMode ? horizontal < 0 : state.left.pressed) buttons &= ~0x20;
        if (preferences.precisionMode ? vertical < 0 : state.up.pressed) buttons &= ~0x40;
        if (preferences.precisionMode ? vertical > 0 : state.down.pressed) buttons &= ~0x80;
        gbcore_set_buttons(core, buttons);

        if (drawFrame) {
            canvas->fillScreen(lilka::colors::Black);
            memset(drawnLines, 0, sizeof(drawnLines));
            drawnLineCount = 0;
        }
        gbcore_run_frame(core, drawFrame);
        if (drawFrame) {
            clearMissingLines();
            if (screenshotOnNextFrame) {
                screenshotOnNextFrame = false;
                if (!screenshot::request(canvas)) ksystem.apps.startToast(K_S_SCREENSHOT_SAVE_ERROR);
            }
            queueDraw();
        }
        writeAudio();
        nextFrameAt += kFrameUs;
        const int64_t waitUs = nextFrameAt - esp_timer_get_time();
        if (waitUs >= 1000) vTaskDelay(pdMS_TO_TICKS(waitUs / 1000));
        else if (waitUs < -3 * kFrameUs) nextFrameAt = esp_timer_get_time();
    }

    stopAudio();
    gbcore_copy_save(core);
    if (!writeSave()) alert("Game Boy", K_S_GB_SAVE_FAILED);
    releaseGame();
}
