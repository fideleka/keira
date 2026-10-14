
#include "keira/app.h"
#if KEIRA_MEASURE
#    include "devices/devicemanager.h"
#endif

#include <lilka/colors565.h>
#include <lilka/display.h>
#include <lilka/controller.h>
#include "services/watchdog/watchdog.h"
#include <string.h>
#include <errno.h>
#include "keira/utils/string.h"

//=============================================================================
// App Constructors/Destructors
//=============================================================================
App::App(const char* name) {
    if (name) setName(name);
    else setName(APP_NAME);
    setktStackSize(APP_STACK);
    setktPriority(APP_PRIO);
    setktCore(APP_CORE);

    // Set inital drawing flags
    if (!(flags & APP_DRAWING_FLAGS)) {
        setFlags(this->flags | APP_FLAG_DEFAULT);
    }
}
//-----------------------------------------------------------------------------
App::~App() {
    KMTX_LOCK(canvasMutex);

    if (canvas) delete canvas;
    if (backCanvas) delete backCanvas;

    KMTX_UNLOCK(canvasMutex);
}
///////////////////////////////////////////////////////////////////////////////

//=============================================================================
//  App Flags
//=============================================================================
void App::setFlags(AppFlags_t flags) {
    if (this->flags == flags) return;

    this->flags = flags;

    // Keep canvases updated to flags
    initCanvas();
}
//-----------------------------------------------------------------------------
AppFlags_t App::getFlags() {
    return flags;
}
///////////////////////////////////////////////////////////////////////////////

