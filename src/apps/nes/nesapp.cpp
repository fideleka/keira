// Startup diagnostics use standard warning logging, not disabled nofrendo logs.
#define LOG_LOCAL_LEVEL ESP_LOG_WARN
#include <esp_log.h>
#include <stdio.h>
#include <sys/stat.h>
#include "nesapp.h"
#include "apps/launcher/recentroms.h"
#include "driver.h"

extern "C" {
#include <cpu/nes6502.h>
#include <gui.h>
#include <log.h>
#include <nofrendo.h>
#include <osd.h>
#include <vid_drv.h>
}

NesApp::NesApp(String path) : App("NES") {
    setktStackSize(8192); // This task requires 4KB, but let's be careful here
    argv[0] = new char[path.length() + 1];
    strcpy(argv[0], path.c_str());
#ifdef NESAPP_INTERLACED
    setFlags(AppFlags::APP_FLAG_FULLSCREEN | AppFlags::APP_FLAG_INTERLACED);
#else
    setFlags(AppFlags::APP_FLAG_FULLSCREEN);
#endif
}

NesApp::~NesApp() {
    delete[] argv[0];
}

void NesApp::run() {
    // The SDK controller disables all ESP tags; opt in only to bounded NES diagnostics.
    esp_log_level_set("NES-startup", ESP_LOG_WARN);
    startupDiagnostics = NesStartupDiagnostics();
    ESP_LOGW("NES-startup", "launch core=%s", nes6502_irq_fix_identity_v1());
    // Header only: never print ROM contents, path or framebuffer data.
    unsigned char header[16] = {};
    struct stat info = {};
    FILE* file = fopen(argv[0], "rb");
    const size_t bytes = file ? fread(header, 1, sizeof(header), file) : 0;
    if (file) fclose(file);
    const int statResult = stat(argv[0], &info);
    if (bytes == sizeof(header) && memcmp(header, "NES\032", 4) == 0) {
        const bool nes2 = (header[7] & 0x0c) == 0x08;
        const unsigned mapper = (header[6] >> 4) | (header[7] & 0xf0) | (nes2 ? (header[8] & 0x0f) << 8 : 0);
        ESP_LOGW(
            "NES-startup",
            "ROM format=%s mapper=%u prg16k_field=%u chr8k_field=%u bytes=%lld stat=%d",
            nes2 ? "NES2" : "iNES",
            mapper,
            unsigned(header[4]),
            unsigned(header[5]),
            static_cast<long long>(info.st_size),
            statResult
        );
    } else {
        ESP_LOGW("NES-startup", "ROM header invalid/unreadable header_bytes=%u stat=%d", unsigned(bytes), statResult);
    }
    // Diagnostic reads do not replace the core's validation or change failure behavior.
    // Load the ROM
    Driver::setNesApp(this);
    ESP_LOGW("NES-startup", "nofrendo_main enter");
    const int result = nofrendo_main(1, argv);
    ESP_LOGW("NES-startup", "nofrendo_main return=%d", result);

    // Nofrendo normally relies on process-exit cleanup. Keira keeps running, so
    // release every emulator subsystem before returning to the launcher.
    osd_shutdown();
    main_quit();
    gui_shutdown();
    vid_shutdown();
    nofrendo_log_shutdown();
    Driver::setNesApp(NULL);
    if (result == 0) rememberRecentRom(argv[0]);
}

void NesApp::reportStartupRender() {
    if (!startupDiagnostics.nextRender()) return;
    // customBlit runs synchronously after nes6502_execute returned. Do not use
    // nes_getcontext: that also copies live APU state used by the audio task.
    nes6502_diagnostics cpu;
    nes6502_read_diagnostics(&cpu);
    ESP_LOGW(
        "NES-startup",
        "render=%u pc=%04lx cycles=%lu JAM=%u irq_sources=%02x pulse=%u",
        startupDiagnostics.renderCount(),
        static_cast<unsigned long>(cpu.pc),
        static_cast<unsigned long>(cpu.cycles),
        unsigned(cpu.jammed),
        unsigned(cpu.irq_sources),
        unsigned(cpu.int_pending)
    );
}

extern "C" void osd_nes_startup_status(const char* stage, int result) {
    ESP_LOGW("NES-startup", "stage=%s result=%d", stage, result);
}
