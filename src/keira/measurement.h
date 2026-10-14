#pragma once

#ifndef KEIRA_MEASURE
#    define KEIRA_MEASURE 0
#endif
#ifndef KEIRA_MEASURE_LOG
#    define KEIRA_MEASURE_LOG 1
#endif
#if KEIRA_MEASURE
#    include "frame_metrics.h"
#    include <freertos/FreeRTOS.h>
#    include <esp_timer.h>
#    include <atomic>

namespace measurement {
enum Phase { InputWork, CoreDraw, CoreSkip, Blit, PublishWait, PublishWork, Audio, Capture, Internal, PhaseCount };
struct Snapshot {
    FrameMetrics phase[PhaseCount];
    FrameMetrics pollGap;
    uint64_t firstPollUs = 0, lastPollUs = 0, firstSubmitUs = 0, lastSubmitUs = 0;
    uint32_t polls = 0, changes = 0, gapOver33 = 0, gapOver50 = 0, gapOver100 = 0;
    uint32_t submissions = 0;
};

// Multiple writers (emulator, audio, compositor), one emulator-task reporter.
// Critical sections contain only fixed-size arithmetic/copies, never clocks,
// formatting, allocation, logging or peripheral calls.
class Window {
    portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
    Snapshot stats;
    int64_t lastPoll = 0;

public:
    void record(Phase phase, uint32_t us) {
        portENTER_CRITICAL(&lock);
        stats.phase[phase].record(us);
        portEXIT_CRITICAL(&lock);
    }
    void poll(int64_t now) {
        portENTER_CRITICAL(&lock);
        ++stats.polls;
        if (!stats.firstPollUs) stats.firstPollUs = now;
        stats.lastPollUs = now;
        if (lastPoll) {
            const uint32_t gap = now - lastPoll;
            stats.pollGap.record(gap);
            stats.gapOver33 += gap > 33334;
            stats.gapOver50 += gap > 50000;
            stats.gapOver100 += gap > 100000;
        }
        lastPoll = now;
        portEXIT_CRITICAL(&lock);
    }
    void changed() {
        portENTER_CRITICAL(&lock);
        ++stats.changes;
        portEXIT_CRITICAL(&lock);
    }
    void submitted(uint64_t now) {
        portENTER_CRITICAL(&lock);
        ++stats.submissions;
        if (!stats.firstSubmitUs) stats.firstSubmitUs = now;
        stats.lastSubmitUs = now;
        portEXIT_CRITICAL(&lock);
    }
    void take(Snapshot& output) {
        portENTER_CRITICAL(&lock);
        output = stats;
        stats = Snapshot{};
        // Retain lastPoll so long gaps across reporting boundaries are visible.
        portEXIT_CRITICAL(&lock);
    }
};
} // namespace measurement
#endif
