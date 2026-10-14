#include <esp_heap_caps.h>
#include <esp_timer.h>
#if KEIRA_MEASURE
#    include <soc/soc_memory_types.h>
#endif
#include <lilka.h>

#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

#include "keira/utils/acquire.h"
#include "services/screenshot/request.h"
#include "driver.h"
#include "keira/keira_lang.h"

#define OSD_OK          0
#define OSD_INIT_FAILED -1

extern "C" {
#include <event.h>
#include <gui.h>
#include <log.h>
#include <nes/nes.h>
#include <nes/nes_pal.h>
#include <nes/nesinput.h>
#include <nofconfig.h>
#include <osd.h>
#include <noftypes.h>
#include <nofrendo.h>
}

// No need to add `extern "C"` to functions below, because it's already declared in `osd.h`

static SemaphoreHandle_t xSoundMutex = NULL;
static SemaphoreHandle_t audioStoppedSemaphore = NULL;
static TaskHandle_t audioTaskHandle = NULL;
static TimerHandle_t timer = NULL;
static bool soundInitialized = false;

namespace {
constexpr size_t JOYPAD_EVENT_COUNT = 8;
constexpr uint8_t TURBO_HALF_PERIOD_FRAMES = 2;
constexpr TickType_t AUDIO_STOP_TIMEOUT = pdMS_TO_TICKS(100);

const int joypadEvents[JOYPAD_EVENT_COUNT] = {
    event_joypad1_up,
    event_joypad1_down,
    event_joypad1_left,
    event_joypad1_right,
    event_joypad1_select,
    event_joypad1_start,
    event_joypad1_a,
    event_joypad1_b,
};

struct NesInputState {
    bool forwarded[JOYPAD_EVENT_COUNT] = {};
    bool saveChordActive = false;
    bool loadChordActive = false;
    bool selectWasPressed = false;
    bool startWasPressed = false;
    bool selectConsumed = false;
    bool startGameActive = false;
    bool exitRequested = false;
    uint8_t turboAFrame = 0;
    uint8_t turboBFrame = 0;
};

NesInputState inputState;

void stopNesTimer(bool wait = false) {
    if (!timer) return;
#if LILKA_VERSION == 2
    auto* app = Driver::app;
    // Serialize disable with the callback's nonblocking reschedule. Otherwise
    // a ChangePeriod queued after Stop can resurrect the timer past the barrier.
    portENTER_CRITICAL(&app->timerMux);
    app->timerEnabled = false;
    portEXIT_CRITICAL(&app->timerMux);
#endif
    xTimerStop(timer, portMAX_DELAY);
#if LILKA_VERSION == 2
    if (wait) {
        // The timer callback now owns app state. Prove it has stopped before
        // shutdown can delete the app; static storage cannot fail allocation.
        StaticSemaphore_t storage;
        SemaphoreHandle_t stopped = xSemaphoreCreateBinaryStatic(&storage);
        xTimerPendFunctionCall(
            [](void* semaphore, uint32_t) { xSemaphoreGive(static_cast<SemaphoreHandle_t>(semaphore)); },
            stopped,
            0,
            portMAX_DELAY
        );
        xSemaphoreTake(stopped, portMAX_DELAY);
        vSemaphoreDelete(stopped);
    }
#else
    (void)wait;
#endif
}

void prepareRuntimeShutdown();
void openSystemMenu();

void resetInputState() {
    inputState = NesInputState();
}

void setJoypadEvent(size_t index, bool pressed) {
    if (inputState.forwarded[index] == pressed) {
        return;
    }

    event_t eventHandler = event_get(joypadEvents[index]);
    if (eventHandler) {
        eventHandler(pressed ? INP_STATE_MAKE : INP_STATE_BREAK);
    }
    inputState.forwarded[index] = pressed;
#if KEIRA_MEASURE
    if (Driver::app->measurementsActive) Driver::app->measures.changed();
#endif
}

void releaseJoypad() {
    for (size_t i = 0; i < JOYPAD_EVENT_COUNT; i++) {
        setJoypadEvent(i, false);
    }
}

void triggerStateEvent(int eventIndex) {
    event_t eventHandler = event_get(eventIndex);
    if (!eventHandler) {
        return;
    }

    Acquire lock(xSoundMutex);
    eventHandler(INP_STATE_MAKE);
}

bool turboPulse(bool pressed, uint8_t& frame) {
    if (!pressed) {
        frame = 0;
        return false;
    }

    bool pulse = ((frame / TURBO_HALF_PERIOD_FRAMES) % 2) == 0;
    frame++;
    return pulse;
}
} // namespace

