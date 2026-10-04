#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nesmenu {
constexpr size_t MAX_CONFIG_BYTES = 512;
constexpr size_t MAX_PATH_BYTES = 512;
constexpr uint32_t MIN_DELAY_MS = 50;
constexpr uint32_t MAX_DELAY_MS = 1000;
struct Preferences {
    bool precisionMode = false;
    uint32_t directionDelayXMs = 250;
    uint32_t directionDelayYMs = 250;
    bool turboA = true;
    bool turboB = true;
};
enum class ConfigResult { Ok, Missing, Malformed, Unsupported, IoError };
bool configPath(const char* rom, char* output, size_t capacity);
ConfigResult parseConfig(const char* data, size_t size, Preferences& preferences);
ConfigResult loadConfig(const char* path, Preferences& preferences);
struct FileOperations {
    int (*renameFile)(const char*, const char*);
    int (*removeFile)(const char*);
};
ConfigResult saveConfig(const char* path, const Preferences& preferences, const FileOperations* operations = nullptr);
class DirectionFilter {
public:
    // Caller supplies its axis delay; filter owns only that axis timing state.
    int update(bool negative, bool positive, uint32_t now, bool precisionMode, uint32_t delayMs) {
        int next = negative == positive ? 0 : negative ? -1 : 1;
        if (!precisionMode) {
            reset();
            return next;
        }
        if (next != direction) {
            direction = next;
            startedAt = now;
            return next; // immediate single emulated-frame press
        }
        return next && uint32_t(now - startedAt) >= delayMs ? next : 0;
    }
    void reset() {
        direction = 0;
        startedAt = 0;
    }

private:
    int direction = 0;
    uint32_t startedAt = 0;
};
inline bool turboPulse(bool pressed, uint8_t& frame) {
    if (!pressed) {
        frame = 0;
        return false;
    }
    bool pulse = ((frame / 2) % 2) == 0;
    ++frame;
    return pulse;
}
class ReleaseGate {
public:
    bool blocked(bool anyPressed) {
        if (waiting && !anyPressed) waiting = false;
        return waiting;
    }
    void arm() {
        waiting = true;
    }

private:
    bool waiting = true;
};
} // namespace nesmenu
