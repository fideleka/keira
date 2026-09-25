#include "multiboot.h"
#include "keira/keira.h"
#include "keira/utils/string.h"
#include "apps/scummvm/recent.h"

#include <Preferences.h>

MultiBootApp::MultiBootApp(
    const String& path, const String& command, const String& gameManifest, const String& gameTitle
) :
    App("MultiBoot"), command(command), gameManifest(gameManifest), gameTitle(gameTitle) {
    this->firmwarePath = path;
    setktStackSize(8192); // Multiboot internally uses 4KB chunk
}

void MultiBootApp::run() {
    if (firmwarePath == "") lilka::multiboot.bootLast();
    else fileLoadAsRom(firmwarePath);
}

void MultiBootApp::fileLoadAsRom(const String& path) {
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
    // start() has already changed the SDK's last-image path. Hide any old
    // game shortcut until this complete image and its metadata are installed.
    {
        Preferences prefs;
        if (prefs.begin("lilka", false)) {
            prefs.remove(scummvm_recent::kImageKey);
            prefs.end();
        }
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

    // The SDK has already recorded the last image path. Commit game metadata
    // only after a complete image write; a raw .bin replaces the prior game.
    Preferences prefs;
    if (prefs.begin("lilka", false)) {
        prefs.remove(scummvm_recent::kImageKey);
        if (!gameManifest.isEmpty() && !gameTitle.isEmpty()) {
            const String imagePath = lilka::fileutils.getLocalPathInfo(path).path;
            if (prefs.putString(scummvm_recent::kManifestKey, gameManifest) == gameManifest.length() &&
                prefs.putString(scummvm_recent::kTitleKey, gameTitle) == gameTitle.length()) {
                prefs.putString(scummvm_recent::kImageKey, imagePath);
            }
        } else {
            prefs.remove(scummvm_recent::kManifestKey);
            prefs.remove(scummvm_recent::kTitleKey);
        }
        prefs.end();
    }

    if (!command.isEmpty()) lilka::multiboot.setCMDParams(command);
    error = lilka::multiboot.finishAndReboot();
    if (error) {
        Preferences prefs;
        if (prefs.begin("lilka", false)) {
            prefs.remove(scummvm_recent::kImageKey);
            prefs.end();
        }
        alert(K_S_ERROR, StringFormat(K_S_FMANAGER_MULTIBOOT_ERROR_FMT, 3, error));
        return;
    }
}