int osd_init_sound();

void* mem_alloc(int size, bool prefer_fast_memory) {
    // return malloc(size);
    if (prefer_fast_memory) {
        return heap_caps_malloc(size, MALLOC_CAP_8BIT);
    } else {
        return heap_caps_malloc_prefer(size, MALLOC_CAP_SPIRAM, MALLOC_CAP_DEFAULT);
    }
}

void osd_getinput(void) {
#if KEIRA_MEASURE
    const int64_t inputStart = esp_timer_get_time();
    Driver::app->measures.poll(inputStart);
#endif
    lilka::State state = lilka::controller.getState();

    if (inputState.exitRequested) {
        return;
    }

    if (state.selectConsumed) inputState.selectConsumed = true;
    // Reserve Select-first Start for the system menu. Start-first remains an
    // ordinary game press. Volume cancellation may yield to Start; application
    // save/load consumption must not be reinterpreted as a menu chord.
    if (state.start.pressed && !inputState.startWasPressed) {
        inputState.startGameActive = !(state.select.pressed || state.selectHeld);
        if (!inputState.startGameActive && (!inputState.selectConsumed || state.selectConsumed) && !state.c.pressed &&
            !state.d.pressed) {
            inputState.selectConsumed = true;
            inputState.startWasPressed = true;
            openSystemMenu();
            return;
        }
    }

    // Both state buttons together are ambiguous. Suppress turbo and wait for
    // a fresh Select press rather than firing save/load as one is released.
    if (state.select.pressed &&
        ((state.c.pressed && state.d.pressed) || (state.start.pressed && (state.c.pressed || state.d.pressed)))) {
        inputState.selectConsumed = true;
    }

    bool saveChord = state.select.pressed && state.c.pressed && !state.d.pressed && !state.start.pressed &&
                     !inputState.selectConsumed;
    bool loadChord = state.select.pressed && state.d.pressed && !state.c.pressed && !state.start.pressed &&
                     !inputState.selectConsumed;

    if (saveChord && !inputState.saveChordActive) {
        triggerStateEvent(event_state_save);
        inputState.selectConsumed = true;
    }
    if (loadChord && !inputState.loadChordActive) {
        triggerStateEvent(event_state_load);
        inputState.selectConsumed = true;
    }
    inputState.saveChordActive = saveChord;
    inputState.loadChordActive = loadChord;

    // A solitary Select release becomes a one-frame NES tap. Never forward a
    // Select used by save/load or menu to the game.
    bool selectTap = inputState.selectWasPressed && !state.select.pressed && !inputState.selectConsumed;
    if (!state.select.pressed) {
        inputState.selectConsumed = false;
    }
    if (!state.start.pressed) {
        inputState.startGameActive = false;
    }
    inputState.selectWasPressed = state.select.pressed;
    inputState.startWasPressed = state.start.pressed;

    bool turboA =
        turboPulse(Driver::app->preferences.turboA && state.c.pressed && !state.select.pressed, inputState.turboAFrame);
    bool turboB =
        turboPulse(Driver::app->preferences.turboB && state.d.pressed && !state.select.pressed, inputState.turboBFrame);

    int direction = Driver::app->directionFilter.update(
        state.left.pressed,
        state.right.pressed,
        millis(),
        Driver::app->preferences.precisionMode,
        Driver::app->preferences.directionDelayXMs
    );
    int vertical = Driver::app->verticalFilter.update(
        state.up.pressed,
        state.down.pressed,
        millis(),
        Driver::app->preferences.precisionMode,
        Driver::app->preferences.directionDelayYMs
    );
    const bool desiredStates[JOYPAD_EVENT_COUNT] = {
        Driver::app->preferences.precisionMode ? vertical < 0 : state.up.pressed,
        Driver::app->preferences.precisionMode ? vertical > 0 : state.down.pressed,
        Driver::app->preferences.precisionMode ? direction < 0 : state.left.pressed,
        Driver::app->preferences.precisionMode ? direction > 0 : state.right.pressed,
        selectTap,
        state.start.pressed && inputState.startGameActive,
        state.a.pressed || turboA,
        state.b.pressed || turboB,
    };

    for (size_t i = 0; i < JOYPAD_EVENT_COUNT; i++) {
        setJoypadEvent(i, desiredStates[i]);
    }
#if KEIRA_MEASURE
    Driver::app->measures.record(measurement::InputWork, esp_timer_get_time() - inputStart);
#endif
}

