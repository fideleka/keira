#include <ff.h>
#include <FS.h>
#include <qrcode.h>
#include "keira/utils/mem.h"
#include "keira/keira.h"
#include "launcher.h"
#include "wallpaper.h"
#include "apps/scummvm/recent.h"
#include "keira/appmanager.h"

#include "keira/servicemanager.h"
// Services:
#include "services/network/network.h"
#include "services/ftp/ftp.h"
#include "services/telnet/telnet.h"
#include "services/web/web.h"
#include "services/mdns/mdns.h"
#include "services/clock/clock.h"
// Demos:
#include "apps/demos/lines/lines.h"
#include "apps/demos/disk/disk.h"
#include "apps/demos/ball/ball.h"
#include "apps/demos/transform/transform.h"
#include "apps/demos/cube/cube.h"
#include "apps/demos/epilepsy/epilepsy.h"
#include "apps/demos/petpet/petpet.h"
// Tests:
#include "apps/tests/keyboard/keyboard.h"
#include "apps/tests/user_spi/user_spi.h"
#include "apps/tests/scan_i2c/scan_i2c.h"
#include "apps/tests/callbacktest/callbacktest.h"
#include "apps/tests/combo/combo.h"
// Apps
#include "apps/statusbar/statusbar.h"
#include "apps/wificonfig/wificonfig.h"
#include "apps/letris/letris.h"
#include "apps/gpiomanager/gpiomanager.h"
#include "apps/tamagotchi/tamagotchi.h"
#include "apps/lua/luarunner.h"
#include "apps/mjs/mjsrunner.h"
#include "apps/nes/nesapp.h"
#include "apps/weather/weather.h"
#include "apps/madplayer/madplayer.h"
#include "apps/lilcatalog/lilcatalog.h"
#include "apps/liltracker/liltracker.h"
#include "apps/fmanager/fmanager.h"
#include "apps/pastebin/pastebinApp.h"
#include "apps/usbdrive/usbdrive.h"
#include "apps/soundsettings/sound.h"

// Icons
#include "apps/icons/demos.h"
#include "apps/icons/sdcard.h"
#include "apps/icons/memory.h"
#include "apps/icons/dev.h"
#include "apps/icons/settings.h"
#include "apps/icons/info.h"
#include "apps/icons/app_group.h"

// Libs
#include <WiFi.h> // for setWiFiTxPower
#include <Preferences.h>
#include <lilka/spi.h>
#include <nvs_flash.h>

#include "keira/ksystem.h"
#include "keira/utils/string.h"

namespace {
bool isDecimalNumber(const String& value) {
    if (value.isEmpty()) {
        return false;
    }
    for (size_t i = 0; i < value.length(); i++) {
        if (value[i] < '0' || value[i] > '9') {
            return false;
        }
    }
    return true;
}

bool parseUtcOffset(const String& input, int16_t& offsetMinutes) {
    String value = input;
    value.trim();
    if (value.isEmpty()) {
        return false;
    }

    int8_t sign = 1;
    if (value[0] == '+' || value[0] == '-') {
        sign = value[0] == '-' ? -1 : 1;
        value.remove(0, 1);
    }

    int separator = value.indexOf(':');
    if (separator >= 0 && value.indexOf(':', separator + 1) >= 0) {
        return false;
    }

    String hoursText = separator >= 0 ? value.substring(0, separator) : value;
    String minutesText = separator >= 0 ? value.substring(separator + 1) : "0";
    if (!isDecimalNumber(hoursText) || !isDecimalNumber(minutesText)) {
        return false;
    }

    int hours = hoursText.toInt();
    int minutes = minutesText.toInt();
    if (hours > 14 || minutes > 59 || (hours == 14 && minutes != 0)) {
        return false;
    }

    offsetMinutes = sign * (hours * 60 + minutes);
    return true;
}

String formatUtcOffset(int16_t offsetMinutes) {
    int absoluteMinutes = abs(offsetMinutes);
    return StringFormat("%c%02d:%02d", offsetMinutes < 0 ? '-' : '+', absoluteMinutes / 60, absoluteMinutes % 60);
}

String timezoneFromUtcOffset(int16_t offsetMinutes) {
    if (offsetMinutes == 0) {
        return CLOCK_TIMEZONE_UTC;
    }

    int absoluteMinutes = abs(offsetMinutes);
    // POSIX TZ offsets have the opposite sign from the familiar UTC offset.
    String timezone = offsetMinutes > 0 ? "UTC-" : "UTC";
    timezone += String(absoluteMinutes / 60);
    if (absoluteMinutes % 60 != 0) {
        timezone += ":";
        timezone += String(absoluteMinutes % 60);
    }
    return timezone;
}

bool utcOffsetFromTimezone(const String& timezone, int16_t& offsetMinutes) {
    if (!timezone.startsWith("UTC")) {
        return false;
    }

    int16_t posixOffsetMinutes;
    if (!parseUtcOffset(timezone.substring(3), posixOffsetMinutes)) {
        return false;
    }
    offsetMinutes = -posixOffsetMinutes;
    return true;
}
} // namespace
// Home screen wallpaper, first existing one is used
static const char* const WALLPAPER_PATHS[] =
    {"/sd/wallpaper.gif", "/sd/wallpaper.png", "/sd/wallpaper.jpg", "/sd/wallpaper.jpeg", "/sd/wallpaper.bmp"};

LauncherApp::LauncherApp() : App("Launcher") {
    setktStackSize(8192); // Yeah, this one is heavy as fuck
}

