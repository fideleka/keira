#include "gameboyapp.h"
#include "keira/keira_lang.h"
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
constexpr uint32_t kExitHoldMs = 1500;
constexpr uint16_t kDmgPalette[4] = {0xFFFF, 0xBDF7, 0x738E, 0x0000};

uint16_t blend565(uint16_t a, uint16_t b) {
    return ((a & 0xF7DE) >> 1) + ((b & 0xF7DE) >> 1) + (a & b & 0x0821);
}
} // namespace

GameBoyApp::GameBoyApp(const String& path) : App("Game Boy"), romPath(path), savePath(path + ".sav") {
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

bool GameBoyApp::initAudio() {
#if LILKA_VERSION == 2
    const size_t samples = gbcore_audio_sample_frames();
    audioFrame = static_cast<int16_t*>(malloc((samples + 1) * 2 * sizeof(int16_t)));
    if (!audioFrame) return false;

    lilka::audio.initPins();
    esp_i2s::i2s_config_t config = {
        .mode = (esp_i2s::i2s_mode_t)(esp_i2s::I2S_MODE_MASTER | esp_i2s::I2S_MODE_TX),
        .sample_rate = 32768,
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

void GameBoyApp::writeAudio(uint8_t& fractionalSamples) {
    if (!audioFrame) return;
    const size_t baseFrames = gbcore_audio_sample_frames();
    gbcore_render_audio(core, audioFrame);
    if (!audioReady) return;

    // 70224 Game Boy clocks per frame / 128 clocks per 32768-Hz sample is
    // 548.625 samples. MiniGB renders 548; duplicate the final stereo pair on
    // five of every eight frames to keep I2S playback paced with video.
    size_t frames = baseFrames;
    fractionalSamples += 5;
    if (fractionalSamples >= 8) {
        fractionalSamples -= 8;
        audioFrame[frames * 2] = audioFrame[(frames - 1) * 2];
        audioFrame[frames * 2 + 1] = audioFrame[(frames - 1) * 2 + 1];
        ++frames;
    }

    const size_t bytes = frames * 2 * sizeof(int16_t);
    lilka::audio.adjustVolume(audioFrame, bytes, 16, volumeLevel);
    size_t written = 0;
    while (written < bytes) {
        size_t chunk = 0;
        const esp_err_t result = esp_i2s::i2s_write(
            esp_i2s::I2S_NUM_0, reinterpret_cast<uint8_t*>(audioFrame) + written, bytes - written, &chunk,
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

void GameBoyApp::drawLine(
    void* context, const uint8_t* pixels, uint8_t line, bool color, const uint16_t* palette
) {
    auto* app = static_cast<GameBoyApp*>(context);
    const int64_t videoStart = app->profileVideo ? esp_timer_get_time() : 0;
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
        for (int x = 0; x < 240; ++x) middle[x] = blend565(upper[x], output[x]);
    }
    if (app->profileVideo) app->sampledVideoUs += esp_timer_get_time() - videoStart;
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

    if (!initAudio()) {
        alert("Game Boy", K_S_GB_AUDIO_UNAVAILABLE);
        canvas->fillScreen(lilka::colors::Black);
        backCanvas->fillScreen(lilka::colors::Black);
    }

    uint32_t exitStartedAt = 0;
    bool screenChordActive = false;
    uint8_t fractionalSamples = 0;
    int64_t nextFrameAt = esp_timer_get_time();
    uint64_t coreTimeUs = 0;
    uint64_t audioTimeUs = 0;
    uint64_t frameTimeUs = 0;
    uint32_t timedFrames = 0;
    int64_t timingWindowStart = nextFrameAt;
    while (true) {
        profileVideo = timedFrames == 0;
        if (profileVideo) sampledVideoUs = 0;
        const int64_t frameStart = esp_timer_get_time();
        const lilka::State state = lilka::controller.getState();
        const bool exitChord = state.select.pressed && state.start.pressed;
        if (exitChord) {
            if (!screenChordActive) {
                screenChordActive = true;
                exitStartedAt = millis();
            }
            if (millis() - exitStartedAt >= kExitHoldMs) break;
        } else if (screenChordActive) {
            screenChordActive = false;
            screenshot::request();
        }

        // Walnut's joypad is active-low: 1 means released, 0 means pressed.
        uint8_t buttons = 0xFF;
        if (state.a.pressed) buttons &= ~0x01;
        if (state.b.pressed) buttons &= ~0x02;
        if (state.select.pressed && !exitChord) buttons &= ~0x04;
        if (state.start.pressed && !exitChord) buttons &= ~0x08;
        if (state.right.pressed) buttons &= ~0x10;
        if (state.left.pressed) buttons &= ~0x20;
        if (state.up.pressed) buttons &= ~0x40;
        if (state.down.pressed) buttons &= ~0x80;
        gbcore_set_buttons(core, buttons);

        // A complete frame replaces every row. Only clear rows the core did
        // not draw, for example while the LCD is disabled during startup.
        memset(drawnLines, 0, sizeof(drawnLines));
        drawnLineCount = 0;
        gbcore_run_frame(core);
        clearMissingLines();
        const int64_t coreEnd = esp_timer_get_time();
        queueDraw();
        writeAudio(fractionalSamples);
        const int64_t audioEnd = esp_timer_get_time();
        coreTimeUs += coreEnd - frameStart;
        audioTimeUs += audioEnd - coreEnd;
        frameTimeUs += audioEnd - frameStart;
        if (++timedFrames == 120) {
            const int64_t elapsedUs = audioEnd - timingWindowStart;
            lilka::serial.log(
                "GB perf: core+video %lu us, scale(sample) %lu us, audio+queue %lu us, frame %lu us, "
                "%lu fps, internal=%d, cache=%u, reloads=%lu",
                static_cast<unsigned long>(coreTimeUs / timedFrames),
                static_cast<unsigned long>(sampledVideoUs),
                static_cast<unsigned long>(audioTimeUs / timedFrames),
                static_cast<unsigned long>(frameTimeUs / timedFrames),
                static_cast<unsigned long>(elapsedUs > 0 ? 120000000LL / elapsedUs : 0),
                gbcore_uses_internal_ram(core), gbcore_cached_rom_banks(core),
                static_cast<unsigned long>(gbcore_take_cache_reloads(core))
            );
            coreTimeUs = audioTimeUs = frameTimeUs = 0;
            timedFrames = 0;
            timingWindowStart = audioEnd;
        }
        nextFrameAt += kFrameUs;
        const int64_t waitUs = nextFrameAt - esp_timer_get_time();
        if (waitUs >= 1000) vTaskDelay(pdMS_TO_TICKS(waitUs / 1000));
        else if (waitUs < -kFrameUs) nextFrameAt = esp_timer_get_time();
    }

    stopAudio();
    if (!writeSave()) alert("Game Boy", K_S_GB_SAVE_FAILED);
    releaseGame();
}
