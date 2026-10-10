#pragma once

#include "services/network/network.h"
#include "apps/icons/app.h"
#include "keira/app.h"
#include "keira/keira.h"

enum class LauncherMenuKind { Static, Applications, NES, GameBoy, GameBoyColor };

typedef struct item_t {
    String name;
    const menu_icon_t* icon;
    uint16_t color;
    std::vector<item_t> submenu;
    std::function<void()> callback;
    std::function<void(void*)> update;
    LauncherMenuKind kind; // Factories initialize this explicitly to keep item_t a C++11 aggregate.

public:
    static item_t SUBMENU(
        const char* name, const std::vector<item_t>& submenu, const menu_icon_t* icon = NULL,
        uint16_t color = lilka::colors::White, LauncherMenuKind kind = LauncherMenuKind::Static,
        std::function<void(void*)> update = nullptr
    ) {
        return item_t{
            name,
            icon,
            color,
            submenu,
            nullptr,
            update,
            kind,
        };
    }

    static item_t MENU(
        const char* name, std::function<void()> callback, const menu_icon_t* icon = NULL,
        uint16_t color = lilka::colors::White, std::function<void(void*)> update = nullptr
    ) {
        return item_t{
            name,
            icon,
            color,
            {},
            callback,
            update,
            LauncherMenuKind::Static,
        };
    }

    static item_t APP(
        const char* name, std::function<void()> callback, const menu_icon_t* icon = NULL,
        uint16_t color = lilka::colors::White, std::function<void(void*)> update = nullptr
    ) {
        return item_t::MENU(name, callback, icon ? icon : &app_img, color, update);
    }

} ITEM;

#define ITEM_LIST std::vector<item_t>

class LauncherApp : public App {
public:
    LauncherApp();

private:
    void run() override;
    const String& wifiMenuStatus();
    String wifiMenuStatus_;
    uint32_t wifiMenuStatusUpdated_ = 0;
    bool wifiMenuStatusValid_ = false;
    // Keep initializer-list construction frames off the retained launcher task frame.
    ITEM_LIST buildApplicationsMenu() __attribute__((noinline));
    item_t buildMainMenu(const ITEM_LIST& appsItems) __attribute__((noinline));
    ITEM_LIST loadCatalogItems();
    std::vector<String> catalogItemNames_;
    std::vector<String> guestShortcutNames_;
    void refreshRecentRomFolders(ITEM_LIST& apps);
    void refreshRecentRomItems(ITEM_LIST& items, LauncherMenuKind kind);
    // Cached autorun setting, menu redraws every frame and it's stored in NVS
    bool autorunEnabled = false;

    void homeScreen(item_t& mainMenu);
    void showMenu(
        const String& title, ITEM_LIST& menu, bool back = true, LauncherMenuKind kind = LauncherMenuKind::Static
    );
    template <typename T, typename... Args>
    void runApp(Args&&... args);
    void setWiFiTxPower();
    void wifiToggle();
    void wifiManager();
    void setSpiSDSpeed();
    void setMDNSHostname();
    void setTimezone();
    void setCustomTimezone();
    void setFixedUtcOffset();
    String getTimezoneLabel(const String& timezone, int presetId);
    void about();
    void info();
    void showEasterEgg();
    void formatSD();
    void factoryReset();
};
