#include "multiboot.h"
#include "guestshortcuts.h"
#include "keira/keira.h"
#include "keira/utils/string.h"

#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {
bool matchesInstalledGuest(const String& path, const esp_partition_t*& partition) {
    partition = esp_ota_get_next_update_partition(esp_ota_get_running_partition());
    if (!partition) return false;
    esp_app_desc_t description{};
    if (esp_ota_get_partition_description(partition, &description) != ESP_OK) return false;

    const String localPath = lilka::fileutils.getLocalPathInfo(path).path;
    Preferences prefs;
    if (!prefs.begin("lilka", true)) return false;
    const bool samePath = prefs.getString("multiboot_path", "") == localPath;
    prefs.end();
    if (!samePath) return false;

    FILE* file = fopen(path.c_str(), "rb");
    if (!file) return false;
    bool matches = false;
    if (fseek(file, 0, SEEK_END) == 0) {
        const long fileSize = ftell(file);
        if (fileSize >= 4096 && static_cast<size_t>(fileSize) <= partition->size && fseek(file, 0, SEEK_SET) == 0) {
            // The image header and SHA trailer detect a replaced guest without scanning the whole image.
            constexpr size_t sampleSize = 1024;
            uint8_t fileBytes[sampleSize];
            uint8_t flashBytes[sampleSize];
            matches = true;
            const size_t size = static_cast<size_t>(fileSize);
            const size_t count = std::min(sampleSize, size);
            for (int sample = 0; sample < (size > count ? 2 : 1); ++sample) {
                const size_t offset = sample ? size - count : 0;
                if (fseek(file, static_cast<long>(offset), SEEK_SET) != 0 ||
                    fread(fileBytes, 1, count, file) != count ||
                    esp_partition_read(partition, offset, flashBytes, count) != ESP_OK ||
                    memcmp(fileBytes, flashBytes, count) != 0) {
                    matches = false;
                    break;
                }
            }
        }
    }
    fclose(file);
    return matches;
}

bool bootInstalledGuest(const String& path) {
    const esp_partition_t* partition = nullptr;
    if (!matchesInstalledGuest(path, partition)) return false;

    const String localPath = lilka::fileutils.getLocalPathInfo(path).path;
    Preferences prefs;
    if (!prefs.begin("lilka", false)) return false;
    const bool saved = prefs.putString("multiboot_path", localPath) == localPath.length();
    prefs.end();
    if (!saved || esp_ota_set_boot_partition(partition) != ESP_OK) return false;

    rememberGuestShortcut(path, readGuestShortcuts());
    esp_restart();
    return true; // unreachable
}
} // namespace

MultiBootApp::MultiBootApp(const String& path) : App("MultiBoot") {
    this->firmwarePath = path;
    setktStackSize(8192); // Multiboot internally uses 4KB chunk
}

void MultiBootApp::run() {
    if (firmwarePath == "") lilka::multiboot.bootLast();
    else if (!bootInstalledGuest(firmwarePath)) fileLoadAsRom(firmwarePath);
}

void MultiBootApp::fileLoadAsRom(const String& path) {
    const std::vector<String> previousGuests = readGuestShortcuts();
    // Draw Welcome message
    lilka::ProgressDialog dialog(K_S_FMANAGER_LOADING, path + "\n\n" K_S_FMANAGER_MULTIBOOT_STARTING);
    dialog.draw(canvas);
    queueDraw();

    // Trying to start upload
    int error;
    error = lilka::multiboot.start(path);
    if (error) {
        alert(K_S_ERROR, StringFormat(K_S_FMANAGER_MULTIBOOT_ERROR_FMT, 1, error));
        return;
    }
    dialog.setMessage(StringFormat(
        K_S_FMANAGER_MULTIBOOT_ABOUT_FMT,
        path.c_str(),
        lilka::fileutils.getHumanFriendlySize(lilka::multiboot.getBytesTotal()).c_str()
    ));
    dialog.draw(canvas);
    queueDraw();

    while ((error = lilka::multiboot.process()) > 0) {
        int progress = lilka::multiboot.getBytesWritten() * 100 / lilka::multiboot.getBytesTotal();
        dialog.setProgress(progress);
        dialog.draw(canvas);
        queueDraw();
        if (lilka::controller.getState().a.justPressed) {
            lilka::multiboot.cancel();
            return;
        }
    }
    if (error < 0) {
        alert(K_S_ERROR, StringFormat(K_S_FMANAGER_MULTIBOOT_ERROR_FMT, 2, error));
        return;
    }
    rememberGuestShortcut(path, previousGuests);
    error = lilka::multiboot.finishAndReboot();
    if (error) {
        alert(K_S_ERROR, StringFormat(K_S_FMANAGER_MULTIBOOT_ERROR_FMT, 3, error));
        return;
    }
}