//=============================================================================
// App UI Dialogs:
//=============================================================================
void App::alert(const String& title, const String& description) {
    lilka::Alert alertDialog(title, description);

    alertDialog.addActivationButton(K_BTN_BACK);

    while (!alertDialog.isFinished()) {
        alertDialog.update();
        canvas->fillScreen(lilka::colors::Black);
        alertDialog.draw(canvas);
        queueDraw();
    }
}
//-----------------------------------------------------------------------------
bool App::confirm(const String& title, const String& description) {
    lilka::Alert confirmDialog(title, description);

    confirmDialog.addActivationButton(K_BTN_BACK);
    confirmDialog.addActivationButton(K_BTN_CONFIRM);

    while (!confirmDialog.isFinished()) {
        confirmDialog.update();
        canvas->fillScreen(lilka::colors::Black);
        confirmDialog.draw(canvas);
        queueDraw();
    }

    return confirmDialog.getButton() == K_BTN_CONFIRM;
}
//-----------------------------------------------------------------------------
bool App::errnocheck() {
    if (!errno) return true;

    String errnoStr = StringFormat("errno: %d\n%s", errno, strerror(errno));

    lilka::Alert errnoDialog(K_S_ERROR, errnoStr.c_str());

    errnoDialog.addActivationButton(K_BTN_OK);
    errnoDialog.addActivationButton(K_BTN_BACK);

    while (!errnoDialog.isFinished()) {
        errnoDialog.update();
        canvas->fillScreen(lilka::colors::Black);
        errnoDialog.draw(canvas);
        queueDraw();
    }

    // Reset errno
    errno = 0;

    return false;
}
//-----------------------------------------------------------------------------
String App::input(const String& title, const String& value, bool masked) {
    lilka::InputDialog inputDialog(title);
    inputDialog.setMasked(masked);
    inputDialog.setValue(value);

    while (!inputDialog.isFinished()) {
        inputDialog.update();
        inputDialog.draw(canvas);
        queueDraw();
    }

    return inputDialog.getValue();
}
///////////////////////////////////////////////////////////////////////////////
//========================================================================
//  App UI Misc:
//========================================================================
// Set toolbar contents
void App::setToolBar(const String& toolbar) {
    this->toolBarLabel = toolbar;
}
void App::setToolBarColor(uint16_t color, uint16_t bgcolor) {
    this->toolBarColor = color;
    this->toolBarBgColor = bgcolor;
}
//////////////////////////////////////////////////////////////////////////
//=============================================================================
//  App Canvas management
//=============================================================================
bool App::getRedraw() {
    return redraw; // TODO: atomic?
}
//-----------------------------------------------------------------------------
void App::setRedraw(bool redraw) {
    this->redraw = redraw;
}
//-----------------------------------------------------------------------------
void App::setCanvasBounds(uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
    if (!width || !height || x + width > lilka::display.width() || y + height > lilka::display.height()) return;
    hasCanvasBounds = true;
    canvasX = x;
    canvasY = y;
    canvasWidth = width;
    canvasHeight = height;
    initCanvas();
}
//-----------------------------------------------------------------------------
void App::initCanvas() {
    KMTX_LOCK(canvasMutex);

    auto oldCanvas = canvas;
    auto oldBackCanvas = backCanvas;

    // Default canvas shifts
    uint16_t x = 0;
    uint16_t y = KEIRA_STATUSBAR_HEIGHT;

    // Default canvas sizes
    uint16_t w = lilka::display.width();
    uint16_t h = lilka::display.height() - KEIRA_STATUSBAR_HEIGHT;

    // Determine canvas dimensions based on flags
    if (flags & APP_FLAG_FULLSCREEN) {
        x = 0;
        y = 0;
        w = lilka::display.width();
        h = lilka::display.height();
    }

    if (flags & APP_FLAG_STATUSBAR) {
        x = 0;
        y = 0;
        w = lilka::display.width();
        h = KEIRA_STATUSBAR_HEIGHT;
    }

    if (hasCanvasBounds) {
        x = canvasX;
        y = canvasY;
        w = canvasWidth;
        h = canvasHeight;
    }

    // Ensure init/reinit needed
    if (canvas && backCanvas) {
        uint16_t cx = canvas->x();
        uint16_t cy = canvas->y();
        uint16_t bx = backCanvas->x();
        uint16_t by = backCanvas->y();

        uint16_t cw = canvas->width();
        uint16_t ch = canvas->height();
        uint16_t bw = backCanvas->width();
        uint16_t bh = backCanvas->height();

        // how to make it less ugly ? :D
        if ((cx == bx) && (cy == by) && (cw == bw) && (ch == bh) && (cy == y) && (cx == x) && (cw == w) && (ch == h)) {
            KMTX_UNLOCK(canvasMutex);
            return;
        }
    }

    // Create canvases
    canvas = new lilka::Canvas(x, y, w, h);
    backCanvas = new lilka::Canvas(x, y, w, h);

    // Fill them with black
    canvas->fillScreen(lilka::colors::Black);
    backCanvas->fillScreen(lilka::colors::Black);
    backgroundDirty = hasCanvasBounds;

    // Cleanup old canvases if them exist
    if (oldCanvas) delete oldCanvas;
    if (oldBackCanvas) delete oldBackCanvas;

    KMTX_UNLOCK(canvasMutex);
}
//-----------------------------------------------------------------------------
void App::deinitCanvas() {
    KMTX_LOCK(canvasMutex);

    if (canvas) {
        delete canvas;
        canvas = NULL;
    }
    if (backCanvas) {
        delete backCanvas;
        backCanvas = NULL;
    }

    KMTX_UNLOCK(canvasMutex);
}
//-----------------------------------------------------------------------------
void App::queueDraw() {
#if KEIRA_MEASURE
    const int64_t waitStart = esp_timer_get_time();
#endif
    KMTX_LOCK(canvasMutex);
#if KEIRA_MEASURE
    const int64_t acquired = esp_timer_get_time();
    if (measurementsActive) measures.record(measurement::PublishWait, acquired - waitStart);
#endif

    // Draw toolbar on top of canvas
    if (toolBarLabel != "") {
        canvas->fillRect(0, canvas->height() - TOOL_BAR_HEIGHT, canvas->width(), TOOL_BAR_HEIGHT, this->toolBarBgColor);

        canvas->setCursor(TOOL_BAR_SAFE_DISTANCE, canvas->height() - 20 / 2); // FONT_Y / 2
        canvas->setFont(FONT_8x13);
        canvas->setTextColor(this->toolBarColor);

        canvas->setTextBound(
            TOOL_BAR_SAFE_DISTANCE, canvas->height() - TOOL_BAR_HEIGHT, TOOL_BAR_WIDTH, TOOL_BAR_HEIGHT
        );

        canvas->printf("%s", this->toolBarLabel.c_str());
    }

    // Detect if frame was skipped. Need to readjust priorities in this case
    KAPP_DBG if (frame && redraw) {
        skippedFrames++;
        float percentSkipped = skippedFrames * 100 / frame;
        lilka::serial.log("Skipping frame %d. Skipped %f", frame, percentSkipped);
    }

    // Flip canvas
    auto buf = canvas;
    canvas = backCanvas;
    backCanvas = buf;

    // Increment frameCount
    frame++;

    setRedraw(true);
#if KEIRA_MEASURE
    if (measurementsActive) {
        measures.submitted(esp_timer_get_time());
        measures.record(measurement::PublishWork, esp_timer_get_time() - acquired);
    }
#endif
    KMTX_UNLOCK(canvasMutex);

    // Switch to a drawing task
    taskYIELD();
}

