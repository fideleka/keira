#include "scummvm.h"

#include "apps/multiboot/multiboot.h"
#include "keira/ksystem.h"
#include "keira/utils/mem.h"

#include <sys/stat.h>
#include <stdio.h>

namespace {

constexpr size_t kMaxManifestBytes = 4096;
constexpr size_t kMaxManifestPath = 512;
constexpr const char* kScummImage = "/sd/scummvm/engines/scumm.bin";

bool isSafePath(const String& path, bool absolute) {
    if (path.isEmpty() || path.length() > kMaxManifestPath || path.indexOf('\\') >= 0) return false;
    if (absolute && !path.startsWith("/sd/")) return false;
    if (!absolute && path.startsWith("/")) return false;
    if (path.indexOf("//") >= 0) return false;

    int start = absolute ? 1 : 0;
    while (start < static_cast<int>(path.length())) {
        int end = path.indexOf('/', start);
        if (end < 0) end = path.length();
        if (path.substring(start, end) == "..") return false;
        start = end + 1;
    }
    return true;
}

bool isGameId(const String& id) {
    if (id.isEmpty() || id.length() > 48) return false;
    for (size_t i = 0; i < id.length(); ++i) {
        char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    }
    return true;
}

bool isSafeOption(const String& value) {
    if (value.length() > 16) return false;
    for (size_t i = 0; i < value.length(); ++i) {
        char c = value[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    }
    return true;
}

bool isButtonAction(const String& action) {
    return action == "leftClick" || action == "rightClick" || action == "enter" || action == "escape" ||
           action == "space" || action == "f5" || action == "f7" || action == "virtualKeyboard" || action == "none";
}

bool validControls(const JsonDocument& doc) {
    JsonVariantConst controls = doc["controls"];
    if (!controls.isNull()) {
        if (!controls.is<JsonObjectConst>()) return false;
        static const char* buttons[] = {"a", "b", "c", "d", "start", "select"};
        for (const char* button : buttons) {
            JsonVariantConst action = controls[button];
            if (!action.isNull() && (!action.is<const char*>() || !isButtonAction(action.as<String>()))) return false;
        }
    }
    JsonVariantConst pointer = doc["pointer"];
    if (pointer.isNull()) return true;
    if (!pointer.is<JsonObjectConst>()) return false;
    struct PointerSetting {
        const char* name;
        int minimum;
        int maximum;
    };
    static constexpr PointerSetting settings[] = {
        {"slowStep", 1, 4}, {"fastStep", 1, 12}, {"accelerationMs", 100, 2000}
    };
    for (const PointerSetting& setting : settings) {
        JsonVariantConst value = pointer[setting.name];
        if (!value.isNull() &&
            (!value.is<int>() || value.as<int>() < setting.minimum || value.as<int>() > setting.maximum))
            return false;
    }
    int slowStep = pointer["slowStep"].isNull() ? 1 : pointer["slowStep"].as<int>();
    int fastStep = pointer["fastStep"].isNull() ? 4 : pointer["fastStep"].as<int>();
    return fastStep >= slowStep;
}

String base64Url(const String& input) {
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    String result;
    result.reserve((input.length() * 4 + 2) / 3);
    for (size_t i = 0; i < input.length(); i += 3) {
        uint32_t n = static_cast<uint8_t>(input[i]) << 16;
        if (i + 1 < input.length()) n |= static_cast<uint8_t>(input[i + 1]) << 8;
        if (i + 2 < input.length()) n |= static_cast<uint8_t>(input[i + 2]);
        result += alphabet[(n >> 18) & 63];
        result += alphabet[(n >> 12) & 63];
        if (i + 1 < input.length()) result += alphabet[(n >> 6) & 63];
        if (i + 2 < input.length()) result += alphabet[n & 63];
    }
    return result;
}

} // namespace

ScummVMManagerApp::ScummVMManagerApp(const String& path) : App("ScummVM"), manifestPath(path) {
    setktStackSize(16384);
}

void ScummVMManagerApp::run() {
    if (!isSafePath(manifestPath, true)) {
        alert("ScummVM", "Invalid manifest path");
        return;
    }

    struct stat manifestStat;
    if (stat(manifestPath.c_str(), &manifestStat) != 0 || !S_ISREG(manifestStat.st_mode) || manifestStat.st_size <= 0 ||
        manifestStat.st_size > static_cast<off_t>(kMaxManifestBytes)) {
        alert("ScummVM", "Manifest is missing or too large");
        return;
    }

    FILE* file = fopen(manifestPath.c_str(), "rb");
    if (!file) {
        alert("ScummVM", "Cannot read manifest");
        return;
    }
    char json[kMaxManifestBytes + 1];
    size_t count = fread(json, 1, manifestStat.st_size, file);
    fclose(file);
    if (count != static_cast<size_t>(manifestStat.st_size)) {
        alert("ScummVM", "Cannot read complete manifest");
        return;
    }
    json[count] = '\0';

    JsonDocument doc(&spiRamAllocator);
    if (deserializeJson(doc, json) || !doc.is<JsonObject>()) {
        alert("ScummVM", "Invalid manifest JSON");
        return;
    }
    String schema = doc["schema"].as<String>();
    String title = doc["title"].as<String>();
    String engine = doc["engine"].as<String>();
    String gameId = doc["gameId"].as<String>();
    String relativePath = doc["path"].as<String>();
    String language = doc["language"].as<String>();
    String platform = doc["platform"].as<String>();
    if (schema != "keira-scummvm-v1" || title.isEmpty() || title.length() > 80 || engine != "scumm" ||
        !isGameId(gameId) || !isSafePath(relativePath, false) || !isSafeOption(language) || !isSafeOption(platform) ||
        !validControls(doc)) {
        alert("ScummVM", "Unsupported or invalid SCUMM manifest");
        return;
    }

    int slash = manifestPath.lastIndexOf('/');
    String gamePath = manifestPath.substring(0, slash);
    if (relativePath != ".") {
        gamePath += "/";
        gamePath += relativePath;
    }
    struct stat gameStat;
    if (stat(gamePath.c_str(), &gameStat) != 0 || !S_ISDIR(gameStat.st_mode)) {
        alert("ScummVM", "Game folder is missing");
        return;
    }

    struct stat imageStat;
    if (stat(kScummImage, &imageStat) != 0 || !S_ISREG(imageStat.st_mode) || imageStat.st_size <= 0 ||
        imageStat.st_size > 0x640000) {
        alert("ScummVM", "SCUMM engine image is missing or too large");
        return;
    }
    FILE* image = fopen(kScummImage, "rb");
    int magic = image ? fgetc(image) : EOF;
    if (image) fclose(image);
    if (magic != 0xE9) {
        alert("ScummVM", "SCUMM engine image is invalid");
        return;
    }

    String command = String(kScummImage) + " manifest=" + base64Url(manifestPath);
    if (command.length() >= 1024) {
        alert("ScummVM", "Manifest path is too long");
        return;
    }
    if (!confirm("ScummVM", "Launch " + title + "?")) return;
    ksystem.apps.spawn(new MultiBootApp(kScummImage, command));
}
