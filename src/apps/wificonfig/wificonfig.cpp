#include "keira/ksystem.h"
#include "apps/wificonfig/wificonfig.h"
// Libraries:
#include <WiFi.h>
#include <esp_wifi.h>
#include "keira/keira.h"
// Services:
#include "services/network/network.h"
#include "services/network/credentials.h"
// Icons:
#include "apps/icons/wifi_0.h"
#include "apps/icons/wifi_1.h"
#include "apps/icons/wifi_2.h"
#include "apps/icons/wifi_3.h"
// Utils:
#include "keira/utils/string.h"

WiFiConfigApp::WiFiConfigApp() : App("WiFi") {
}
String WiFiConfigApp::getEncryptionTypeStr(uint8_t encryptionType) {
    switch (encryptionType) {
        case WIFI_AUTH_OPEN:
            return "Open";
        case WIFI_AUTH_WEP:
            return "WEP";
        case WIFI_AUTH_WPA_PSK:
            return "WPA PSK";
        case WIFI_AUTH_WPA2_PSK:
            return "WPA2 PSK";
        case WIFI_AUTH_WPA_WPA2_PSK:
            return "WPA WPA2 PSK";
        case WIFI_AUTH_WPA2_ENTERPRISE:
            return "ENTERPRISE";
        case WIFI_AUTH_WPA3_PSK:
            return "WPA3 PSK";
        case WIFI_AUTH_WPA2_WPA3_PSK:
            return "WPA2 WPA3 PSK";
        case WIFI_AUTH_WAPI_PSK:
            return "WAPI PSK";
        default:
            return "Unknown";
    }
}