void LauncherApp::run() {
#ifdef KEIRA_DEBUG_APP
#    ifdef KEIRA_DEBUG_APP_PARAMS
    this->runApp<KEIRA_DEBUG_APP>(KEIRA_DEBUG_APP_PARAMS);
#    else
    this->runApp<KEIRA_DEBUG_APP>();
#    endif
#endif
    for (lilka::Button button : {lilka::Button::UP, lilka::Button::DOWN, lilka::Button::LEFT, lilka::Button::RIGHT}) {
        lilka::controller.setAutoRepeat(button, 10, 300);
    }

    ITEM_LIST appsItems = {
        ITEM::SUBMENU(
            K_S_LAUNCHER_DEMOS,
            {
                ITEM::APP(K_S_LAUNCHER_LINES, [this]() { this->runApp<DemoLines>(); }),
                ITEM::APP(K_S_LAUNCHER_DISK, [this]() { this->runApp<DiskApp>(); }),
                ITEM::APP(K_S_LAUNCHER_TRANSFORM, [this]() { this->runApp<TransformApp>(); }),
                ITEM::APP(K_S_LAUNCHER_BALL, [this]() { this->runApp<BallApp>(); }),
                ITEM::APP(K_S_LAUNCHER_CUBE, [this]() { this->runApp<CubeApp>(); }),
                ITEM::APP(K_S_LAUNCHER_EPILEPSY, [this]() { this->runApp<EpilepsyApp>(); }),
                ITEM::APP(K_S_LAUNCHER_PET_PET, [this]() { this->runApp<PetPetApp>(); }),
            },
            &app_group_img,
            lilka::colors::White
        ),
        ITEM::SUBMENU(
            K_S_LAUNCHER_TESTS,
            {
                ITEM::APP(K_S_LAUNCHER_KEYBOARD, [this]() { this->runApp<KeyboardApp>(); }),
                ITEM::APP(K_S_LAUNCHER_SPI_TEST, [this]() { this->runApp<UserSPIApp>(); }),
                ITEM::APP(K_S_LAUNCHER_I2C_SCANNER, [this]() { this->runApp<ScanI2CApp>(); }),
                ITEM::APP(K_S_LAUNCHER_COMBO, [this]() { this->runApp<ComboApp>(); }),
                ITEM::APP(K_S_LAUNCHER_CALLBACK_TEST, [this]() { this->runApp<CallBackTestApp>(); }),
            },
            &app_group_img,
            lilka::colors::White
        ),
        ITEM::APP(K_S_LAUNCHER_LILCATALOG, [this]() { this->runApp<LilCatalogApp>(); }),
        ITEM::APP(K_S_LAUNCHER_LILTRACKER, [this]() { this->runApp<LilTrackerApp>(); }),
        ITEM::APP(K_S_LAUNCHER_LETRIS, [this]() { this->runApp<LetrisApp>(); }),
        ITEM::APP(K_S_LAUNCHER_TAMAGOTCHI, [this]() { this->runApp<TamagotchiApp>(); }),
        ITEM::APP(K_S_LAUNCHER_WEATHER, [this]() { this->runApp<WeatherApp>(); }),
        ITEM::APP(K_S_LAUNCHER_PASTEBIN, [this]() { this->runApp<pastebinApp>(); }),
        ITEM::APP(K_S_LAUNCHER_GPIO_MANAGER, [this]() { this->runApp<GPIOManagerApp>(); }),
    };
    for (auto& item : loadCatalogItems()) {
        appsItems.push_back(std::move(item));
    }

    // Check name of the last loaded guest OTA firmware
    String lastOTA = "";
    String lastGameManifest;
    String lastGameTitle;
    String lastGameImage;
    Preferences prefs;
    prefs.begin("lilka", false);

//TODO: make the corresponding define public in libdeps/lilka/multiboot.h
#define MULTIBOOT_PATH_KEY "multiboot_path"

    if (prefs.isKey(MULTIBOOT_PATH_KEY)) {
        lastOTA = prefs.getString(MULTIBOOT_PATH_KEY);
    }
    lastGameManifest = prefs.getString(scummvm_recent::kManifestKey, "");
    lastGameTitle = prefs.getString(scummvm_recent::kTitleKey, "");
    lastGameImage = prefs.getString(scummvm_recent::kImageKey, "");
    prefs.end();

    // Insert it into the Applications menu
    if (!lastOTA.isEmpty()) {
        if (lastOTA == lastGameImage && lastGameManifest.startsWith("/sd/") && lastGameManifest.length() <= 512 &&
            !lastGameTitle.isEmpty() && lastGameTitle.length() <= 80) {
            appsItems.insert(appsItems.begin(), ITEM::APP(lastGameTitle.c_str(), [this, lastGameManifest]() {
                                 this->runApp<ScummVMManagerApp>(lastGameManifest, false);
                             }));
        } else {
            appsItems.insert(appsItems.begin(), ITEM::APP(lastOTA.c_str(), [this]() { this->runApp<MultiBootApp>(); }));
        }
    }

    item_t root_item = ITEM::SUBMENU(
        K_S_LAUNCHER_MAIN_MENU,
        {
            ITEM::SUBMENU(K_S_LAUNCHER_APPS, appsItems, &demos_img, lilka::colors::Pink),
            ITEM::APP(
                K_S_LAUNCHER_FMANAGER,
                [this]() { this->runApp<FileManagerApp>("/"); },
                &sdcard_img,
                lilka::colors::Arylide_yellow
            ),
            ITEM::APP(
                K_S_LAUNCHER_USB_DRIVE, [this]() { this->runApp<USBDriveApp>(); }, &memory_img, lilka::colors::Mint
            ),
            ITEM::SUBMENU(
                K_S_LAUNCHER_DEV_MENU,
                {
                    ITEM::APP(K_S_LAUNCHER_LIVE_LUA, [this]() { this->runApp<LuaLiveRunnerApp>(); }),
                    ITEM::APP(K_S_LAUNCHER_LUA_REPL, [this]() { this->runApp<LuaReplApp>(); }),
                },
                &dev_img,
                lilka::colors::Jasmine
            ),
            ITEM::SUBMENU(
                K_S_SETTINGS,
                {
                    ITEM::SUBMENU(
                        K_S_LAUNCHER_WIFI,
                        {
                            ITEM::MENU(
                                K_S_LAUNCHER_WIFI_ADAPTER,
                                [this]() { this->wifiToggle(); },
                                nullptr,
                                lilka::colors::White,
                                [this](void* item) {
                                    lilka::MenuItem* menuItem = static_cast<lilka::MenuItem*>(item);
                                    NetworkService* networkService =
                                        static_cast<NetworkService*>(ksystem.services["network"]);
                                    menuItem->postfix = networkService->getEnabled() ? K_S_ON : K_S_OFF;
                                }
                            ),
                            ITEM::MENU(K_S_LAUNCHER_WIFI_NETWORKS, [this]() { this->wifiManager(); }),
                            ITEM::MENU(K_S_LAUNCHER_WIFI_TX_POWER, [this]() { this->setWiFiTxPower(); }),
                        }
                    ),
                    ITEM::MENU(
                        K_S_LAUNCHER_TIMEZONE,
                        [this]() { this->setTimezone(); },
                        nullptr,
                        lilka::colors::White,
                        [this](void* item) {
                            lilka::MenuItem* menuItem = static_cast<lilka::MenuItem*>(item);
                            ClockService* clockService = static_cast<ClockService*>(ksystem.services["clock"]);
                            menuItem->postfix = getTimezoneLabel(clockService->getTimezone());
                        }
                    ),
                    ITEM::SUBMENU(
                        K_S_LAUNCHER_SD,
                        {
                            ITEM::MENU(K_S_PARTITION_TABLE, [this]() { this->partitions(); }),
                            ITEM::MENU(K_S_LAUNCHER_SD_FORMAT, [this]() { this->formatSD(); }),
                            ITEM::MENU(K_S_LAUNCHER_SD_SPEED, [this]() { this->setSpiSDSpeed(); }),
                        }
                    ),
                    ITEM::SUBMENU(
                        K_S_LAUNCHER_BATTERY_SETTINGS,
                        {
                            ITEM::MENU(
                                K_S_LAUNCHER_BATTERY_SET_FULL,
                                [this]() { this->calibrateBatteryFullLevel(); },
                                nullptr,
                                lilka::colors::White,
                                [](void* item) {
                                    lilka::MenuItem* menuItem = static_cast<lilka::MenuItem*>(item);
                                    menuItem->postfix = lilka::battery.hasFullLevelCalibration() ? "[x]" : "[ ]";
                                }
                            ),
                            ITEM::MENU(
                                K_S_LAUNCHER_BATTERY_RESET_FULL, [this]() { this->resetBatteryFullLevelCalibration(); }
                            ),
                            ITEM::MENU(
                                K_S_LAUNCHER_BATTERY_DISCHARGE_PROFILE,
                                []() {
                                    auto profile = lilka::battery.getDischargeProfile();
                                    lilka::BatteryDischargeProfile nextProfile;
                                    switch (profile) {
                                        case lilka::BatteryDischargeProfile::Normal:
                                            nextProfile = lilka::BatteryDischargeProfile::SharpTop;
                                            break;
                                        case lilka::BatteryDischargeProfile::SharpTop:
                                            nextProfile = lilka::BatteryDischargeProfile::SmoothTop;
                                            break;
                                        case lilka::BatteryDischargeProfile::SmoothTop:
                                            nextProfile = lilka::BatteryDischargeProfile::SharpBottom;
                                            break;
                                        case lilka::BatteryDischargeProfile::SharpBottom:
                                            nextProfile = lilka::BatteryDischargeProfile::SmoothBottom;
                                            break;
                                        case lilka::BatteryDischargeProfile::SmoothBottom:
                                            nextProfile = lilka::BatteryDischargeProfile::SharpEnds;
                                            break;
                                        case lilka::BatteryDischargeProfile::SharpEnds:
                                            nextProfile = lilka::BatteryDischargeProfile::SmoothEnds;
                                            break;
                                        case lilka::BatteryDischargeProfile::SmoothEnds:
                                        default:
                                            nextProfile = lilka::BatteryDischargeProfile::Normal;
                                            break;
                                    }
                                    lilka::battery.setDischargeProfile(nextProfile);
                                },
                                nullptr,
                                lilka::colors::White,
                                [](void* item) {
                                    lilka::MenuItem* menuItem = static_cast<lilka::MenuItem*>(item);
                                    switch (lilka::battery.getDischargeProfile()) {
                                        case lilka::BatteryDischargeProfile::SharpTop:
                                            menuItem->postfix = K_S_LAUNCHER_BATTERY_PROFILE_SHARP_TOP;
                                            break;
                                        case lilka::BatteryDischargeProfile::SmoothTop:
                                            menuItem->postfix = K_S_LAUNCHER_BATTERY_PROFILE_SMOOTH_TOP;
                                            break;
                                        case lilka::BatteryDischargeProfile::SharpBottom:
                                            menuItem->postfix = K_S_LAUNCHER_BATTERY_PROFILE_SHARP_BOTTOM;
                                            break;
                                        case lilka::BatteryDischargeProfile::SmoothBottom:
                                            menuItem->postfix = K_S_LAUNCHER_BATTERY_PROFILE_SMOOTH_BOTTOM;
                                            break;
                                        case lilka::BatteryDischargeProfile::SharpEnds:
                                            menuItem->postfix = K_S_LAUNCHER_BATTERY_PROFILE_SHARP_ENDS;
                                            break;
                                        case lilka::BatteryDischargeProfile::SmoothEnds:
                                            menuItem->postfix = K_S_LAUNCHER_BATTERY_PROFILE_SMOOTH_ENDS;
                                            break;
                                        case lilka::BatteryDischargeProfile::Normal:
                                        default:
                                            menuItem->postfix = K_S_LAUNCHER_BATTERY_PROFILE_NORMAL;
                                            break;
                                    }
                                }
                            ),
                        }
                    ),
                    ITEM::MENU(K_S_LAUNCHER_SOUND, [this]() { this->runApp<SoundConfigApp>(); }),
                    ITEM::SUBMENU(
                        K_S_LAUNCHER_SERVICES,
                        {
                            ITEM::SUBMENU(
                                K_S_LAUNCHER_WEB,
                                {
                                    ITEM::MENU(
                                        K_S_STATUS,
                                        [this]() {
                                            WebService* webService = static_cast<WebService*>(ksystem.services["web"]);
                                            webService->setEnabled(!webService->getEnabled());
                                        },
                                        nullptr,
                                        lilka::colors::White,
                                        [this](void* item) {
                                            lilka::MenuItem* menuItem = static_cast<lilka::MenuItem*>(item);
                                            WebService* webService = static_cast<WebService*>(ksystem.services["web"]);
                                            menuItem->postfix = webService->getEnabled() ? K_S_ON : K_S_OFF;
                                        }
                                    ),
                                }
                            ),
                            ITEM::SUBMENU(
                                K_S_LAUNCHER_TELNET,
                                {
                                    ITEM::MENU(
                                        K_S_STATUS,
                                        [this]() {
                                            TelnetService* telnetService =
                                                static_cast<TelnetService*>(ksystem.services["telnet"]);
                                            telnetService->setEnabled(!telnetService->getEnabled());
                                        },
                                        nullptr,
                                        lilka::colors::White,
                                        [this](void* item) {
                                            lilka::MenuItem* menuItem = static_cast<lilka::MenuItem*>(item);
                                            TelnetService* telnetService =
                                                static_cast<TelnetService*>(ksystem.services["telnet"]);
                                            menuItem->postfix = telnetService->getEnabled() ? K_S_ON : K_S_OFF;
                                        }
                                    ),
                                }
                            ),
                            ITEM::SUBMENU(
                                K_S_LAUNCHER_FTP,
                                {
                                    ITEM::MENU(
                                        K_S_STATUS,
                                        [this]() {
                                            FTPService* ftpService = static_cast<FTPService*>(ksystem.services["ftp"]);
                                            ftpService->setEnabled(!ftpService->getEnabled());
                                        },
                                        nullptr,
                                        lilka::colors::White,
                                        [this](void* item) {
                                            lilka::MenuItem* menuItem = static_cast<lilka::MenuItem*>(item);
                                            FTPService* ftpService = static_cast<FTPService*>(ksystem.services["ftp"]);
                                            menuItem->postfix = ftpService->getEnabled() ? K_S_ON : K_S_OFF;
                                        }
                                    ),
                                    ITEM::MENU(
                                        K_S_LAUNCHER_FTP_USER,
                                        nullptr,
                                        nullptr,
                                        lilka::colors::White,
                                        [this](void* item) {
                                            lilka::MenuItem* menuItem = static_cast<lilka::MenuItem*>(item);
                                            FTPService* ftpService = static_cast<FTPService*>(ksystem.services["ftp"]);
                                            menuItem->postfix = ftpService->getUser();
                                        }
                                    ),
                                    ITEM::MENU(
                                        K_S_LAUNCHER_FTP_PASSWORD,
                                        [this]() {
                                            FTPService* ftpService = static_cast<FTPService*>(ksystem.services["ftp"]);
                                            ftpService->createPassword();
                                        },
                                        nullptr,
                                        lilka::colors::White,
                                        [this](void* item) {
                                            lilka::MenuItem* menuItem = static_cast<lilka::MenuItem*>(item);
                                            FTPService* ftpService = static_cast<FTPService*>(ksystem.services["ftp"]);
                                            menuItem->postfix = ftpService->getPassword();
                                        }
                                    ),
                                    ITEM::MENU(
                                        K_S_LAUNCHER_FTP_IP,
                                        nullptr,
                                        nullptr,
                                        lilka::colors::White,
                                        [this](void* item) {
                                            lilka::MenuItem* menuItem = static_cast<lilka::MenuItem*>(item);
                                            NetworkService* networkService =
                                                static_cast<NetworkService*>(ksystem.services["network"]);
                                            menuItem->postfix = networkService->getipAddr();
                                        }
                                    ),
                                }
                            ),
                            ITEM::SUBMENU(
                                K_S_LAUNCHER_MDNS,
                                {
                                    ITEM::MENU(
                                        K_S_STATUS,
                                        [this]() {
                                            MDNSService* mdnsService =
                                                static_cast<MDNSService*>(ksystem.services["mdns"]);
                                            mdnsService->setEnabled(!mdnsService->getEnabled());
                                        },
                                        nullptr,
                                        lilka::colors::White,
                                        [this](void* item) {
                                            lilka::MenuItem* menuItem = static_cast<lilka::MenuItem*>(item);
                                            MDNSService* mdnsService =
                                                static_cast<MDNSService*>(ksystem.services["mdns"]);
                                            menuItem->postfix = mdnsService->getEnabled() ? K_S_ON : K_S_OFF;
                                        }
                                    ),
                                    ITEM::MENU(
                                        K_S_LAUNCHER_MDNS_HOSTNAME,
                                        [this]() { this->setMDNSHostname(); },
                                        nullptr,
                                        lilka::colors::White,
                                        [this](void* item) {
                                            lilka::MenuItem* menuItem = static_cast<lilka::MenuItem*>(item);
                                            MDNSService* mdnsService =
                                                static_cast<MDNSService*>(ksystem.services["mdns"]);
                                            menuItem->postfix = mdnsService->getFullHostname();
                                        }
                                    ),
                                }
                            ),
                        }
                    ),
                    ITEM::SUBMENU(
                        K_S_LAUNCHER_STATUSBAR,
                        {
                            ITEM::MENU(
                                K_S_LAUNCHER_CLOCK,
                                [this]() {
                                    auto statusBar = static_cast<StatusBarApp*>(ksystem.apps.getpanel());
                                    statusBar->setClockMode((statusBar->getClockMode() + 1));
                                },
                                nullptr,
                                lilka::colors::White,
                                [this](void* item) {
                                    lilka::MenuItem* menuItem = static_cast<lilka::MenuItem*>(item);
                                    auto statusBar = static_cast<StatusBarApp*>(ksystem.apps.getpanel());
                                    switch (statusBar->getClockMode()) {
                                        case 0:
                                            menuItem->postfix = K_S_LAUNCHER_CLOCK_0;
                                            break;
                                        case 1:
                                            menuItem->postfix = K_S_LAUNCHER_CLOCK_1;
                                            break;
                                        case 2:
                                            menuItem->postfix = K_S_LAUNCHER_CLOCK_2;
                                            break;
                                        default:
                                            menuItem->postfix = K_S_LAUNCHER_CLOCK_3;
                                            break;
                                    }
                                }
                            ),
                            ITEM::MENU(
                                K_S_LAUNCHER_MEM,
                                [this]() {
                                    auto statusBar = static_cast<StatusBarApp*>(ksystem.apps.getpanel());
                                    statusBar->setMemMode((statusBar->getMemMode() + 1));
                                },
                                nullptr,
                                lilka::colors::White,
                                [this](void* item) {
                                    lilka::MenuItem* menuItem = static_cast<lilka::MenuItem*>(item);
                                    auto statusBar = static_cast<StatusBarApp*>(ksystem.apps.getpanel());
                                    switch (statusBar->getMemMode()) {
                                        case 0:
                                            menuItem->postfix = K_S_LAUNCHER_MEM_0;
                                            break;
                                        case 1:
                                            menuItem->postfix = K_S_LAUNCHER_MEM_1;
                                            break;
                                        default:
                                            menuItem->postfix = K_S_LAUNCHER_MEM_2;
                                            break;
                                    }
                                }
                            ),
                            ITEM::MENU(
                                K_S_LAUNCHER_NETWORK,
                                [this]() {
                                    auto statusBar = static_cast<StatusBarApp*>(ksystem.apps.getpanel());
                                    statusBar->setNetworkMode((statusBar->getNetworkMode() + 1));
                                },
                                nullptr,
                                lilka::colors::White,
                                [this](void* item) {
                                    lilka::MenuItem* menuItem = static_cast<lilka::MenuItem*>(item);
                                    auto statusBar = static_cast<StatusBarApp*>(ksystem.apps.getpanel());
                                    switch (statusBar->getNetworkMode()) {
                                        case 0:
                                            menuItem->postfix = K_S_LAUNCHER_NETWORK_0;
                                            break;
                                        default:
                                            menuItem->postfix = K_S_LAUNCHER_NETWORK_1;
                                            break;
                                    }
                                }
                            ),
                            ITEM::MENU(
                                K_S_LAUNCHER_BATTERY,
                                [this]() {
                                    auto statusBar = static_cast<StatusBarApp*>(ksystem.apps.getpanel());
                                    statusBar->setBatteryMode((statusBar->getBatteryMode() + 1));
                                },
                                nullptr,
                                lilka::colors::White,
                                [this](void* item) {
                                    lilka::MenuItem* menuItem = static_cast<lilka::MenuItem*>(item);
                                    auto statusBar = static_cast<StatusBarApp*>(ksystem.apps.getpanel());
                                    switch (statusBar->getBatteryMode()) {
                                        case 0:
                                            menuItem->postfix = K_S_LAUNCHER_BATTERY_0;
                                            break;
                                        case 1:
                                            menuItem->postfix = K_S_LAUNCHER_BATTERY_1;
                                            break;
                                        case 2:
                                            menuItem->postfix = K_S_LAUNCHER_BATTERY_2;
                                            break;
                                        case 3:
                                            menuItem->postfix = K_S_LAUNCHER_BATTERY_3;
                                            break;
                                        default:
                                            menuItem->postfix = K_S_LAUNCHER_BATTERY_4;
                                            break;
                                    }
                                }
                            ),
                        }
                    ),
                    ITEM::SUBMENU(
                        K_S_LAUNCHER_MULTIBOOT,
                        {
                            ITEM::MENU(K_S_LAUNCHER_MULTIBOOT_OTA, [this]() { this->runApp<MultiBootApp>(); }),
                        }
                    ),
                    ITEM::SUBMENU(
                        K_S_LAUNCHER_ABOUT,
                        {
                            ITEM::MENU(K_S_OS_NAME, [this]() { this->about(); }),
                            ITEM::MENU(K_S_LAUNCHER_DEVICE_INFO, [this]() { this->info(); }),
                        }
                    ),
                    ITEM::MENU(K_S_LAUNCHER_FACTORY_RESET, [this]() { this->factoryReset(); }),
                    ITEM::MENU(K_S_LAUNCHER_REBOOT, []() { esp_restart(); }),
                },
                &settings_img,
                lilka::colors::Orchid
            ),
        }
    );
    homeScreen(root_item);
}