int logprint(const char* string) {
    return printf("%s", string);
}

int osd_init() {
    resetInputState();
    xSoundMutex = xSemaphoreCreateMutex();
    audioStoppedSemaphore = xSemaphoreCreateBinary();
    nofrendo_log_chain_logfunc(logprint);
    osd_init_sound();
    return 0;
}

char configfilename[] = "na";
int osd_main(int argc, char* argv[]) {
    config.filename = configfilename;
    return main_loop(argv[0], system_autodetect);
}

int osd_installtimer(int frequency, void* func, int funcsize, void* counter, int countersize) {
    nofrendo_log_printf("Timer install, configTICK_RATE_HZ=%d, freq=%d\n", configTICK_RATE_HZ, frequency);
#if LILKA_VERSION == 2
    if (frequency <= 0 || frequency > configTICK_RATE_HZ) return -1;
    auto* app = Driver::app;
    app->timerPeriod = FractionalTicks(configTICK_RATE_HZ, frequency);
    app->timerCallback = reinterpret_cast<TimerCallbackFunction_t>(func);
    const TickType_t first = app->timerPeriod.next();
    app->timerDeadline = xTaskGetTickCount() + first;
    app->timerEnabled = true;
    timer = xTimerCreate("nes", first, pdTRUE, app, [](TimerHandle_t handle) {
        auto* app = static_cast<NesApp*>(pvTimerGetTimerID(handle));
        app->timerCallback(handle);
        app->timerDeadline += app->timerPeriod.next();
        const TickType_t now = xTaskGetTickCount();
        const TickType_t ahead = app->timerDeadline - now;
        // Keep an absolute fractional deadline across daemon jitter. A late
        // callback catches up at most once per tick, never a tight burst.
        const TickType_t delay = ahead && ahead <= configTICK_RATE_HZ ? ahead : 1;
        portENTER_CRITICAL(&app->timerMux);
        if (app->timerEnabled) xTimerChangePeriod(handle, delay, 0);
        portEXIT_CRITICAL(&app->timerMux);
    });
    if (!timer) return -1;
    if (xTimerStart(timer, 0) != pdPASS) {
        xTimerDelete(timer, portMAX_DELAY);
        timer = NULL;
        return -1;
    }
#else
    timer = xTimerCreate("nes", configTICK_RATE_HZ / frequency, pdTRUE, NULL, (TimerCallbackFunction_t)func);
    xTimerStart(timer, 0);
#endif
    return 0;
}

/* filename manipulation */
void osd_fullname(char* fullname, const char* shortname) {
    strncpy(fullname, shortname, PATH_MAX);
}

/* This gives filenames for storage of saves */
// cppcheck-suppress constParameterPointer
extern char* osd_newextension(char* string, char* ext) {
    // dirty: assume both extensions is 3 characters
    size_t l = strlen(string);
    string[l - 3] = ext[1];
    string[l - 2] = ext[2];
    string[l - 1] = ext[3];

    return string;
}

/* This gives filenames for storage of PCX snapshots */
int osd_makesnapname(char* filename, int len) {
    return -1;
}

void osd_getmouse(int* x, int* y, int* button) {
}

