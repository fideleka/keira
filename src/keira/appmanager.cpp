// Libraries
#include <lilka/controller.h>
#include <cstring>
#include <esp_timer.h>
#include "keira/frame_metrics.h"

#include "keira/appmanager.h"
#include "keira/thread.h"
#include "keira/keira_lang.h"
#include "keira/ksystem.h"
#include "devices/devicemanager.h"

// Apps:
#include "apps/statusbar/statusbar.h"
#include "apps/launcher/launcher.h"

#define MAX_FPS 60

void AppManager::waitForFrame() {
#if LILKA_VERSION == 2
    const TickType_t period = framePeriod.next();
#else
    const TickType_t period = pdMS_TO_TICKS(1000 / MAX_FPS);
#endif
    const TickType_t now = xTaskGetTickCount();
    if (now - lastFrameTick >= period) {
        // Missed frames cannot be recovered. Always let apps/input/audio run
        // after an overrun instead of spinning through expired deadlines.
        vTaskDelay(1);
        lastFrameTick = xTaskGetTickCount();
#if LILKA_VERSION == 2
        framePeriod.reset();
#endif
    } else vTaskDelayUntil(&lastFrameTick, period);
}

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
#if KEIRA_MEASURE
    FrameMetrics work, intervals, internalTimes, submitted;
    uint32_t windowAt = millis(), previousAppFrame = 0, overruns = 0;
    int64_t previousStart = 0;
    App* monitored = nullptr;
