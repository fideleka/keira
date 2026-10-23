#include "appmetadata.h"

#include <ArduinoJson.h>
#include <cstdio>
#include <cstring>

namespace {
bool singleFilename(const char* name) {
    return name && name[0] && strlen(name) <= 48 && strcmp(name, ".") && strcmp(name, "..") &&
           !strpbrk(name, "/\\:\r\n");
}

bool displayName(const char* name) {
    if (!name || !name[0] || strlen(name) > 48) return false;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(name); *p; ++p)
        if (*p < 32 || *p == 127) return false;
    return true;
}
} // namespace

void readGuestAppMetadata(const String& firmware, GuestAppMetadata& metadata) {
    metadata.hasIcon = false;
    const int slash = firmware.lastIndexOf('/');
    const String basename = firmware.substring(slash + 1);
    metadata.name = basename;
    if (metadata.name.endsWith(".bin")) metadata.name.remove(metadata.name.length() - 4);
    if (metadata.name.equalsIgnoreCase("doom")) metadata.name = "Doom";
    else if (metadata.name.equalsIgnoreCase("lasertank")) metadata.name = "LaserTank";
    if (slash < 0) return;
    const String folder = firmware.substring(0, slash + 1);
    FILE* file = fopen((folder + "app.json").c_str(), "rb");
    if (!file) return;
    char body[1025];
    const size_t length = fread(body, 1, sizeof(body), file);
    const bool failed = ferror(file);
    fclose(file);
    if (failed || !length || length > 1024) return;
    JsonDocument doc;
    if (deserializeJson(doc, body, length, DeserializationOption::NestingLimit(2)) || doc["schema"] != 1 ||
        !doc["firmware"].is<const char*>() || basename != doc["firmware"].as<const char*>())
        return;
    const char* name = doc["name"] | "";
    if (displayName(name)) metadata.name = name;
    const char* icon = doc["icon"] | "";
    if (!singleFilename(icon)) return;
    file = fopen((folder + icon).c_str(), "rb");
    if (!file) return;
    // Exactly one native 24x24 RGB565-LE image. No image decoder or size-driven allocation.
    const size_t bytes = fread(metadata.pixels, 1, sizeof(metadata.pixels), file);
    const int trailing = fgetc(file);
    const bool iconFailed = ferror(file);
    fclose(file);
    if (iconFailed || bytes != sizeof(metadata.pixels) || trailing != EOF) return;
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(metadata.pixels);
    for (unsigned i = 0; i < 24 * 24; ++i)
        metadata.pixels[i] = raw[2 * i] | (uint16_t(raw[2 * i + 1]) << 8);
    metadata.hasIcon = true;
}