#define DEFAULT_FRAGSIZE    64
#define HW_AUDIO_SAMPLERATE 22050
#define HW_AUDIO_BPS        16
static void (*audio_callback)(void* buffer, int length) = NULL;
int16_t* audio_frame = NULL;
QueueHandle_t queue;
int osd_init_sound() {
#if LILKA_VERSION == 1
    lilka::serial.err("This part of code should never be called. Audio not supported for this version of lilka");
    return OSD_INIT_FAILED;
#elif LILKA_VERSION == 2
    audio_frame = static_cast<int16_t*>(malloc(DEFAULT_FRAGSIZE * 4));
    if (!audio_frame) {
        lilka::serial.err("Failed to allocate audio_frame\n");
        //
        return OSD_INIT_FAILED;
    }

    lilka::audio.initPins();

    esp_i2s::i2s_config_t cfg = {
        .mode = (esp_i2s::i2s_mode_t)(esp_i2s::I2S_MODE_MASTER | esp_i2s::I2S_MODE_TX),
        .sample_rate = HW_AUDIO_SAMPLERATE,
        .bits_per_sample = esp_i2s::I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = esp_i2s::I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format =
            (esp_i2s::i2s_comm_format_t)(esp_i2s::I2S_COMM_FORMAT_I2S | esp_i2s::I2S_COMM_FORMAT_I2S_MSB),
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 7,
        .dma_buf_len = 256,
        .use_apll = false,
    };
    if (i2s_driver_install(esp_i2s::I2S_NUM_0, &cfg, 2, &queue) != ESP_OK) {
        free(audio_frame);
        audio_frame = NULL;
        return OSD_INIT_FAILED;
    }
    soundInitialized = true;
    // esp_i2s::i2s_pin_config_t pins = {
    //     .bck_io_num = LILKA_I2S_BCLK,
    //     .ws_io_num = LILKA_I2S_LRCK,
    //     .data_out_num = LILKA_I2S_DOUT,
    //     .data_in_num = -1,
    // };
    // i2s_set_pin(esp_i2s::I2S_NUM_0, &pins);
    i2s_zero_dma_buffer(esp_i2s::I2S_NUM_0);
    audio_callback = 0;
    return OSD_OK;
#endif

    return OSD_INIT_FAILED;
}

void osd_stopsound() {
    if (!xSoundMutex) {
        return;
    }

    Acquire lock(xSoundMutex);
    if (soundInitialized) {
        if (i2s_driver_uninstall(esp_i2s::I2S_NUM_0) != ESP_OK) {
            lilka::serial.err("Failed to uninstall I2S driver\n");
        }
        soundInitialized = false;
    }
    free(audio_frame);
    audio_frame = NULL;
    audio_callback = 0;
}

bool do_audio_frame() {
    if (!audio_frame || !audio_callback) return false;
    // Reference: https://github.com/moononournation/arduino-nofrendo/blob/c5ba6d39c1fff0ceed314b76f6da5145d0d99bcc/examples/esp32-nofrendo/sound.c#L72
    const int64_t deadline = esp_timer_get_time() + 50000;
    int left = HW_AUDIO_SAMPLERATE / NES_REFRESH_RATE;
    auto volume = lilka::audio.getVolume();
    while (left) {
        int n = DEFAULT_FRAGSIZE;
        if (n > left) n = left;
        audio_callback(audio_frame, n); //get more data

        //16 bit mono -> 32-bit (16 bit r+l)
        const int16_t* mono_ptr = audio_frame + n;
        int16_t* stereo_ptr = audio_frame + n + n;
        int i = n;
        while (i--) {
            // int16_t a = (*(--mono_ptr) >> 2);
            // Too silent, so I changed it to:
            int16_t a = *(--mono_ptr);
            *(--stereo_ptr) = a;
            *(--stereo_ptr) = a;
        }

        // Gain and mono-to-stereo conversion happen once. Preserve the unwritten
        // tail on partial writes instead of asking the APU for replacement data.
        lilka::audio.adjustVolume(audio_frame, 4 * n, 16, volume);
        const size_t bytes = 4 * n;
        size_t offset = 0;
        while (offset < bytes) {
            const int64_t remainingUs = deadline - esp_timer_get_time();
            if (remainingUs <= 0) return false;
            const TickType_t timeout = pdMS_TO_TICKS((remainingUs + 999) / 1000);
            size_t written = 0;
            const esp_err_t result = i2s_write(
                esp_i2s::I2S_NUM_0,
                reinterpret_cast<const uint8_t*>(audio_frame) + offset,
                bytes - offset,
                &written,
                timeout ? timeout : 1
            );
            if (result != ESP_OK || !written || written > bytes - offset || written % 4) return false;
            offset += written;
        }
        left -= n;
    }
    return true;
}

