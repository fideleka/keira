#include "nesapp.h"
#include "driver.h"

extern "C" {
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
    // Load the ROM
    Driver::setNesApp(this);
    nofrendo_main(1, argv);

    // Nofrendo normally relies on process-exit cleanup. Keira keeps running, so
    // release every emulator subsystem before returning to the launcher.
    osd_shutdown();
    main_quit();
    gui_shutdown();
    vid_shutdown();
    nofrendo_log_shutdown();
    Driver::setNesApp(NULL);
}