#if KEIRA_MEASURE
void App::startMeasurements() {
#    if defined(LILKA_MEASURE_INPUT) && LILKA_MEASURE_INPUT
    lilka::controller.takeInputMeasurements();
#    endif
    measurementAt = millis();
    measurementsActive = true;
}
void App::reportMeasurements(bool force) {
    if (!measurementsActive && !force) return;
    const uint32_t now = millis(), elapsed = now - measurementAt;
    if (!force && elapsed < 5000) return;
    if (!elapsed) return;
    measurement::Snapshot snapshot;
    measures.take(snapshot);
    measurementAt = now;
#    if defined(LILKA_MEASURE_INPUT) && LILKA_MEASURE_INPUT
    const auto scanner = lilka::controller.takeInputMeasurements();
#    endif
    const auto mode = ksystem.devices ? ksystem.devices->displayMode() : DeviceManager::Internal;
    const char* modeName = mode == DeviceManager::Both       ? "Both"
                           : mode == DeviceManager::External ? "External"
                                                             : "Internal";
    const int64_t reportStart = esp_timer_get_time();
    const uint32_t dropsBefore = lilka::serial.droppedLogs();
    lilka::serial.log(
        "measure: app=%s mode=%s window-ms=%u polls=%u submits=%u changes=%u gap-us=%u/%u gaps>33/50/100ms=%u/%u/%u",
        getName(),
        modeName,
        elapsed,
        snapshot.polls,
        snapshot.submissions,
        snapshot.changes,
        snapshot.pollGap.averageUs(),
        snapshot.pollGap.maxUs,
        snapshot.gapOver33,
        snapshot.gapOver50,
        snapshot.gapOver100
    );
#    if defined(LILKA_MEASURE_INPUT) && LILKA_MEASURE_INPUT
    lilka::serial.log(
        "measure-controller: scans=%u gap-us=%u/%u scan-work-us=%u/%u",
        scanner.scans,
        scanner.gaps ? static_cast<unsigned>(scanner.gapTotalUs / scanner.gaps) : 0,
        scanner.gapMaxUs,
        scanner.scans ? static_cast<unsigned>(scanner.workTotalUs / scanner.scans) : 0,
        scanner.workMaxUs
    );
#    endif
    lilka::serial.log(
        "measure-span: app=%s poll-span-us=%llu submit-span-us=%llu",
        getName(),
        static_cast<unsigned long long>(snapshot.lastPollUs - snapshot.firstPollUs),
        static_cast<unsigned long long>(snapshot.lastSubmitUs - snapshot.firstSubmitUs)
    );
    static const char* names[] = {
        "input", "core-draw", "core-skip", "blit", "publish-wait", "publish-work", "audio", "capture", "internal"
    };
    for (unsigned i = 0; i < measurement::PhaseCount; i += 2) {
        const auto& first = snapshot.phase[i];
        const FrameMetrics empty;
        const auto& second = i + 1 < measurement::PhaseCount ? snapshot.phase[i + 1] : empty;
        if (!first.count && !second.count) continue;
        // Compact pairs keep the whole burst well below the 16-record queue
        // limit. Values are count,total-us,average-us,max-us; no truncation.
        lilka::serial.log(
            "measure-phase: app=%s %s=%u,%llu,%u,%u %s=%u,%llu,%u,%u",
            getName(),
            names[i],
            first.count,
            static_cast<unsigned long long>(first.totalUs),
            first.averageUs(),
            first.maxUs,
            i + 1 < measurement::PhaseCount ? names[i + 1] : "none",
            second.count,
            static_cast<unsigned long long>(second.totalUs),
            second.averageUs(),
            second.maxUs
        );
    }
    lilka::serial.log(
        "measure-log: app=%s enqueue-us=%u dropped-total=%u dropped-this-report=%u emu-stack-free=%u",
        getName(),
        static_cast<unsigned>(esp_timer_get_time() - reportStart),
        lilka::serial.droppedLogs(),
        lilka::serial.droppedLogs() - dropsBefore,
        uxTaskGetStackHighWaterMark(nullptr)
    );
}
#endif