void LauncherApp::homeScreen(item_t& mainMenu) {
    Wallpaper wallpaper;
    while (1) {
        // Wallpaper is reopened each time to free its memory while the menu and apps are running
        for (const char* path : WALLPAPER_PATHS) {
            if (wallpaper.open(path, canvas->width(), canvas->height())) break;
        }

        while (1) {
            int delayMs = 0;
            if (wallpaper.isOpen()) {
                delayMs = wallpaper.nextFrame();
                wallpaper.draw(canvas);
            } else {
                canvas->fillScreen(lilka::colors::Black);
            }
            canvas->setFont(FONT_9x15);
            canvas->setTextColor(lilka::colors::White);
            canvas->drawTextAligned(
                K_S_LAUNCHER_HOME_HINT, canvas->width() / 2, canvas->height() - 4, lilka::ALIGN_CENTER, lilka::ALIGN_END
            );
            queueDraw();

            // Wait for the next frame while staying responsive to buttons
            TickType_t frameEnd = xTaskGetTickCount() + pdMS_TO_TICKS(delayMs > 0 ? delayMs : 30);
            bool openMenu = false;
            do {
                if (lilka::controller.getState().a.justPressed) {
                    openMenu = true;
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(10));
            } while (xTaskGetTickCount() < frameEnd);
            if (openMenu) break;
        }

        wallpaper.close();
        showMenu(mainMenu.name, mainMenu.submenu);
    }
}
void LauncherApp::showMenu(const char* title, ITEM_LIST& list, bool back) {
    int itemCount = list.size();
    lilka::Menu menu(title);
    for (int i = 0; i < list.size(); i++) {
        menu.addItem(list[i].name, list[i].icon, list[i].color);
    }
    if (back) {
        menu.addActivationButton(K_BTN_BACK);
        menu.addItem(K_S_MENU_BACK);
    }
    while (1) {
        while (!menu.isFinished()) {
            for (int i = 0; i < list.size(); i++) {
                if (list[i].update != nullptr) {
                    lilka::MenuItem menuItem;
                    menu.getItem(i, &menuItem);
                    list[i].update(&menuItem);
                    menu.setItem(i, menuItem.title, menuItem.icon, menuItem.color, menuItem.postfix);
                }
            }
            menu.update();
            menu.draw(canvas);
            queueDraw();
        }
        if (menu.getButton() == K_BTN_BACK) {
            break;
        }
        int16_t index = menu.getCursor();
        if (back && index == itemCount) {
            break;
        }

        item_t item = list[index];
        if (item.callback != nullptr) {
            item.callback();
        }
        if (!item.submenu.empty()) {
            showMenu(item.name, item.submenu);
        }
    }
}
template <typename T, typename... Args>
void LauncherApp::runApp(Args&&... args) {
    ksystem.apps.spawn(new T(std::forward<Args>(args)...));
}

