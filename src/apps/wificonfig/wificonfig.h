#pragma once
#include "keira/keira.h"
#include "keira/app.h"
class NetworkService;

class WiFiConfigApp : public App {
public:
    WiFiConfigApp();

private:
    String getEncryptionTypeStr(uint8_t encryptionType);
    void run() override;
    void showAlert(const String& title, const String& message);
    void connectNetwork(NetworkService& service, const String& ssid, bool editPassword, bool open);
    void showKnownNetworks(NetworkService& service);
    void scanNetworks(NetworkService& service);
};
