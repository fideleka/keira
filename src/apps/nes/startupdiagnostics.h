#pragma once
#include <stdint.h>

// Per-launch render checkpoints, not emulated-frame counts (frameskip is unchanged).
// Saturates after 60; no wraparound or recurring logs during long sessions.
class NesStartupDiagnostics {
public:
    bool nextRender() {
        if (renders >= 60) return false;
        ++renders;
        return renders == 10 || renders == 60;
    }
    unsigned renderCount() const {
        return renders;
    }

private:
    unsigned renders = 0;
};
