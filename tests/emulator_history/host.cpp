// Host-only mocks; run.py injects the real launch methods and history writer here.
#include <ctime>
struct PowerLoss {};
enum class Failure { None, Log, Config, Osd, Gui, Video, Core, Rom, Mapper, Timer };
Failure failure = Failure::None;
bool cutPower = true;
int cleanupCalls = 0;
struct AppFlags {
    enum { APP_FLAG_FULLSCREEN = 1, APP_FLAG_INTERLACED = 2 };
};
class App {
public:
    explicit App(const char*) {
    }
    virtual ~App() = default;
    virtual void run() = 0;
    void setktStackSize(int) {
    }
    void setFlags(int) {
    }
};
// Use the real declaration, including its per-instance guard initializer.
#define private public
#include "apps/nes/nesapp.h"
#undef private
class Driver {
public:
    static NesApp* app;
    static void setNesApp(NesApp* value) {
        app = value;
    }
};
NesApp* Driver::app = nullptr;
void osd_shutdown() {
    ++cleanupCalls;
}
void main_quit();
void gui_shutdown() {
    ++cleanupCalls;
}
void vid_shutdown() {
    ++cleanupCalls;
}
void nofrendo_log_shutdown() {
    ++cleanupCalls;
}
int nofrendo_main(int, char**);
// NES_METHODS

// RTOS task creation is a boundary mock, never execute the audio task.
using TickType_t = int;
using TaskHandle_t = void*;
void* audioStoppedSemaphore = nullptr;
void* xSoundMutex = nullptr;
TaskHandle_t audioTaskHandle = nullptr;
void (*audio_callback)(void*, int) = nullptr;
int16_t* audio_frame = nullptr;
#define LILKA_VERSION    2
#define NES_REFRESH_RATE 60
#define pdMS_TO_TICKS(x) (x)
struct Acquire {
    explicit Acquire(void*) {
    }
};
int xTaskGetTickCount() {
    return 0;
}
void xSemaphoreTake(void*, int) {
}
void xSemaphoreGive(void*) {
}
void vTaskDelayUntil(int*, int) {
}
void vTaskSuspend(void*) {
}
void do_audio_frame() {
}
void xTaskCreatePinnedToCore(void (*)(void*), const char*, int, void*, int, void**, int) {
}
// OSD_METHOD

// Include the dependency's REAL main_loop, internal_insert and nes_emulate;
// each load/init error below returns through those real startup branches.
enum system_t { system_unknown, system_nes, system_autodetect };
struct Apu {
    void (*process)(void*, int) = nullptr;
} apu;
struct Nes {
    explicit Nes(Apu* value) : apu(value) {
    }
    Apu* apu;
    int scanline_cycles = 0, fiq_cycles = 0;
    bool poweroff = false, pause = false, autoframeskip = false;
} nes{&apu};
struct Console {
    const char* filename = nullptr;
    const char* nextfilename = nullptr;
    system_t type = system_nes, nexttype = system_nes;
    struct {
        Nes* nes = nullptr;
    } machine;
    bool quit = false;
    int refresh_rate = 0;
} console;
#define NOFRENDO_STRDUP(s) (s)
#define NOFRENDO_FREE(s)   ((void)(s))
#define NES_SCREEN_WIDTH   256
#define NES_SCREEN_HEIGHT  240
#define NES_FIQ_PERIOD     29830
int nofrendo_ticks = 1;
struct vidinfo_t {
    int default_width = 256, default_height = 240;
    void* driver = nullptr;
};
struct Config {
    int open() {
        return failure == Failure::Config ? -1 : 0;
    }
} config;
void shutdown_everything() {
}
int osd_init() {
    return failure == Failure::Osd ? -1 : 0;
}
int gui_init() {
    return failure == Failure::Gui ? -1 : 0;
}
void osd_getvideoinfo(vidinfo_t*) {
}
int vid_init(int, int, void*) {
    return failure == Failure::Video ? -1 : 0;
}
system_t detect_systemtype(const char*) {
    return system_nes;
}
void event_set_system(system_t) {
}
void gui_setrefresh(int) {
}
void nofrendo_log_printf(const char*) {
}
Nes* nes_create() {
    return failure == Failure::Core ? nullptr : &nes;
}
int nes_insertcart(const char*, Nes*) {
    return failure == Failure::Rom || failure == Failure::Mapper ? -1 : 0;
}
void vid_setmode(int, int) {
}
int install_timer(int) {
    return failure == Failure::Timer ? -1 : 0;
}
void nes_emulate();
void gui_tick(int) {
}
void nes_renderframe(bool) {
}
void osd_getinput() {
}
void system_video(bool) {
    // Assert durable history DURING gameplay, without any return/cleanup.
    assert(historyWrites == 1);
    assert(readRecentRoms(RomSystem::NES).front() == String("/sd/game.nes"));
    for (int i = 0; i < 5; ++i) {
        // Simulate a later runtime re-entry/reset callback in the same app.
        osd_setsound(nullptr);
        assert(historyWrites == 1);
    }
    if (cutPower) throw PowerLoss();
    nes.poweroff = true;
    console.quit = true;
}
int main_loop(const char*, system_t);
int nofrendo_log_init() {
    return failure == Failure::Log ? -1 : 0;
}
void event_init() {
}
int osd_main(int, char** argv) {
    return main_loop(argv[0], system_autodetect);
}
// CORE_METHODS
void main_quit() {
    ++cleanupCalls;
    console.quit = true;
}