#endif
    while (1) {
#if KEIRA_MEASURE
        const int64_t iterationStart = esp_timer_get_time();
        bool internalPresented = false;
        uint32_t internalUs = 0;
#endif
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

        auto* devices = ksystem.devices;
        const bool internalOutput = !devices || devices->internalOutput();
        const bool sharedOutput = devices && devices->externalOutput();
        const bool legacyInternal = internalOutput && !sharedOutput;
        if (devices && devices->invalidation() != externalGeneration) {
            externalGeneration = devices->invalidation();
            externalDirty = true;
        }
        const bool routeChanged = internalOutput != lastInternal || sharedOutput != lastShared;
        lastInternal = internalOutput;
        lastShared = sharedOutput;
        const bool wokeDisplay = lilka::displaySettings.serviceIdle(strcmp(topApp->getName(), "Launcher") == 0);
        lilka::display.serviceOutputPower();
        if (lilka::displaySettings.isSleeping() || (!internalOutput && !sharedOutput)) {
#if KEIRA_MEASURE
            monitored = nullptr;
#endif
            externalSleeping = true; // Power is already off; do not compose a blank frame.
            KMTX_UNLOCK(ThreadManager::lock);
            waitForFrame();
            continue;
        }

        if (externalSleeping) {
            externalSleeping = false;
            externalDirty = true;
        }

        // Only this presentation task touches internal SPI. Feedback uses an immutable
        // snapshot and never writes into app/back/screenshot canvases.
        const uint32_t overlayNow = millis();
        auto volumeOverlay = lilka::audio.getVolumeOverlay();
        static_assert(sizeof(K_S_VOLUME_MUTE) <= sizeof(volumeOverlay.muteLabel), "Mute label exceeds snapshot");
        memcpy(volumeOverlay.muteLabel, K_S_VOLUME_MUTE, sizeof(K_S_VOLUME_MUTE));
        const auto lightOverlay = lilka::brightness.getOverlay();
        if (lightOverlay.visible(overlayNow) &&
            (!volumeOverlay.visible(overlayNow) ||
             overlayNow - lightOverlay.adjustedAt < overlayNow - volumeOverlay.adjustedAt))
            volumeOverlay = lightOverlay;
        if (devices) {
            const auto selection = devices->getOverlay();
            if (selection.visible(overlayNow) &&
                (!volumeOverlay.visible(overlayNow) ||
                 overlayNow - selection.adjustedAt < overlayNow - volumeOverlay.adjustedAt))
                volumeOverlay = selection;
        }
        if (volumeOverlay.displayMode >= 0 && volumeOverlay.displayMode <= 2) {
            const char* names[] = {K_S_DISPLAY_INTERNAL, K_S_DISPLAY_EXTERNAL, K_S_DISPLAY_BOTH};
            static_assert(sizeof(K_S_DISPLAY_INTERNAL) <= sizeof(volumeOverlay.muteLabel), "Display label too long");
            static_assert(sizeof(K_S_DISPLAY_EXTERNAL) <= sizeof(volumeOverlay.muteLabel), "Display label too long");
            static_assert(sizeof(K_S_DISPLAY_BOTH) <= sizeof(volumeOverlay.muteLabel), "Display label too long");
            strlcpy(volumeOverlay.muteLabel, names[volumeOverlay.displayMode], sizeof(volumeOverlay.muteLabel));
        }
        const int outputWidth = lilka::display.width(), outputHeight = lilka::display.height();
        const int outputRotation = lilka::display.getRotation();
        const bool geometryChanged =
            lastOutputWidth != outputWidth || lastOutputHeight != outputHeight || lastOutputRotation != outputRotation;
        lastOutputWidth = outputWidth;
        lastOutputHeight = outputHeight;
        lastOutputRotation = outputRotation;
        const bool nativeRotated = legacyInternal && lilka::display.prepareSystemOverlay(volumeOverlay, overlayNow);
        const bool rotated = geometryChanged || nativeRotated;
        KMTX_LOCK(topApp->canvasMutex);
#if KEIRA_MEASURE
        if (monitored != topApp || routeChanged) {
            monitored = topApp;
            previousAppFrame = topApp->frame;
            windowAt = millis();
            work = intervals = internalTimes = submitted = FrameMetrics{};
            overruns = 0;
            previousStart = 0;
        }
        submitted.count += topApp->frame - previousAppFrame;
        previousAppFrame = topApp->frame;
        if (previousStart) intervals.record(iterationStart - previousStart);
        previousStart = iterationStart;
#endif
        const bool repaintLayers =
            wokeDisplay || routeChanged || topApp->backgroundDirty || rotated || topApp != lastPresentedApp;
        if (repaintLayers) {
            if (legacyInternal) {
#if KEIRA_MEASURE
                const int64_t start = esp_timer_get_time();
#endif
                lilka::display.clearOutsideOverlay(lilka::colors::Black);
#if KEIRA_MEASURE
                internalUs += esp_timer_get_time() - start;
                internalPresented = true;
#endif
            }
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
                externalDirty = true;
                if (legacyInternal) {
#if KEIRA_MEASURE
                    const int64_t start = esp_timer_get_time();
#endif
                    lilka::display.presentCanvasOutsideOverlay(app->backCanvas, parity);
#if KEIRA_MEASURE
                    internalUs += esp_timer_get_time() - start;
                    internalPresented = true;
#endif
                }
                app->setRedraw(false);
            }
            /// UNLOCK APP CANVAS
            KMTX_UNLOCK(app->canvasMutex);
        }
        auto externalOverlay = volumeOverlay;
        if (devices && devices->externalOutput()) {
            const auto brightnessOverlay = lilka::brightness.getOverlay();
            if (brightnessOverlay.visible(overlayNow) &&
                (!externalOverlay.visible(overlayNow) ||
                 overlayNow - brightnessOverlay.adjustedAt < overlayNow - externalOverlay.adjustedAt))
                externalOverlay = brightnessOverlay;
            const bool overlayVisible = externalOverlay.visible(overlayNow);
            if (overlayVisible != externalOverlayVisible || externalOverlay.adjustedAt != externalOverlayAt ||
                externalOverlay.level != externalOverlayLevel ||
                externalOverlay.brightness != externalOverlayBrightness ||
                externalOverlay.displayMode != externalOverlayMode)
                externalDirty = true;
            externalOverlayVisible = overlayVisible;
            externalOverlayAt = externalOverlay.adjustedAt;
            externalOverlayLevel = externalOverlay.level;
            externalOverlayBrightness = externalOverlay.brightness;
            externalOverlayMode = externalOverlay.displayMode;
        }
        if (legacyInternal && lilka::display.systemOverlayNeedsTransfer()) {
            // Match screenshot lock order: panel mutex, panel canvas, top canvas.
            // The SDK takes no locks. Expiry reads retained canvases even for paused
            // scenes; active feedback is opaque and needs no underlying repaint.
            KMTX_LOCK(panelMtx);
            KMTX_LOCK(panel->canvasMutex);
            KMTX_LOCK(topApp->canvasMutex);
            lilka::Canvas* layers[] = {
                (topApp->getFlags() & AppFlags::APP_FLAG_FULLSCREEN) ? nullptr : panel->backCanvas, topApp->backCanvas
            };
#if KEIRA_MEASURE
            const int64_t start = esp_timer_get_time();
#endif
            lilka::display.finishSystemOverlay(layers, 2);
#if KEIRA_MEASURE
            internalUs += esp_timer_get_time() - start;
            internalPresented = true;
#endif
            KMTX_UNLOCK(topApp->canvasMutex);
            KMTX_UNLOCK(panel->canvasMutex);
            KMTX_UNLOCK(panelMtx);
        }
        if (sharedOutput && externalDirty) {
            // Compose once for BOTH readers. The frame lock never covers external SPI.
            KMTX_LOCK(panelMtx);
            KMTX_LOCK(panel->canvasMutex);
            KMTX_LOCK(topApp->canvasMutex);
#if KEIRA_MEASURE
            const int64_t captureStart = esp_timer_get_time();
#endif
            if (devices->capture(
                    (topApp->getFlags() & AppFlags::APP_FLAG_FULLSCREEN) ? nullptr : panel->backCanvas,
                    topApp->backCanvas,
                    externalOverlay,
                    false
                ))
                externalDirty = false;
#if KEIRA_MEASURE
            if (topApp->measurementsActive)
                topApp->measures.record(measurement::Capture, esp_timer_get_time() - captureStart);
#endif
            KMTX_UNLOCK(topApp->canvasMutex);
            KMTX_UNLOCK(panel->canvasMutex);
            KMTX_UNLOCK(panelMtx);
        }
        // App canvases are no longer locked during either LCD transfer in shared mode.
        if (sharedOutput && internalOutput) {
#if KEIRA_MEASURE
            const int64_t start = esp_timer_get_time();
            internalPresented = devices->presentInternal();
            if (internalPresented) internalUs = esp_timer_get_time() - start;
#else
            devices->presentInternal();
#endif
        }
