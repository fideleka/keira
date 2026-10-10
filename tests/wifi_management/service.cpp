#include "service_hal.h"
#define private public
#include "services/network/network.h"
#undef private
#include "services/network/credentials.h"
#include <cstdio>
std::map<std::string, std::vector<uint8_t>> Preferences::data;
std::string Preferences::failWrite, Preferences::failRemove;
unsigned Preferences::writes = 0;
bool Preferences::failBegin = false;
WifiHAL WiFi;
lilka::SerialHAL lilka::serial;
SystemHAL ksystem;
int main() {
    NetworkService service;
    try {
        service.run();
    } catch (StopService&) {
    }
    const String emoji = "🦄🌈🎉";
    service.connect(emoji, "password123");
    assert(WiFi.name == emoji && WiFi.password == "password123");
    WiFi.event(ARDUINO_EVENT_WIFI_STA_CONNECTED);
    assert(service.getnetworkState() == NETWORK_STATE_CONNECTING);
    assert(Preferences::writes == 0);
    assert(!service.saveConnectedNetwork(emoji));
    WiFi.event(ARDUINO_EVENT_WIFI_STA_GOT_IP);
    assert(service.getnetworkState() == NETWORK_STATE_ONLINE);
    assert(Preferences::writes == 0); // Event callback never writes NVS.
    service.persistCredentials();
    assert(service.knownNetworks().size() == 1);
    String password;
    assert(service.getCredentials(emoji, password) && password == "password123");
    const unsigned written = Preferences::writes;
    service.persistCredentials();
    assert(Preferences::writes == written);
    WiFi.event(ARDUINO_EVENT_WIFI_STA_GOT_IP); // Pending save raced with Forget.
    assert(service.forgetNetwork(emoji));
    service.persistCredentials();
    assert(!service.getCredentials(emoji, password));
    assert(service.knownNetworks().empty() && !WiFi.reconnect);
    assert(!service.connect(emoji));
    service.connect("Open", "");
    WiFi.event(ARDUINO_EVENT_WIFI_STA_GOT_IP);
    assert(service.saveConnectedNetwork("Open"));
    assert(service.connect("Open"));
    assert(WiFi.password.isEmpty());
    const unsigned disconnects = WiFi.disconnects;
    service.disconnectNetwork();
    assert(WiFi.disconnects == disconnects + 1);
    service.autoConnect();
    assert(WiFi.name == "Open" && WiFi.password.isEmpty());
    // A queued event after cancel/Forget cannot recreate the entry.
    service.disconnectNetwork();
    WiFi.event(ARDUINO_EVENT_WIFI_STA_GOT_IP);
    service.persistCredentials();
    assert(service.knownNetworks().size() == 1);
    service.connect("Never connected", "password123");
    service.disconnectNetwork();
    service.persistCredentials();
    assert(!service.getCredentials("Never connected", password));
    Preferences::failBegin = true;
    password = "stale secret";
    assert(!service.getCredentials("Open", password) && password.isEmpty());
    assert(service.knownNetworks().empty());
    assert(!service.forgetNetwork("Open"));
    Preferences::failBegin = false;
    assert(service.getCredentials("Open", password));
    service.connect("Backup", "backup123");
    WiFi.event(ARDUINO_EVENT_WIFI_STA_GOT_IP);
    assert(service.saveConnectedNetwork("Backup"));
    service.connect("Home", "home1234");
    WiFi.event(ARDUINO_EVENT_WIFI_STA_GOT_IP);
    assert(service.saveConnectedNetwork("Home"));
    WiFi.state = WL_DISCONNECTED;
    WiFi.scanStart = 2;
    WiFi.visible = {{"Open", -70}, {"Backup", -40}};
    service.autoConnect();
    assert(WiFi.name == "Home");
    hostMillis() = 10000;
    service.serviceAutomaticConnection();
    assert(WiFi.name == "Backup");
    WiFi.event(ARDUINO_EVENT_WIFI_STA_GOT_IP);
    service.serviceAutomaticConnection();
    service.persistCredentials();
    Preferences saved;
    assert(saved.begin("network", true));
    assert(saved.getString("last_ssid", "") == "Backup");
    const unsigned afterFallback = Preferences::writes;
    const unsigned attempts = WiFi.attempts.size();
    service.serviceAutomaticConnection();
    service.persistCredentials();
    assert(WiFi.attempts.size() == attempts && Preferences::writes == afterFallback);
    WiFi.state = WL_DISCONNECTED;
    service.serviceAutomaticConnection();
    hostMillis() += 29999;
    service.serviceAutomaticConnection();
    assert(WiFi.attempts.size() == attempts);
    ++hostMillis();
    service.serviceAutomaticConnection();
    assert(WiFi.attempts.size() == attempts + 1 && WiFi.name == "Backup");
    WiFi.event(ARDUINO_EVENT_WIFI_STA_GOT_IP);
    service.serviceAutomaticConnection();
    service.persistCredentials();
    service.pauseAutomaticConnection(true);
    assert(WiFi.state == WL_CONNECTED); // UI never disrupts a working connection.
    service.pauseAutomaticConnection(false);
    WiFi.state = WL_DISCONNECTED;
    service.serviceAutomaticConnection();
    assert(WiFi.name == "Backup");
    service.disconnectNetwork();
    hostMillis() += 60000;
    service.serviceAutomaticConnection();
    assert(WiFi.state == WL_DISCONNECTED); // Explicit Disconnect is respected.
    // Forget invalidates the selector snapshot, not only the password mirror.
    service.autoConnect();
    assert(service.forgetNetwork("Backup"));
    service.serviceAutomaticConnection();
    assert(WiFi.name != "Backup" || WiFi.state == WL_DISCONNECTED);
    puts(
        "Production NetworkService: save after IP, no event NVS, pending-save/Forget, UTF-8, open and cancellation PASS"
    );
}