// GB/GBC share the same production launch method. Stub only core/resources;
// fatal load/init branches and the nonfatal audio fallback are executed intact.
const char* K_S_GB_ROM_LOAD_FAILED = "load";
const char* K_S_GB_UNSUPPORTED_ROM = "core";
const char* K_S_GB_AUDIO_UNAVAILABLE = "audio";
struct GbCanvas {
    void fillScreen(int) {
    }
} gbCanvas;
class GameBoyApp {
public:
    String romPath;
    void* rom = nullptr;
    size_t romSize = 0;
    void* core = nullptr;
    GbCanvas* canvas = &gbCanvas;
    GbCanvas* backCanvas = &gbCanvas;
    bool romOk = true, coreOk = true, saveOk = true, audioOk = true;
    int released = 0, alerts = 0;
    explicit GameBoyApp(const char* path) : romPath(path) {
    }
    bool loadRom() {
        return romOk;
    }
    bool loadSave() {
        return saveOk;
    }
    bool initAudio() {
        return audioOk;
    }
    void releaseGame() {
        ++released;
    }
    void alert(const char*, const char*) {
        ++alerts;
    }
    static void drawLine(void*, const uint8_t*, uint8_t, bool, const uint16_t*) {
    }
    void run();
};
GameBoyApp* currentGb = nullptr;
void* gbcore_create(void*, size_t, void (*)(void*, const uint8_t*, uint8_t, bool, const uint16_t*), void*) {
    return currentGb->coreOk ? currentGb : nullptr;
}
void gbcore_set_clock(void*, const struct tm*) {
}
// GB_STARTUP

void seedHistory() {
    namespacePresent = true;
    savedPreferences = {
        {"recent_nes", "/sd/old.nes\n/sd/game.nes"},
        {"recent_gb", "/sd/old.gb\n/sd/game.gb"},
        {"recent_gbc", "/sd/old.gbc\n/sd/game.gbc"},
        {"guest_fw", "/sd/scummvm.bin"}
    };
    availableFiles = {"/sd/old.nes", "/sd/game.nes", "/sd/old.gb", "/sd/game.gb", "/sd/old.gbc", "/sd/game.gbc"};
    historyWrites = cleanupCalls = 0;
    console = Console();
    nes.poweroff = false;
    nofrendo_ticks = 1;
}
int main() {
    for (Failure error :
         {Failure::Log,
          Failure::Config,
          Failure::Osd,
          Failure::Gui,
          Failure::Video,
          Failure::Core,
          Failure::Rom,
          Failure::Mapper,
          Failure::Timer}) {
        seedHistory();
        const auto before = savedPreferences;
        failure = error;
        NesApp app("/sd/game.nes");
        app.run();
        assert(savedPreferences == before && historyWrites == 0 && cleanupCalls == 5);
        assert(Driver::app == nullptr);
    }
    failure = Failure::None;
    for (bool resetWithoutExit : {true, false}) {
        seedHistory();
        cutPower = resetWithoutExit;
        {
            NesApp app("/sd/game.nes");
            try {
                app.run();
                assert(!resetWithoutExit);
            } catch (const PowerLoss&) {
                assert(resetWithoutExit && cleanupCalls == 0);
            }
            assert(historyWrites == 1);
        }
        // Simulated reboot: a fresh reader/App sees the persisted order.
        assert(readRecentRoms(RomSystem::NES).front() == String("/sd/game.nes"));
        assert(savedPreferences["recent_gb"] == String("/sd/old.gb\n/sd/game.gb"));
        assert(savedPreferences["guest_fw"] == String("/sd/scummvm.bin"));
        Driver::setNesApp(nullptr);
    }
    // A new launched app is a new session (not suppressed by stale/global state).
    seedHistory();
    cutPower = true;
    for (int session = 0; session < 2; ++session) {
        historyWrites = 0;
        console = Console();
        nes.poweroff = false;
        NesApp app("/sd/game.nes");
        try {
            app.run();
            assert(false);
        } catch (const PowerLoss&) {
        }
        assert(historyWrites == 1);
    }
    Driver::setNesApp(nullptr);
    for (const char* path : {"/sd/game.gb", "/sd/game.gbc"}) {
        for (int scenario = 0; scenario < 5; ++scenario) {
            seedHistory();
            const auto before = savedPreferences;
            GameBoyApp app(path);
            currentGb = &app;
            app.romOk = scenario != 0;
            app.coreOk = scenario != 1;
            app.saveOk = scenario != 2;
            app.audioOk = scenario != 3;
            try {
                app.run();
                assert(scenario < 3);
            } catch (const PowerLoss&) {
                assert(scenario >= 3 && app.released == 0);
            }
            if (scenario < 3) assert(historyWrites == 0 && savedPreferences == before && app.released == 1);
            else {
                assert(historyWrites == 1 && readRecentRoms(romSystemForPath(path)).front() == String(path));
                assert(savedPreferences["recent_nes"] == before.at("recent_nes"));
                assert(savedPreferences["guest_fw"] == before.at("guest_fw"));
            }
        }
    }
    assert(!nvsLocked);
    std::cout << "launch persists before exit; failures unchanged; one write/session/reset; GB/GBC startup passed\n";
}
