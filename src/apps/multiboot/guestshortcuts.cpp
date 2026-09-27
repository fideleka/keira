#include "guestshortcuts.h"

#include <Preferences.h>
#include <cstdio>

#include <lilka.h>

namespace {
constexpr size_t maxShortcuts = 4;
constexpr const char* listKey = "guest_fw";

String canonicalPath(const String& value) {
    const String root = lilka::fileutils.getSDRoot();
    if (value.startsWith(root + "/")) return value;
    if (value.startsWith("/")) return root + value;
    return "";
}

void addIfAvailable(std::vector<String>& entries, const String& value) {
    if (entries.size() >= maxShortcuts) return;
    const String path = canonicalPath(value);
    if (path.isEmpty() || path.length() > 255 || path.indexOf('\n') >= 0) return;
    String lower = path;
    lower.toLowerCase();
    if (!lower.endsWith(".bin")) return;
    for (const String& existing : entries)
        if (existing == path) return;
    FILE* file = fopen(path.c_str(), "rb");
    if (!file) return;
    fclose(file);
    entries.push_back(path);
}
} // namespace

std::vector<String> readGuestShortcuts() {
    std::vector<String> entries;
    Preferences prefs;
    String saved;
    if (prefs.begin("keira", true)) {
        saved = prefs.getString(listKey, "");
        prefs.end();
    }
    int from = 0;
    while (from < saved.length() && entries.size() < maxShortcuts) {
        int end = saved.indexOf('\n', from);
        if (end < 0) end = saved.length();
        addIfAvailable(entries, saved.substring(from, end));
        from = end + 1;
    }

    // Migrate the existing one-slot shortcut without consuming the SDK key.
    if (prefs.begin("lilka", true)) {
        addIfAvailable(entries, prefs.getString("multiboot_path", ""));
        prefs.end();
    }
    return entries;
}

void rememberGuestShortcut(const String& path, const std::vector<String>& previous) {
    std::vector<String> entries;
    addIfAvailable(entries, path);
    for (const String& old : previous)
        addIfAvailable(entries, old);
    String saved;
    for (const String& entry : entries) {
        if (!saved.isEmpty()) saved += '\n';
        saved += entry;
    }
    Preferences prefs;
    if (prefs.begin("keira", false)) {
        prefs.putString(listKey, saved);
        prefs.end();
    }
}