#if KEIRA_MEASURE
        if (internalPresented) internalTimes.record(internalUs);
        if (internalPresented && topApp->measurementsActive) topApp->measures.record(measurement::Internal, internalUs);
        const uint32_t workUs = esp_timer_get_time() - iterationStart;
        work.record(workUs);
        if (uint64_t(workUs) * MAX_FPS >= 1000000) ++overruns;
        const uint32_t elapsed = millis() - windowAt;
        const bool report = elapsed >= 5000u;
        char appName[48] = {};
        if (report) strncpy(appName, topApp->getName(), sizeof(appName) - 1);
#endif
        lastPresentedApp = topApp;
        /// UNLOCK THREADS LIST

        KMTX_UNLOCK(ThreadManager::lock);
#if KEIRA_MEASURE
        if (report) {
            const uint32_t appFps = submitted.fpsX10(elapsed), lcdFps = internalTimes.fpsX10(elapsed);
            const uint32_t compositorFps = work.fpsX10(elapsed);
            lilka::serial.log(
                "perf: app=%s mode=%s window-ms=%u submit-fps=%u.%u internal-fps=%u.%u "
                "compositor-fps=%u.%u",
                appName,
                sharedOutput ? (internalOutput ? "Both" : "External") : "Internal",
                elapsed,
                appFps / 10,
                appFps % 10,
                lcdFps / 10,
                lcdFps % 10,
                compositorFps / 10,
                compositorFps % 10
            );
            lilka::serial.log(
                "perf-time: window-ms=%u interval-avg/max-us=%u/%u work-avg/max-us=%u/%u "
                "internal-avg/max-us=%u/%u overruns=%u",
                elapsed,
                intervals.averageUs(),
                intervals.maxUs,
                work.averageUs(),
                work.maxUs,
                internalTimes.averageUs(),
                internalTimes.maxUs,
                overruns
            );
            windowAt = millis();
            work = intervals = internalTimes = submitted = FrameMetrics{};
            overruns = 0;
        }
#endif
        waitForFrame();
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