ITEM_LIST LauncherApp::loadCatalogItems() {
    struct ScanEntry {
        String name;
        String execPath;
        ExecutionType type;
    };
    std::vector<ScanEntry> scanned;

    DIR* dir = opendir("/sd/lilcatalog/manifests");
    if (!dir) {
        return {};
    }

    const struct dirent* de;
    while ((de = readdir(dir)) != nullptr) {
        String filename = de->d_name;
        if (!filename.endsWith(".json")) {
            continue;
        }

        String entryId = filename.substring(0, filename.length() - 5);
        String manifestPath = "/sd/lilcatalog/manifests/" + filename;

        FILE* f = fopen(manifestPath.c_str(), "r");
        if (!f) {
            continue;
        }

        fseek(f, 0, SEEK_END);
        long fileSize = ftell(f);
        fseek(f, 0, SEEK_SET);

        if (fileSize <= 0 || fileSize > 8192) {
            fclose(f);
            continue;
        }

        String json;
        char buf[256];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf) - 1, f)) > 0) {
            buf[n] = '\0';
            json += buf;
        }
        fclose(f);

        JsonDocument doc(&spiRamAllocator);
        if (deserializeJson(doc, json)) {
            continue;
        }

        String name = doc["name"].as<String>();
        if (name.isEmpty()) {
            continue;
        }

        String execTypeStr;
        String execLocation;
        if (doc.containsKey("entryfile")) {
            execTypeStr = doc["entryfile"]["type"].as<String>();
            execLocation = doc["entryfile"]["location"].as<String>();
        }
        if (execLocation.isEmpty()) {
            continue;
        }

        String execPath = "/sd/lilcatalog/" + entryId + "/" + execLocation;
        FILE* execCheck = fopen(execPath.c_str(), "r");
        if (!execCheck) {
            continue;
        }
        fclose(execCheck);

        ExecutionType execType = EXEC_TYPE_UNKNOWN;
        if (execTypeStr == "lua") {
            execType = EXEC_TYPE_LUA;
        } else if (execTypeStr == "binary") {
            execType = EXEC_TYPE_BINARY;
        } else if (execTypeStr == "dynapp" || execTypeStr == "so") {
            execType = EXEC_TYPE_DYNAPP;
        } else {
            String loc = execLocation;
            loc.toLowerCase();
            if (loc.endsWith(".lua")) execType = EXEC_TYPE_LUA;
            else if (loc.endsWith(".bin")) execType = EXEC_TYPE_BINARY;
            else if (loc.endsWith(".so")) execType = EXEC_TYPE_DYNAPP;
        }

        if (execType == EXEC_TYPE_UNKNOWN) {
            continue;
        }

        scanned.push_back({name, execPath, execType});
    }
    closedir(dir);

    ITEM_LIST items;
    catalogItemNames_.reserve(scanned.size());
    for (auto& e : scanned) {
        catalogItemNames_.push_back(e.name);
        const char* nameCStr = catalogItemNames_.back().c_str();
        String execPath = e.execPath;
        ExecutionType execType = e.type;
        items.push_back(
            ITEM::APP(
                nameCStr,
                [this, execPath, execType]() {
                    switch (execType) {
                        case EXEC_TYPE_LUA:
                            ksystem.apps.spawn(new LuaFileRunnerApp(execPath));
                            break;
                        case EXEC_TYPE_BINARY:
                            ksystem.apps.spawn(new MultiBootApp(execPath));
                            break;
                        case EXEC_TYPE_DYNAPP:
                            ksystem.apps.spawn(new DynApp(execPath));
                            break;
                        default:
                            break;
                    }
                },
                nullptr,
                lilka::colors::Aquamarine
            )
        );
    }
    return items;
}

