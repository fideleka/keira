#include "preferences.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace nesmenu {
bool configPath(const char* rom, char* output, size_t capacity) {
    const char* slash = strrchr(rom, '/');
    const char* dot = strrchr(rom, '.');
    size_t base = dot && (!slash || dot > slash + 1) && dot != rom ? size_t(dot - rom) : strlen(rom);
    if (base + sizeof(".conf") > capacity) return false;
    memcpy(output, rom, base);
    memcpy(output + base, ".conf", sizeof(".conf"));
    return true;
}
ConfigResult parseConfig(const char* data, size_t size, Preferences& preferences) {
    preferences = Preferences();
    if (size > MAX_CONFIG_BYTES || memchr(data, 0, size)) return ConfigResult::Malformed;
    char buffer[MAX_CONFIG_BYTES + 1];
    memcpy(buffer, data, size);
    buffer[size] = 0;
    Preferences parsed;
    unsigned seen = 0;
    uint32_t legacyDelay = 250;
    bool malformed = false, unsupported = false;
    char* save = nullptr;
    for (char* line = strtok_r(buffer, "\n", &save); line; line = strtok_r(nullptr, "\n", &save)) {
        size_t length = strlen(line);
        if (length && line[length - 1] == '\r') line[--length] = 0;
        if (!length || line[0] == '#') continue;
        char* equals = strchr(line, '=');
        if (!equals) {
            malformed = true;
            continue;
        }
        *equals++ = 0;
        unsigned key = !strcmp(line, "version")                ? 1
                       : !strcmp(line, "precision_mode")       ? 2
                       : !strcmp(line, "direction_delay_ms")   ? 4
                       : !strcmp(line, "turbo_a")              ? 8
                       : !strcmp(line, "turbo_b")              ? 16
                       : !strcmp(line, "direction_delay_x_ms") ? 32
                       : !strcmp(line, "direction_delay_y_ms") ? 64
                                                               : 0;
        if (!key || (seen & key)) {
            malformed = true;
            continue;
        }
        seen |= key;
        uint32_t value = 0;
        bool valid = *equals != 0;
        for (const char* digit = equals; *digit; ++digit) {
            if (*digit < '0' || *digit > '9' || value > 100000) {
                valid = false;
                break;
            }
            value = value * 10 + (*digit - '0');
        }
        if (key == 1 && (!valid || value != 1)) unsupported = true;
        bool delayKey = key == 4 || key == 32 || key == 64;
        if (!valid || (key != 1 && !delayKey && value > 1) ||
            (delayKey && (value < MIN_DELAY_MS || value > MAX_DELAY_MS))) {
            malformed = true;
            continue;
        }
        switch (key) {
            case 2:
                parsed.precisionMode = value;
                break;
            case 4:
                legacyDelay = value;
                break;
            case 32:
                parsed.directionDelayXMs = value;
                break;
            case 64:
                parsed.directionDelayYMs = value;
                break;
            case 8:
                parsed.turboA = value;
                break;
            case 16:
                parsed.turboB = value;
                break;
        }
    }
    if (unsupported) return ConfigResult::Unsupported;
    if (malformed || !(seen & 1)) return ConfigResult::Malformed;
    // Explicit axes override the legacy fallback regardless of line order.
    if (!(seen & 32)) parsed.directionDelayXMs = legacyDelay;
    if (!(seen & 64)) parsed.directionDelayYMs = legacyDelay;
    preferences = parsed;
    return ConfigResult::Ok;
}
ConfigResult loadConfig(const char* path, Preferences& preferences) {
    preferences = Preferences();
    FILE* file = fopen(path, "rb");
    if (!file) return errno == ENOENT ? ConfigResult::Missing : ConfigResult::IoError;
    char buffer[MAX_CONFIG_BYTES + 1];
    size_t size = fread(buffer, 1, sizeof(buffer), file);
    bool failed = ferror(file);
    if (fclose(file)) failed = true;
    if (failed) return ConfigResult::IoError;
    return parseConfig(buffer, size, preferences);
}
ConfigResult saveConfig(const char* path, const Preferences& preferences, const FileOperations* operations) {
    Preferences previous;
    ConfigResult existing = loadConfig(path, previous);
    // Protect even oversized/malformed files: an unknown version may lie beyond
    // our bounded reader. Session settings remain usable, but writing is refused.
    if (existing != ConfigResult::Ok && existing != ConfigResult::Missing) return existing;
    if (preferences.directionDelayXMs < MIN_DELAY_MS || preferences.directionDelayXMs > MAX_DELAY_MS ||
        preferences.directionDelayYMs < MIN_DELAY_MS || preferences.directionDelayYMs > MAX_DELAY_MS)
        return ConfigResult::Malformed;
    char temp[MAX_PATH_BYTES], backup[MAX_PATH_BYTES];
    if (strlen(path) + sizeof(".tmp") > sizeof(temp)) return ConfigResult::IoError;
    snprintf(temp, sizeof(temp), "%s.tmp", path);
    snprintf(backup, sizeof(backup), "%s.bak", path);
    FILE* leftover = fopen(backup, "rb");
    if (leftover) {
        fclose(leftover);
        return ConfigResult::IoError;
    }
    if (errno != ENOENT) return ConfigResult::IoError;
    FILE* file = fopen(temp, "wb");
    if (!file) return ConfigResult::IoError;
    bool failed = fprintf(
                      file,
                      "version=1\nprecision_mode=%d\ndirection_delay_x_ms=%lu\ndirection_delay_y_ms=%lu\n"
                      "turbo_a=%d\nturbo_b=%d\n",
                      preferences.precisionMode,
                      static_cast<unsigned long>(preferences.directionDelayXMs),
                      static_cast<unsigned long>(preferences.directionDelayYMs),
                      preferences.turboA,
                      preferences.turboB
                  ) < 0;
    if (fflush(file)) failed = true;
    if (!failed && fsync(fileno(file))) failed = true;
    if (fclose(file)) failed = true;
    auto renameFile = operations ? operations->renameFile : rename;
    auto removeFile = operations ? operations->removeFile : remove;
    if (failed) {
        removeFile(temp);
        return ConfigResult::IoError;
    }
    bool hadPrevious = existing == ConfigResult::Ok;
    if (hadPrevious && renameFile(path, backup)) {
        removeFile(temp);
        return ConfigResult::IoError;
    }
    if (renameFile(temp, path)) {
        // A failed rollback leaves the old file intact at .bak, never deletes it.
        if (hadPrevious) renameFile(backup, path);
        removeFile(temp);
        return ConfigResult::IoError;
    }
    if (hadPrevious && removeFile(backup)) return ConfigResult::IoError;
    return ConfigResult::Ok;
}
} // namespace nesmenu
