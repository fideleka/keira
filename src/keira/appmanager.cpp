// Libraries
#include <lilka/controller.h>
#include <cstring>

#include "keira/appmanager.h"
#include "keira/thread.h"
#include "keira/keira_lang.h"

// Apps:
#include "apps/statusbar/statusbar.h"
#include "apps/launcher/launcher.h"

#define MAX_FPS 60

AppManager::AppManager() {
    setName(APPMANAGER_NAME);
    setktStackSize(APPMANAGER_STACK);
    setktPriority(APPMANAGER_PRIO);
    setktCore(APPMANAGER_CORE);
}

#define GET_BACK(X) X.empty() ? NULL : X.back()
void AppManager::threadsRun() {
    KMTX_LOCK(ThreadManager::lock);

    // Launch new threads
    for (auto& thread : threadsToRun) {
        // Suspend previous thread(app)
        auto topThread = GET_BACK(threads);

        if (topThread) topThread->suspend();
        // Launch new one
        thread->start();
        threads.push_back(thread);
    }
    threadsToRun.clear();

    KMTX_UNLOCK(ThreadManager::lock);
}

/// Performs Apps Run/Stop/Suspend/Draw if necessary
void AppManager::run() {
    K_AMG_DBG lilka::serial.log("Starting apps update loop");
    while (1) {
        threadsRun();

        ThreadManager::threadsClean();

        /// LOCK THREADS LIST
        KMTX_LOCK(ThreadManager::lock);

        // Retrieve top app[Thread]
        App* topApp = APP_PCAST(GET_BACK(threads));

        // Ensure topApp exists
        if (!(topApp)) {
            // Absolutely possible situation and can happen

            /// UNLOCK THREADS LIST
            KMTX_UNLOCK(ThreadManager::lock);
            continue;
        }

        // Ensure topApp not sleeping
        if (topApp->getState() == KTS_SUSPENDED) {
            // Wake up Neo
            topApp->resume();
            KMTX_LOCK(panelMtx);
            panel->setRedraw(true);
            KMTX_UNLOCK(panelMtx);
        }

        const bool wokeDisplay = lilka::displaySettings.serviceIdle(strcmp(topApp->getName(), "Launcher") == 0);
        if (lilka::displaySettings.isSleeping()) {
            KMTX_UNLOCK(ThreadManager::lock);
            vTaskDelayUntil(&lastFrameTick, pdMS_TO_TICKS(1000 / MAX_FPS));
            continue;
        }

        // Only this presentation task touches SPI. Feedback uses an immutable
        // snapshot and never writes into app/back/screenshot canvases.
        const uint32_t overlayNow = millis();
        auto volumeOverlay = lilka::audio.getVolumeOverlay();
        static_assert(sizeof(K_S_VOLUME_MUTE) <= sizeof(volumeOverlay.muteLabel), "Mute label exceeds snapshot");
        memcpy(volumeOverlay.muteLabel, K_S_VOLUME_MUTE, sizeof(K_S_VOLUME_MUTE));
        const bool rotated = lilka::display.prepareSystemOverlay(volumeOverlay, overlayNow);
        KMTX_LOCK(topApp->canvasMutex);
        const bool repaintLayers = wokeDisplay || topApp->backgroundDirty || rotated || topApp != lastPresentedApp;
        if (repaintLayers) {
            lilka::display.clearOutsideOverlay(lilka::colors::Black);
            topApp->backgroundDirty = false;
        }
        KMTX_UNLOCK(topApp->canvasMutex);

        // Draw panel and top app
        for (App* app : {panel, topApp}) {
            if (app == panel) {
                KMTX_LOCK(panelMtx);
                // Check if topApp is fullscreen. If it is, don't draw the panel
                if (topApp->getFlags() & AppFlags::APP_FLAG_FULLSCREEN) {
                    KMTX_UNLOCK(panelMtx);
                    continue;
                }
                KMTX_UNLOCK(panelMtx);
            }

            /// LOCK APP CANVAS
            KMTX_LOCK(app->canvasMutex);

            // Draw toast message on app's canvas to prevent flickering
            if (millis() < toast.endTime) {
                renderToast(topApp->backCanvas);
            }

            // Redraw app
            if (app->getRedraw() || repaintLayers) {
                const int parity = (app->flags & AppFlags::APP_FLAG_INTERLACED) && !repaintLayers ? app->frame % 2 : -1;
                lilka::display.presentCanvasOutsideOverlay(app->backCanvas, parity);
                app->setRedraw(false);
            }
            /// UNLOCK APP CANVAS
            KMTX_UNLOCK(app->canvasMutex);
        }
        if (lilka::display.systemOverlayNeedsTransfer()) {
            // Match screenshot lock order: panel mutex, panel canvas, top canvas.
            // The SDK takes no locks. Expiry reads retained canvases even for paused
            // scenes; active feedback is opaque and needs no underlying repaint.
            KMTX_LOCK(panelMtx);
            KMTX_LOCK(panel->canvasMutex);
            KMTX_LOCK(topApp->canvasMutex);
            lilka::Canvas* layers[] = {
                (topApp->getFlags() & AppFlags::APP_FLAG_FULLSCREEN) ? nullptr : panel->backCanvas, topApp->backCanvas
            };
            lilka::display.finishSystemOverlay(layers, 2);
            KMTX_UNLOCK(topApp->canvasMutex);
            KMTX_UNLOCK(panel->canvasMutex);
            KMTX_UNLOCK(panelMtx);
        }
        lastPresentedApp = topApp;
        /// UNLOCK THREADS LIST

        KMTX_UNLOCK(ThreadManager::lock);
        vTaskDelayUntil(&lastFrameTick, pdMS_TO_TICKS(1000 / MAX_FPS));
        //K_AMG_DBG lilka::serial.log("Last frame tick = %d", lastFrameTick);
    }
}
/// Render panel and top app to the given canvas.
/// Useful for taking screenshots.
void AppManager::renderToCanvas(lilka::Canvas* canvas) {
    KMTX_LOCK(ThreadManager::lock);

    App* topApp = APP_PCAST(GET_BACK(threads));

    // Ensure topApp exists
    if (topApp == NULL) {
        KMTX_UNLOCK(ThreadManager::lock);
        return;
    }

    // Match the physical display: fullscreen apps own a black background and
    // hide the panel, even when their canvas covers only part of the screen.
    canvas->fillScreen(lilka::colors::Black);
    KMTX_LOCK(panelMtx);
    for (App* app : {panel, topApp}) {
        if (app == panel && (topApp->getFlags() & AppFlags::APP_FLAG_FULLSCREEN)) continue;
        KMTX_LOCK(app->canvasMutex);
        canvas->drawCanvas(app->backCanvas);
        KMTX_UNLOCK(app->canvasMutex);
    }
    KMTX_UNLOCK(panelMtx);

    KMTX_UNLOCK(ThreadManager::lock);
}
#undef GET_BACK