void osd_setsound(void (*playfunc)(void* buffer, int length)) {
#if LILKA_VERSION == 1
#elif LILKA_VERSION == 2
    audio_callback = playfunc;
    if (audioStoppedSemaphore) {
        xSemaphoreTake(audioStoppedSemaphore, 0);
    }
    xTaskCreatePinnedToCore(
        [](void*) {
            while (true) {
                bool fed = false;
                // Active PCM is paced by the 22050 Hz I2S consumer, not an
                // emulation-frame timer. 367 samples * 60 Hz underfeeds it.
                {
                    Acquire lock(xSoundMutex);
                    if (!audio_frame) {
                        break;
                    }
                    if (!Driver::app->audioPaused) {
#    if KEIRA_MEASURE
                        const int64_t audioStart = esp_timer_get_time();
#    endif
                        fed = do_audio_frame();
#    if KEIRA_MEASURE
                        if (Driver::app->measurementsActive)
                            Driver::app->measures.record(measurement::Audio, esp_timer_get_time() - audioStart);
#    endif
                    }
                }
                // I2S blocks when its bounded DMA queue is full. Yield outside
                // the sound mutex; pause/failure must not become a busy loop.
                vTaskDelay(fed ? 1 : pdMS_TO_TICKS(10));
            }
            if (audioStoppedSemaphore) {
                xSemaphoreGive(audioStoppedSemaphore);
            }
            vTaskSuspend(NULL);
        },
        "nes_audio",
        8192,
        NULL,
        1,
        &audioTaskHandle,
        1
    );
#endif
    // Nofrendo calls this only on entry to nes_emulate(), after cartridge,
    // mapper, video and timer setup succeeded. Persist before gameplay, not
    // on exit. The app guard also covers later resets/restarts in this session.
    Driver::app->rememberSuccessfulLaunch();
}

void osd_getsoundinfo(sndinfo_t* info) {
    info->sample_rate = HW_AUDIO_SAMPLERATE;
    info->bps = HW_AUDIO_BPS;
}

void osd_getvideoinfo(vidinfo_t* info) {
    info->default_width = NES_SCREEN_WIDTH;
    info->default_height = NES_SCREEN_HEIGHT;
    info->driver = &Driver::driver;
}

void osd_shutdown() {
    prepareRuntimeShutdown();

    if (timer) {
        xTimerDelete(timer, portMAX_DELAY);
        timer = NULL;
    }
    if (audioStoppedSemaphore) {
        vSemaphoreDelete(audioStoppedSemaphore);
        audioStoppedSemaphore = NULL;
    }
    if (xSoundMutex) {
        vSemaphoreDelete(xSoundMutex);
        xSoundMutex = NULL;
    }
}

