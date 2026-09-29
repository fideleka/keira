#include "recentroms.h"

#include "keira/ksystem.h"
#include "keira/mutex.h"

#include <Preferences.h>
#include <cstdio>

namespace {
constexpr size_t kMaxRoms = 20;
constexpr size_t kMaxPathLength = 255;
constexpr size_t kMaxSavedLength = 3999; // Preferences/NVS strings have a 4000-byte limit including NUL.

const char* keyFor(RomSystem system) {
    switch (system) {
        case RomSystem::NES:
            return "recent_nes";
        case RomSystem::GameBoy:
            return "recent_gb";
        case RomSystem::GameBoyColor:
            return "recent_gbc";
    }
    return "recent_nes";
}

bool validPath(const String& path, RomSystem system) {
    String lower = path;
    lower.toLowerCase();
    if (!path.startsWith("/sd/") || path.length() > kMaxPathLength || path.indexOf('\n') >= 0 ||
        !(lower.endsWith(".nes") || lower.endsWith(".rom") || lower.endsWith(".gb") || lower.endsWith(".gbc")) ||
        romSystemForPath(path) != system) {
        return false;
    }
    FILE* file = fopen(path.c_str(), "rb");
    if (!file) return false;
    fclose(file);
    return true;
}

std::vector<String> parseList(const String& saved, RomSystem system) {
    std::vector<String> entries;
    int from = 0;
    while (from < saved.length() && entries.size() < kMaxRoms) {
        int end = saved.indexOf('\n', from);
        if (end < 0) end = saved.length();
        String path = saved.substring(from, end);
        if (validPath(path, system)) {
            bool duplicate = false;
            for (const String& existing : entries) {
                if (existing == path) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) entries.push_back(path);
        }
        from = end + 1;
    }
    return entries;
}
} // namespace

RomSystem romSystemForPath(const String& path) {
    String lower = path;
    lower.toLowerCase();
    if (lower.endsWith(".gbc")) return RomSystem::GameBoyColor;
    if (lower.endsWith(".gb")) return RomSystem::GameBoy;
    return RomSystem::NES;
}

std::vector<String> readRecentRoms(RomSystem system) {
    String saved;
    NVS_LOCK;
    Preferences prefs;
    if (prefs.begin("keira", true)) {
        saved = prefs.getString(keyFor(system), "");
        prefs.end();
    }
    NVS_UNLOCK;
    return parseList(saved, system);
}

void rememberRecentRom(const String& path) {
    RomSystem system = romSystemForPath(path);
    if (!validPath(path, system)) return;

    NVS_LOCK;
    Preferences prefs;
    if (prefs.begin("keira", false)) {
        const char* key = keyFor(system);
        std::vector<String> previous = parseList(prefs.getString(key, ""), system);
        String saved = path;
        size_t count = 1;
        for (const String& entry : previous) {
            if (entry == path || saved.length() + entry.length() + 1 > kMaxSavedLength) continue;
            saved += '\n';
            saved += entry;
            if (++count >= kMaxRoms) break;
        }
        prefs.putString(key, saved);
        prefs.end();
    }
    NVS_UNLOCK;
}