void LauncherApp::setWiFiTxPower() {
    String names[] = {
        "19.5 dBm", "19 dBm", "18.5 dBm", "17 dBm", "15 dBm", "13 dBm", "11 dBm", "8.5 dBm", "7 dBm", "2 dBm", "-1 dBm"
    };
    wifi_power_t values[] = {
        WIFI_POWER_19_5dBm,
        WIFI_POWER_19dBm,
        WIFI_POWER_18_5dBm,
        WIFI_POWER_17dBm,
        WIFI_POWER_15dBm,
        WIFI_POWER_13dBm,
        WIFI_POWER_11dBm,
        WIFI_POWER_8_5dBm,
        WIFI_POWER_7dBm,
        WIFI_POWER_5dBm,
        WIFI_POWER_2dBm,
        WIFI_POWER_MINUS_1dBm
    };
    lilka::Menu wifiSetTxMenu;
    wifiSetTxMenu.setTitle(K_S_LAUNCHER_SELECT_TX_POWER);
    wifiSetTxMenu.addActivationButton(K_BTN_BACK); // Exit
    // Add names
    for (auto i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        wifiSetTxMenu.addItem(names[i]);
    // Perform draw
    while (!wifiSetTxMenu.isFinished()) {
        wifiSetTxMenu.update();
        wifiSetTxMenu.draw(canvas);
        queueDraw();
    }
    auto button = wifiSetTxMenu.getButton();

    if (button == K_BTN_BACK) return;

    auto index = wifiSetTxMenu.getCursor();

    // Set power immediately
    WiFi.setTxPower(values[index]);

    // Save value to NVS
    NVS_LOCK;
    Preferences prefs;
    // This is a bit dumb, stor somewhere
    prefs.begin(ksystem.services["network"]->getName(), false);
    prefs.putInt("txPower", static_cast<int>(values[index]));
    prefs.end();
    NVS_UNLOCK;
}

void LauncherApp::setSpiSDSpeed() {
    uint32_t sdFrequencies[] = {
        4000000, // 4  MHz
        16000000, // 16 MHz
        20000000, // 20 MHz
        40000000, // 40 MHz
        80000000 // 80 MHz
    };

    lilka::Menu setSpiSDSpeedMenu;
    setSpiSDSpeedMenu.setTitle(K_S_LAUNCHER_SD_SPEED);
    setSpiSDSpeedMenu.addActivationButton(K_BTN_BACK); // Exit

    // Add frequencies to menu
    for (auto i = 0; i < sizeof(sdFrequencies) / sizeof(sdFrequencies[0]); i++)
        setSpiSDSpeedMenu.addItem(StringFormat("%d MHz", sdFrequencies[i] / 1000000));

    // Perform draw
    while (!setSpiSDSpeedMenu.isFinished()) {
        setSpiSDSpeedMenu.update();
        setSpiSDSpeedMenu.draw(canvas);
        queueDraw();
    }
    auto button = setSpiSDSpeedMenu.getButton();

    if (button == K_BTN_BACK) return;

    auto index = setSpiSDSpeedMenu.getCursor();

    // store new frequency to NVS
    NVS_LOCK;
    Preferences prefs;
    prefs.begin(LILKA_SPI_NVS_NAMESPACE, false);
    uint32_t sdFrequency = sdFrequencies[index];
    prefs.putUInt(LILKA_SPI_NVS_SD_FREQUENCY_KEY, sdFrequency);
    prefs.end();
    NVS_UNLOCK;

    alert("", K_S_CHANGE_ON_NEXT_BOOT);
}

void LauncherApp::calibrateBatteryFullLevel() {
    String description =
        StringFormat(K_S_LAUNCHER_BATTERY_SET_FULL_CONFIRM, String(lilka::battery.readRawVoltage(), 2).c_str());
    if (!confirm(K_S_LAUNCHER_BATTERY_SET_FULL, description)) {
        return;
    }

    if (!lilka::battery.calibrateFullLevel()) {
        alert(K_S_LAUNCHER_BATTERY_SET_FULL, K_S_LAUNCHER_BATTERY_SET_FULL_ERROR);
    }
}

void LauncherApp::resetBatteryFullLevelCalibration() {
    if (confirm(K_S_LAUNCHER_BATTERY_RESET_FULL, K_S_LAUNCHER_BATTERY_RESET_FULL_CONFIRM)) {
        lilka::battery.resetFullLevelCalibration();
    }
}

void LauncherApp::wifiToggle() {
    NetworkService* networkService = static_cast<NetworkService*>(ksystem.services["network"]);
    networkService->setEnabled(!networkService->getEnabled());
}

void LauncherApp::setMDNSHostname() {
    MDNSService* mdnsService = static_cast<MDNSService*>(ksystem.services["mdns"]);

    lilka::InputDialog inputDialog(K_S_LAUNCHER_MDNS_ENTER_HOSTNAME);
    inputDialog.setValue(mdnsService->getHostname());

    while (!inputDialog.isFinished()) {
        inputDialog.update();
        inputDialog.draw(canvas);
        queueDraw();
    }

    String newHostname = inputDialog.getValue();
    if (!newHostname.isEmpty()) {
        mdnsService->setHostname(newHostname);
    }
}

String LauncherApp::getTimezoneLabel(const String& timezone) {
    if (timezone == CLOCK_TIMEZONE_UTC) {
        return K_S_LAUNCHER_TIMEZONE_UTC;
    }
    if (timezone == CLOCK_TIMEZONE_KYIV) {
        return K_S_LAUNCHER_TIMEZONE_KYIV;
    }
    if (timezone == CLOCK_TIMEZONE_TORONTO) {
        return K_S_LAUNCHER_TIMEZONE_TORONTO;
    }

    int16_t offsetMinutes;
    if (utcOffsetFromTimezone(timezone, offsetMinutes)) {
        String label = "UTC";
        label += formatUtcOffset(offsetMinutes);
        return label;
    }
    return K_S_LAUNCHER_TIMEZONE_CUSTOM;
}

void LauncherApp::setTimezone() {
    ClockService* clockService = static_cast<ClockService*>(ksystem.services["clock"]);
    String currentTimezone = clockService->getTimezone();

    lilka::Menu menu(K_S_LAUNCHER_TIMEZONE);
    menu.addActivationButton(K_BTN_BACK);
    menu.addItem(
        K_S_LAUNCHER_TIMEZONE_KYIV,
        nullptr,
        lilka::colors::White,
        currentTimezone == CLOCK_TIMEZONE_KYIV ? "[x]" : "[ ]"
    );
    menu.addItem(
        K_S_LAUNCHER_TIMEZONE_TORONTO,
        nullptr,
        lilka::colors::White,
        currentTimezone == CLOCK_TIMEZONE_TORONTO ? "[x]" : "[ ]"
    );
    menu.addItem(
        K_S_LAUNCHER_TIMEZONE_UTC, nullptr, lilka::colors::White, currentTimezone == CLOCK_TIMEZONE_UTC ? "[x]" : "[ ]"
    );
    bool isCustom = currentTimezone != CLOCK_TIMEZONE_KYIV && currentTimezone != CLOCK_TIMEZONE_TORONTO &&
                    currentTimezone != CLOCK_TIMEZONE_UTC;
    menu.addItem(K_S_LAUNCHER_TIMEZONE_CUSTOM, nullptr, lilka::colors::White, isCustom ? "[x]" : "[ ]");
    int16_t cursor = 3;
    if (currentTimezone == CLOCK_TIMEZONE_KYIV) cursor = 0;
    else if (currentTimezone == CLOCK_TIMEZONE_TORONTO) cursor = 1;
    else if (currentTimezone == CLOCK_TIMEZONE_UTC) cursor = 2;
    menu.setCursor(cursor);

    while (!menu.isFinished()) {
        menu.update();
        menu.draw(canvas);
        queueDraw();
    }

    if (menu.getButton() == K_BTN_BACK) {
        return;
    }

    switch (menu.getCursor()) {
        case 0:
            clockService->setTimezone(CLOCK_TIMEZONE_KYIV);
            break;
        case 1:
            clockService->setTimezone(CLOCK_TIMEZONE_TORONTO);
            break;
        case 2:
            clockService->setTimezone(CLOCK_TIMEZONE_UTC);
            break;
        case 3: {
            setCustomTimezone();
            break;
        }
        default:
            break;
    }
}

void LauncherApp::setCustomTimezone() {
    ClockService* clockService = static_cast<ClockService*>(ksystem.services["clock"]);
    String currentTimezone = clockService->getTimezone();
    int16_t currentOffsetMinutes = 0;
    bool isFixedOffset =
        currentTimezone != CLOCK_TIMEZONE_UTC && utcOffsetFromTimezone(currentTimezone, currentOffsetMinutes);
    bool isAdvanced = currentTimezone != CLOCK_TIMEZONE_KYIV && currentTimezone != CLOCK_TIMEZONE_TORONTO &&
                      currentTimezone != CLOCK_TIMEZONE_UTC && !isFixedOffset;

    lilka::Menu menu(K_S_LAUNCHER_TIMEZONE_CUSTOM);
    menu.addActivationButton(K_BTN_BACK);
    menu.addItem(K_S_LAUNCHER_TIMEZONE_FIXED_OFFSET, nullptr, lilka::colors::White, isFixedOffset ? "[x]" : "[ ]");
    menu.addItem(K_S_LAUNCHER_TIMEZONE_ADVANCED, nullptr, lilka::colors::White, isAdvanced ? "[x]" : "[ ]");
    menu.setCursor(isAdvanced ? 1 : 0);

    while (!menu.isFinished()) {
        menu.update();
        menu.draw(canvas);
        queueDraw();
    }

    if (menu.getButton() == K_BTN_BACK) {
        return;
    }

    if (menu.getCursor() == 0) {
        setFixedUtcOffset();
        return;
    }

    String advancedTimezone = input(K_S_LAUNCHER_TIMEZONE_CUSTOM_INPUT, currentTimezone);
    if (!advancedTimezone.isEmpty()) {
        clockService->setTimezone(advancedTimezone);
    }
}

void LauncherApp::setFixedUtcOffset() {
    constexpr int16_t MIN_OFFSET_MINUTES = -12 * 60;
    constexpr int16_t MAX_OFFSET_MINUTES = 14 * 60;
    constexpr int16_t OFFSET_STEP_MINUTES = 30;

    ClockService* clockService = static_cast<ClockService*>(ksystem.services["clock"]);
    String originalTimezone = clockService->getTimezone();
    int16_t offsetMinutes = 0;
    if (!utcOffsetFromTimezone(originalTimezone, offsetMinutes)) {
        if (originalTimezone == CLOCK_TIMEZONE_KYIV) {
            offsetMinutes = 2 * 60;
        } else if (originalTimezone == CLOCK_TIMEZONE_TORONTO) {
            offsetMinutes = -5 * 60;
        }
    }

    clockService->setTimezone(timezoneFromUtcOffset(offsetMinutes), false);
    int16_t cursor = 2;

    while (true) {
        lilka::Menu menu(K_S_LAUNCHER_TIMEZONE_FIXED_OFFSET);
        menu.addActivationButton(K_BTN_BACK);
        struct tm localTime = clockService->getTime();
        String offsetLabel = "UTC";
        offsetLabel += formatUtcOffset(offsetMinutes);
        menu.addItem(
            K_S_LAUNCHER_TIMEZONE_OFFSET_CURRENT,
            nullptr,
            lilka::colors::White,
            StringFormat("%02d:%02d", localTime.tm_hour, localTime.tm_min)
        );
        menu.addItem(K_S_LAUNCHER_TIMEZONE_OFFSET_DECREASE, nullptr, lilka::colors::White, "-30 min");
        menu.addItem(K_S_LAUNCHER_TIMEZONE_FIXED_OFFSET, nullptr, lilka::colors::White, offsetLabel);
        menu.addItem(K_S_LAUNCHER_TIMEZONE_OFFSET_INCREASE, nullptr, lilka::colors::White, "+30 min");
        menu.addItem(K_S_LAUNCHER_TIMEZONE_OFFSET_SAVE);
        menu.addItem(K_S_LAUNCHER_TIMEZONE_OFFSET_CANCEL);
        menu.setCursor(cursor);

        while (!menu.isFinished()) {
            menu.update();
            menu.draw(canvas);
            queueDraw();
        }

        if (menu.getButton() == K_BTN_BACK) {
            clockService->setTimezone(originalTimezone, false);
            return;
        }

        cursor = menu.getCursor();
        switch (cursor) {
            case 1:
                offsetMinutes -= OFFSET_STEP_MINUTES;
                if (offsetMinutes < MIN_OFFSET_MINUTES) offsetMinutes = MIN_OFFSET_MINUTES;
                clockService->setTimezone(timezoneFromUtcOffset(offsetMinutes), false);
                break;
            case 3:
                offsetMinutes += OFFSET_STEP_MINUTES;
                if (offsetMinutes > MAX_OFFSET_MINUTES) offsetMinutes = MAX_OFFSET_MINUTES;
                clockService->setTimezone(timezoneFromUtcOffset(offsetMinutes), false);
                break;
            case 4:
                clockService->setTimezone(timezoneFromUtcOffset(offsetMinutes));
                return;
            case 5:
                clockService->setTimezone(originalTimezone, false);
                return;
            default:
                break;
        }
    }
}

void LauncherApp::wifiManager() {
    NetworkService* networkService = static_cast<NetworkService*>(ksystem.services["network"]);
    if (!networkService->getEnabled()) {
        alert(K_S_ERROR, K_S_LAUNCHER_ENABLE_WIFI_FIRST);
        return;
    }
    ksystem.apps.spawn(new WiFiConfigApp());
}
void LauncherApp::about() {
    static int clickCount = 0;
    clickCount++;

    if (clickCount >= 5) {
        clickCount = 0;
        showEasterEgg();
        return;
    }

    alert(
        K_S_OS_NAME,
        StringFormat(
            K_S_LAUNCHER_ABOUT_FMT,
            ksystem.getVersionStr().c_str(),
            lilka::sdk.getVersionStr().c_str(),
            esp_get_idf_version()
        )
    );
}
void LauncherApp::info() {
    NetworkService* networkService = static_cast<NetworkService*>(ksystem.services["network"]);
    alert(
        K_S_LAUNCHER_DEVICE_INFO,
        StringFormat(
            K_S_LAUNCHER_DEVICE_INFO_FMT,
            ARDUINO_BOARD,
            ESP.getChipModel(),
            ESP.getChipRevision(),
            ESP.getCpuFreqMHz(),
            ESP.getChipCores(),
            networkService->getipAddr().c_str()
        )
    );
}
void LauncherApp::showEasterEgg() {
    const char* url = "https://youtu.be/dQw4w9WgXcQ";

    // Use a static variable to store QR code for display callback
    static lilka::Canvas* qrCanvas = nullptr;
    static int qrScale = 0;
    static int qrOffsetX = 0;
    static int qrOffsetY = 0;

    qrCanvas = canvas;

    auto displayFunc = [](esp_qrcode_handle_t qrcode) {
        int size = esp_qrcode_get_size(qrcode);
        qrScale = min((int)qrCanvas->width(), (int)(qrCanvas->height() - 40)) / size;
        qrOffsetX = (qrCanvas->width() - size * qrScale) / 2;
        qrOffsetY = 30 + (qrCanvas->height() - 40 - size * qrScale) / 2;

        qrCanvas->fillScreen(lilka::colors::White);
        qrCanvas->setTextColor(lilka::colors::Black);
        qrCanvas->setFont(FONT_9x15);
        qrCanvas->setTextBound(0, 0, qrCanvas->width(), qrCanvas->height());
        qrCanvas->setCursor(qrCanvas->width() / 2 - 40, 20);
        qrCanvas->print(K_S_LAUNCHER_EASTER_EGG);

        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                if (esp_qrcode_get_module(qrcode, x, y)) {
                    qrCanvas->fillRect(
                        qrOffsetX + x * qrScale, qrOffsetY + y * qrScale, qrScale, qrScale, lilka::colors::Black
                    );
                }
            }
        }
    };

    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    cfg.display_func = displayFunc;
    cfg.max_qrcode_version = 10;
    cfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;

    if (esp_qrcode_generate(&cfg, url) != ESP_OK) {
        alert(K_S_ERROR, "Failed to generate QR code");
        return;
    }

    queueDraw();

    while (true) {
        lilka::State state = lilka::controller.getState();
        if (state.a.justPressed || state.b.justPressed || state.start.justPressed) {
            break;
        }
        taskYIELD();
    }
}
void LauncherApp::partitions() {
    // TODO : support more than 16 partitions
    String names[16];
    int partitionCount = lilka::sys.get_partition_labels(names);

    ITEM_LIST partitionsMenu;
    for (int i = 0; i < partitionCount; i++) {
        String partition = names[i];
        partitionsMenu.push_back(ITEM::MENU(names[i].c_str(), [this, partition]() {
            alert(
                partition,
                StringFormat(
                    K_S_LAUNCHER_PARTITION_FMT,
                    String(lilka::sys.get_partition_address(partition.c_str()), HEX).c_str(),
                    String(lilka::sys.get_partition_size(partition.c_str()), HEX).c_str()
                )
            );
        }));
    }
    showMenu(K_S_PARTITION_TABLE, partitionsMenu);
}
void LauncherApp::formatSD() {
    if (!confirm(K_S_LAUNCHER_FORMAT, K_S_LAUNCHER_FORMAT_DISCLAIMER_ALERT)) return;

    lilka::ProgressDialog dialog(K_S_LAUNCHER_FORMAT, K_S_LAUNCHER_PLEASE_STANDBY);
    dialog.draw(canvas);
    queueDraw();
    if (!lilka::fileutils.createSDPartTable()) {
        alert(K_S_ERROR, K_S_LAUNCHER_FORMAT_ERROR_ALERT);
        esp_restart();
    }
    if (!lilka::fileutils.formatSD()) {
        this->alert(K_S_ERROR, K_S_LAUNCHER_FORMAT_ERROR_ALERT);
        esp_restart();
    }
    this->alert(K_S_LAUNCHER_FORMAT, K_S_LAUNCHER_FORMAT_SUCCESS_ALLERT);
    esp_restart();
}

void LauncherApp::factoryReset() {
    if (!confirm(K_S_LAUNCHER_FACTORY_RESET, K_S_LAUNCHER_FACTORY_RESET_DISCLAIMER_ALERT)) return;

    lilka::ProgressDialog dialog(K_S_LAUNCHER_FACTORY_RESET, K_S_LAUNCHER_PLEASE_STANDBY);
    dialog.draw(canvas);
    queueDraw();

    nvs_flash_erase();

    this->alert(K_S_LAUNCHER_FACTORY_RESET, K_S_LAUNCHER_FACTORY_RESET_SUCCESS_ALERT);
    esp_restart();
}