bool AppManager::isTopAppNamed(const char* name) {
    KMTX_LOCK(ThreadManager::lock);
    bool matches = !threads.empty() && strcmp(threads.back()->getName(), name) == 0;
    KMTX_UNLOCK(ThreadManager::lock);
    return matches;
}

void AppManager::spawn(App* app, bool autoSuspend) {
    // Reset controller state on launch
    app->setupOnEntryCallback(KT_CLBK_CAST(&lilka::Controller::resetState), KT_CLBK_DATA_CAST(&lilka::controller));
    // Reset controler state on resume
    app->setupOnResumeCallback(KT_CLBK_CAST(&lilka::Controller::resetState), KT_CLBK_DATA_CAST(&lilka::controller));
    // Clear canvas resources on suspend
    app->setupOnSuspendCallback(KT_CLBK_CAST(&App::deinitCanvas), KT_CLBK_DATA_CAST(app));
    // Restore canvas resources on resume
    app->setupOnResumeCallback(KT_CLBK_CAST(&App::initCanvas), KT_CLBK_DATA_CAST(app));

    // Do spawn
    ThreadManager::spawn(app, autoSuspend);
}

void AppManager::renderToast(lilka::Canvas* canvas) {
    int16_t x, y;
    uint16_t w, h;
    KMTX_LOCK(toast.mtx);

    lilka::display.setFont(FONT_8x13);
    lilka::display.getTextBounds(toast.message.c_str(), 0, 0, &x, &y, &w, &h);
    int16_t cx = lilka::display.width() / 2;
    int16_t cy = lilka::display.height() / 7 * 6;
    int16_t yOffset = 0;
    uint64_t time = millis();

    if (time < toast.startTime + 300) {
        // Phase 1: Fade in
        // Offset goes from 100 to 0
        yOffset = 50 - (time - toast.startTime) * 50 / 300;
    } else if (time < toast.endTime - 300) {
        // Phase 2: Fully visible
        yOffset = 0;
    } else {
        // Phase 3: Fade out
        // Offset goes from 0 to 100
        yOffset = (time - toast.endTime + 300) * 50 / 300;
    }

    lilka::Canvas toastCanvas(cx - w / 2 - 5 - canvas->x(), cy - h - 5 + yOffset - canvas->y(), w + 10, h + 10);

    toastCanvas.setFont(FONT_8x13);
    toastCanvas.fillScreen(lilka::colors::Dark_sienna);
    toastCanvas.setTextColor(lilka::colors::White);
    toastCanvas.setCursor(2, h + 2);
    toastCanvas.print(toast.message.c_str());

    KMTX_UNLOCK(toast.mtx);

    // canvas mtx already locked in run
    canvas->drawCanvas(&toastCanvas);
}

/// Display a toast message.
void AppManager::startToast(String message, uint64_t duration) {
    KMTX_LOCK(toast.mtx);

    // TODO: is millis equialent to xTaskGetTickCount() ?
    toast.message = message;
    toast.startTime = millis();
    toast.endTime = millis() + duration;

    KMTX_UNLOCK(toast.mtx);
}