namespace {
// Stop commands are asynchronous. The timer-daemon barrier proves the callback
// is quiescent before the modal loop, so nofrendo_ticks cannot accrue catch-up.
void openSystemMenu() {
#if KEIRA_MEASURE
    Driver::app->measurementsActive = false;
#endif
    releaseJoypad();
    SemaphoreHandle_t stopped = xSemaphoreCreateBinary();
    if (!stopped) {
        lilka::serial.err(K_S_EMU_MENU_UNAVAILABLE);
#if KEIRA_MEASURE
        Driver::app->measurementsActive = true;
#endif
        return; // chord already consumed; keep emulator running safely
    }
    if (timer) {
        stopNesTimer();
        xTimerPendFunctionCall(
            [](void* semaphore, uint32_t) { xSemaphoreGive(static_cast<SemaphoreHandle_t>(semaphore)); },
            stopped,
            0,
            portMAX_DELAY
        );
        xSemaphoreTake(stopped, portMAX_DELAY);
    }
    vSemaphoreDelete(stopped);
    {
        Acquire lock(xSoundMutex);
        Driver::app->audioPaused = true;
        if (soundInitialized) i2s_zero_dma_buffer(esp_i2s::I2S_NUM_0);
    }
    Driver::app->directionFilter.reset();
    NesApp::SystemAction action =
        Driver::app->holdExitRequested() ? NesApp::SystemAction::Exit : Driver::app->showSystemMenu();
    Driver::app->waitForRelease();
    resetInputState();
    Driver::app->clearGameCanvases();
    if (action == NesApp::SystemAction::Exit) {
        inputState.exitRequested = true;
        prepareRuntimeShutdown();
        event_t quitHandler = event_get(event_quit);
        if (quitHandler) quitHandler(INP_STATE_MAKE);
        return;
    }
    if (action == NesApp::SystemAction::Reset) triggerStateEvent(event_soft_reset);
    if (action == NesApp::SystemAction::Save) triggerStateEvent(event_state_save);
    if (action == NesApp::SystemAction::Load) triggerStateEvent(event_state_load);
    {
        Acquire lock(xSoundMutex);
        Driver::app->audioPaused = false;
    }
#if KEIRA_MEASURE
    Driver::app->measurementsActive = true;
#endif
    if (timer) {
#if LILKA_VERSION == 2
        Driver::app->timerPeriod.reset();
        const TickType_t first = Driver::app->timerPeriod.next();
        Driver::app->timerDeadline = xTaskGetTickCount() + first;
        portENTER_CRITICAL(&Driver::app->timerMux);
        Driver::app->timerEnabled = true;
        portEXIT_CRITICAL(&Driver::app->timerMux);
        xTimerChangePeriod(timer, first, portMAX_DELAY);
#else
        xTimerReset(timer, portMAX_DELAY);
#endif
    }
    // Screenshot is deferred until the next game blit, not the menu framebuffer.
    if (action == NesApp::SystemAction::Screenshot) Driver::app->screenshotOnNextFrame = true;
}

void prepareRuntimeShutdown() {
    if (timer) {
        stopNesTimer(true);
    }

    osd_stopsound();

    TaskHandle_t taskToDelete = audioTaskHandle;
    if (taskToDelete) {
        if (audioStoppedSemaphore) {
            xSemaphoreTake(audioStoppedSemaphore, AUDIO_STOP_TIMEOUT);
        }
        vTaskDelete(taskToDelete);
        audioTaskHandle = NULL;
    }
}
} // namespace

#if KEIRA_MEASURE
extern "C" uint64_t osd_measure_clock(void) {
    return esp_timer_get_time();
}
extern "C" void osd_measure_frame(int draw, uint64_t us) {
    Driver::app->measures.record(draw ? measurement::CoreDraw : measurement::CoreSkip, us);
}
extern "C" void osd_measure_core_memory(void) {
    const auto region = [](const void* pointer) {
        return !pointer                        ? "none"
               : esp_ptr_external_ram(pointer) ? "psram"
               : esp_ptr_internal(pointer)     ? "internal"
                                               : "other";
    };
    const auto* machine = nes_getcontextptr();
    const auto* bitmap = vid_getbuffer();
    lilka::serial.log(
        "measure-nes-memory: framebuffer=%s cpu-ram=%s chr=%s",
        region(bitmap ? bitmap->data : nullptr),
        region(machine && machine->cpu ? machine->cpu->mem_page[0] : nullptr),
        region(machine && machine->ppu ? machine->ppu->page[0] : nullptr)
    );
}
extern "C" int osd_measure_core_sample(int draw) {
    auto& count = draw ? Driver::app->coreSampleDraw : Driver::app->coreSampleSkip;
    return (count++ & 15U) == 0;
}
extern "C" void osd_measure_core(int draw, uint64_t cpuUs, uint64_t ppuUs) {
    Driver::app->measures.record(draw ? measurement::NesCpuDraw : measurement::NesCpuSkip, cpuUs);
    Driver::app->measures.record(draw ? measurement::NesPpuDraw : measurement::NesPpuSkip, ppuUs);
}
extern "C" void osd_measure_report(void) {
    Driver::app->reportMeasurements();
}
#endif