void WiFiConfigApp::showAlert(const String& title, const String& message) {
    lilka::Alert alert(title, message);
    while (!alert.isFinished()) {
        alert.update();
        alert.draw(canvas);
        queueDraw();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void WiFiConfigApp::connectNetwork(NetworkService& service, const String& ssid, bool editPassword, bool open) {
    String password;
    const bool known = service.getCredentials(ssid, password);
    if (editPassword || (!known && !open)) {
        password = input(K_S_WIFI_CONFIG_ENTER_PASSWORD, "", true);
    } else if (!known) {
        lilka::Menu confirmation(K_S_ATTENTION);
        confirmation.addItem(K_S_WIFI_CONFIG_CONNECT_OPEN);
        confirmation.addItem(K_S_MENU_BACK);
        confirmation.setCursor(1);
        confirmation.addActivationButton(K_BTN_BACK);
        while (!confirmation.isFinished()) {
            confirmation.update();
            confirmation.draw(canvas);
            queueDraw();
        }
        if (confirmation.getButton() == K_BTN_BACK || confirmation.getCursor() != 0) {
            return;
        }
    }
    service.connect(ssid, password);
    password = "";
    const uint32_t started = millis();
    bool cancelled = false;
    while (uint32_t(millis() - started) < 15000 &&
           (WiFi.status() != WL_CONNECTED || service.getnetworkState() != NETWORK_STATE_ONLINE)) {
        const auto state = lilka::controller.getState();
        if (state.b.justPressed) {
            cancelled = true;
            break;
        }
        canvas->fillScreen(lilka::colors::Black);
        canvas->setCursor(8, 24);
        canvas->println(K_S_WIFI_CONFIG_CONNECTING);
        canvas->println(NetworkCredentials::displayName(ssid));
        canvas->println(K_S_WIFI_CONFIG_CANCEL);
        queueDraw();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    const bool connected = !cancelled && WiFi.status() == WL_CONNECTED && WiFi.SSID() == ssid &&
                           service.getnetworkState() == NETWORK_STATE_ONLINE;
    if (!connected) {
        service.disconnectNetwork();
        if (!cancelled) {
            showAlert(
                K_S_ERROR,
                StringFormat(K_S_WIFI_CONFIG_CANT_CONNECT_TO_NETWORK_FMT, NetworkCredentials::displayName(ssid).c_str())
            );
        }
        return;
    }
    const bool saved = service.saveConnectedNetwork(ssid);
    showAlert(
        saved ? K_S_SUCCESS : K_S_ERROR,
        saved ? StringFormat(K_S_WIFI_CONFIG_CONNECTED_TO_NETWORK_FMT, NetworkCredentials::displayName(ssid).c_str())
              : String(K_S_WIFI_CONFIG_SAVE_FAILED)
    );
}

void WiFiConfigApp::showKnownNetworks(NetworkService& service) {
    for (;;) {
        const auto names = service.knownNetworks();
        lilka::Menu menu(K_S_WIFI_CONFIG_KNOWN);
        for (const String& name : names) {
            menu.addItem(NetworkCredentials::displayName(name));
        }
        menu.addItem(K_S_MENU_BACK);
        menu.addActivationButton(K_BTN_BACK);
        while (!menu.isFinished()) {
            menu.update();
            menu.draw(canvas);
            queueDraw();
        }
        const unsigned selected = menu.getCursor();
        if (menu.getButton() == K_BTN_BACK || selected >= names.size()) {
            return;
        }
        const String ssid = names[selected];
        lilka::Menu actions(NetworkCredentials::displayName(ssid));
        actions.addItem(K_S_WIFI_CONFIG_CONNECT);
        actions.addItem(K_S_WIFI_CONFIG_CHANGE_PASSWORD);
        actions.addItem(K_S_WIFI_CONFIG_FORGET);
        actions.addItem(K_S_MENU_BACK);
        actions.addActivationButton(K_BTN_BACK);
        while (!actions.isFinished()) {
            actions.update();
            actions.draw(canvas);
            queueDraw();
        }
        if (actions.getButton() == K_BTN_BACK) {
            continue;
        }
        const int action = actions.getCursor();
        if (action == 0 || action == 1) {
            connectNetwork(service, ssid, action == 1, false);
        } else if (action == 2) {
            lilka::Menu confirmation(K_S_WIFI_CONFIG_FORGET_CONFIRM);
            confirmation.addItem(K_S_WIFI_CONFIG_FORGET);
            confirmation.addItem(K_S_MENU_BACK);
            confirmation.setCursor(1);
            confirmation.addActivationButton(K_BTN_BACK);
            while (!confirmation.isFinished()) {
                confirmation.update();
                confirmation.draw(canvas);
                queueDraw();
            }
            if (confirmation.getButton() != K_BTN_BACK && confirmation.getCursor() == 0) {
                const bool forgotten = service.forgetNetwork(ssid);
                showAlert(
                    forgotten ? K_S_SUCCESS : K_S_ERROR,
                    forgotten ? K_S_WIFI_CONFIG_FORGOTTEN : K_S_WIFI_CONFIG_SAVE_FAILED
                );
            }
        }
    }
}

void WiFiConfigApp::scanNetworks(NetworkService& service) {
    if (!WiFi.mode(WIFI_STA)) {
        showAlert(K_S_ERROR, K_S_WIFI_CONFIG_SCAN_FAILED);
        return;
    }
    WiFi.scanDelete();
    int16_t count = WiFi.scanNetworks(true);
    const uint32_t started = millis();
    while (count == WIFI_SCAN_RUNNING && uint32_t(millis() - started) < 15000) {
        if (lilka::controller.getState().b.justPressed) {
            esp_wifi_scan_stop();
            WiFi.scanDelete();
            return;
        }
        canvas->fillScreen(lilka::colors::Black);
        canvas->setCursor(8, 24);
        canvas->println(K_S_WIFI_CONFIG_SCANING_NETWORKS);
        canvas->println(K_S_WIFI_CONFIG_CANCEL);
        queueDraw();
        vTaskDelay(pdMS_TO_TICKS(50));
        count = WiFi.scanComplete();
    }
    if (count < 0) {
        esp_wifi_scan_stop();
        WiFi.scanDelete();
        showAlert(K_S_ERROR, K_S_WIFI_CONFIG_SCAN_FAILED);
        return;
    }
    std::vector<String> names;
    std::vector<int16_t> indices;
    lilka::Menu menu(K_S_WIFI_CONFIG_NETWORKS);
    menu.addActivationButton(K_BTN_BACK);
    menu.addActivationButton(lilka::Button::C);
    for (int16_t i = 0; i < count && names.size() < 64; ++i) {
        const String ssid = WiFi.SSID(i);
        bool duplicate = false;
        for (const String& name : names) {
            duplicate |= name == ssid;
        }
        if (!ssid.length() || duplicate) {
            continue;
        }
        names.push_back(ssid);
        indices.push_back(i);
        const int32_t rssi = WiFi.RSSI(i);
        const unsigned strength = rssi >= -50 ? 3 : rssi >= -70 ? 2 : rssi >= -80 ? 1 : 0;
        menu_icon_t* icons[] = {&wifi_0_img, &wifi_1_img, &wifi_2_img, &wifi_3_img};
        String password;
        const bool known = service.getCredentials(ssid, password);
        password = "";
        if (known) {
            service.learnKnownNetwork(ssid);
        }
        menu.addItem(
            NetworkCredentials::displayName(ssid), icons[strength], known ? lilka::colors::Green : lilka::colors::White
        );
    }
    menu.addItem(K_S_MENU_BACK);
    for (;;) {
        while (!menu.isFinished()) {
            menu.update();
            menu.draw(canvas);
            queueDraw();
        }
        const unsigned selected = menu.getCursor();
        if (menu.getButton() == K_BTN_BACK || selected >= names.size()) {
            break;
        }
        const int16_t index = indices[selected];
        if (menu.getButton() == lilka::Button::C) {
            showAlert(
                NetworkCredentials::displayName(names[selected]),
                StringFormat(
                    K_S_WIFI_CONFIG_ABOUT_NETWORK_FMT,
                    WiFi.channel(index),
                    WiFi.RSSI(index),
                    WiFi.BSSIDstr(index).c_str(),
                    getEncryptionTypeStr(WiFi.encryptionType(index))
                )
            );
            continue;
        }
        connectNetwork(service, names[selected], false, WiFi.encryptionType(index) == WIFI_AUTH_OPEN);
        break;
    }
    WiFi.scanDelete();
}

void WiFiConfigApp::onStop() {
    // Forced task deletion does not unwind run()'s scope guard.
    auto* service = static_cast<NetworkService*>(ksystem.services["network"]);
    if (service) {
        service->pauseAutomaticConnection(false);
    }
}

void WiFiConfigApp::run() {
    auto* service = static_cast<NetworkService*>(ksystem.services["network"]);
    if (!service) {
        showAlert(K_S_ERROR, K_S_WIFI_CONFIG_SCAN_FAILED);
        return;
    }
    service->pauseAutomaticConnection(true);
    struct ResumeAutomaticConnection {
        NetworkService& service;
        ~ResumeAutomaticConnection() {
            service.pauseAutomaticConnection(false);
        }
    } resume{*service};
    for (;;) {
        lilka::Menu menu("WiFi");
        menu.addItem(K_S_WIFI_CONFIG_NETWORKS);
        menu.addItem(K_S_WIFI_CONFIG_KNOWN);
        menu.addItem(K_S_WIFI_CONFIG_DISCONNECT);
        menu.addItem(K_S_MENU_BACK);
        menu.addActivationButton(K_BTN_BACK);
        while (!menu.isFinished()) {
            menu.update();
            menu.draw(canvas);
            queueDraw();
        }
        if (menu.getButton() == K_BTN_BACK || menu.getCursor() == 3) {
            return;
        }
        switch (menu.getCursor()) {
            case 0:
                scanNetworks(*service);
                break;
            case 1:
                showKnownNetworks(*service);
                break;
            case 2:
                service->disconnectNetwork();
                break;
        }
    }
}
