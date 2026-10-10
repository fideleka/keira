// TODO: Add enable/disable methods instead of deallocating WiFi from apps like LilTracker
// TODO: Use the mutex, Luke!

#include <Preferences.h>
#include <esp_wifi.h>
#include "keira/ksystem.h"
#include "network.h"
#include "credentials.h"

// Macro magic used to convert decimal constant to char[] constant
#define STRX(x)               #x
#define STR(x)                STRX(x)
#define LILKA_HOSTNAME_PREFIX "LilkaV"

// EEPROM preferences used:
// - keira.last_ssid - last connected SSID
// - keira.[SSID_hash]_pw - password of known network with a given SSID

NetworkService::NetworkService() : Service("network") {
}

void NetworkService::run() {
    // Loading settings from NVS

    bool enabled = getEnabled();
    WiFi.persistent(false);
    NVS_LOCK;
    Preferences prefs;
    prefs.begin(getName(), true);
    // Set transmit power
    wifi_power_t txPower =
        prefs.isKey("txPower") ? static_cast<wifi_power_t>(prefs.getInt("txPower")) : WIFI_POWER_19_5dBm;
    WiFi.setTxPower(txPower);

    prefs.end();
    NVS_UNLOCK;
    // Setting Lilka hostname
    // Take LILKA_HOSTNAME_PREFIX as a prefix and append MAC to it
    // This value should be random enough to avoid potential conflicts
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char cstrMac[50];
    sprintf(cstrMac, LILKA_HOSTNAME_PREFIX STR(LILKA_VERSION) "_%02X%02X%02X", mac[3], mac[4], mac[5]);
    WiFi.setHostname(cstrMac);

    // Handling events
    WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) {
        switch (event) {
            case ARDUINO_EVENT_WIFI_STA_START: {
                lilka::serial.log("NetworkService: got event: connecting to WiFi");
                KMTX_LOCK(mtxNetwork);
                const bool requested = requestedSSID.length() > 0;
                KMTX_UNLOCK(mtxNetwork);
                setnetworkState(requested ? NETWORK_STATE_CONNECTING : NETWORK_STATE_OFFLINE);
                break;
            }
            case ARDUINO_EVENT_WIFI_STA_CONNECTED: {
                lilka::serial.log("NetworkService: got event: connected to WiFi");
                setnetworkState(NETWORK_STATE_CONNECTING);
                break;
            }
            case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
                lilka::serial.log(
                    "NetworkService: got event: disconnected from WiFi, reason: %d", info.wifi_sta_disconnected.reason
                );
                setnetworkState(NETWORK_STATE_OFFLINE);
                setipAddr("");
                setdisconnectReason(info.wifi_sta_disconnected.reason);
                break;
            }
            case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            case ARDUINO_EVENT_WIFI_STA_GOT_IP6: {
                IPAddress ip = WiFi.localIP();
                setipAddr(ip.toString());
                lilka::serial.log("NetworkService: got event: got IP address: %s", ip.toString().c_str());
                setnetworkState(NETWORK_STATE_ONLINE);
                const String connectedSSID = WiFi.SSID();
                KMTX_LOCK(mtxNetwork);
                if (connectedSSID == requestedSSID) {
                    pendingSSID = connectedSSID;
                    pendingPassword = lastPassword;
                    credentialSavePending = true;
                    automaticConnection = true;
                }
                KMTX_UNLOCK(mtxNetwork);
                break;
            }
            case ARDUINO_EVENT_WIFI_STA_LOST_IP: {
                lilka::serial.log("NetworkService: got event: lost IP address");
                setipAddr("");
                setnetworkState(NETWORK_STATE_OFFLINE);
                break;
            }
            case ARDUINO_EVENT_WIFI_STA_STOP: {
                lilka::serial.log("NetworkService: got event: WiFi stopped");
                setnetworkState(NETWORK_STATE_DISABLED);
                break;
            }
            default:
                break;
        }
    });

    if (enabled) {
        lilka::serial.log("NetworkService: WiFi is enabled, starting auto connection");
        setnetworkState(NETWORK_STATE_OFFLINE);
        WiFi.mode(WIFI_STA);
        autoConnect();
    } else {
        lilka::serial.log("NetworkService: WiFi is disabled, not starting auto connection");
        setnetworkState(NETWORK_STATE_DISABLED);
        WiFi.disconnect(true, true);
        WiFi.mode(WIFI_OFF);
    }

    while (1) {
        // Check if WiFi is deallocated
        wifi_mode_t mode;
        if (esp_wifi_get_mode(&mode) == ESP_ERR_WIFI_NOT_INIT) {
            // WiFi was deallocated
            // TODO: This is a crutch to avoid using WiFi after deallocation by apps (e. g. LilTracker). /AD
            // lilka::serial.log("NetworkService: WiFi deallocated");
        } else if (WiFi.status() == WL_DISCONNECTED) {
            // WiFi is disconnected
            // lilka::serial.log("NetworkService: WiFi disconnected");
        } else {
            const int8_t rssi = WiFi.RSSI();
            if (rssi == 0) {
                setsignalStrength(0);
            } else {
                const int8_t excellent = -50;
                const int8_t good = -70;
                const int8_t fair = -80;

                if (rssi >= excellent) {
                    setsignalStrength(3);
                } else if (rssi >= good) {
                    setsignalStrength(2);
                } else if (rssi >= fair) {
                    setsignalStrength(1);
                } else {
                    setsignalStrength(0);
                }
            }
        }

        bool stateChanged = getEnabled() != enabled;
        if (stateChanged) {
            enabled = !enabled; // toggle to new state
            if (enabled) {
                setnetworkState(NETWORK_STATE_OFFLINE);
                WiFi.mode(WIFI_STA);
                autoConnect();
            } else {
                disconnectNetwork();
                WiFi.disconnect(true, true);
                WiFi.mode(WIFI_OFF);
            }
        }

        serviceAutomaticConnection();
        persistCredentials();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void NetworkService::autoConnect(bool requested) {
    NVS_LOCK;
    KMTX_LOCK(mtxNetwork);
    if (!requested && (!automaticConnection || automaticPaused)) {
        KMTX_UNLOCK(mtxNetwork);
        NVS_UNLOCK;
        return;
    }
    lilka::wifiConnection.cancel(false);
    requestedSSID = lastPassword = "";
    credentialSavePending = false;
    automaticConnection = true;
    retryPending = false;
    Preferences prefs;
    bool loaded = false;
    if (prefs.begin(getName(), true)) {
        loaded = lilka::wifiConnection.load(prefs);
        prefs.end();
    }
    NVS_UNLOCK;
    if (loaded && !automaticPaused) {
        lilka::wifiConnection.start(millis());
    } else {
        retryPending = true;
        retryStarted = millis();
    }
    KMTX_UNLOCK(mtxNetwork);
}

void NetworkService::serviceAutomaticConnection() {
    KMTX_LOCK(mtxNetwork);
    if (!automaticConnection || automaticPaused) {
        KMTX_UNLOCK(mtxNetwork);
        return;
    }
    const uint32_t now = millis();
    const auto state = lilka::wifiConnection.state();
    if (state == lilka::WiFiConnection::State::Idle || retryPending) {
        const bool retry = WiFi.status() != WL_CONNECTED && (!retryPending || uint32_t(now - retryStarted) >= 30000);
        KMTX_UNLOCK(mtxNetwork);
        if (retry) {
            autoConnect(false);
        }
        return;
    }
    const auto result = lilka::wifiConnection.poll(now);
    if (result == lilka::WiFiConnection::State::Connected) {
        // Persist once after successful fallback, never from an event callback.
        if (requestedSSID != lilka::wifiConnection.ssid() && lilka::wifiConnection.ssid().length()) {
            requestedSSID = pendingSSID = lilka::wifiConnection.ssid();
            lastPassword = pendingPassword = lilka::wifiConnection.password();
            credentialSavePending = true;
        }
        networkState = NETWORK_STATE_ONLINE;
    } else if (
        result == lilka::WiFiConnection::State::Failed || result == lilka::WiFiConnection::State::NoCredentials
    ) {
        requestedSSID = lastPassword = "";
        retryPending = true;
        retryStarted = now;
        networkState = NETWORK_STATE_OFFLINE;
    } else {
        networkState = NETWORK_STATE_CONNECTING;
    }
    KMTX_UNLOCK(mtxNetwork);
}

void NetworkService::pauseAutomaticConnection(bool paused) {
    KMTX_LOCK(mtxNetwork);
    automaticPaused = paused;
    if (paused) {
        lilka::wifiConnection.cancel(false);
        retryPending = false;
    }
    KMTX_UNLOCK(mtxNetwork);
}

bool NetworkService::connect(String ssid) {
    String password;
    if (!getCredentials(ssid, password)) {
        return false;
    }
    connect(ssid, password);
    return true;
}

void NetworkService::connect(String ssid, String password) {
    KMTX_LOCK(mtxNetwork);
    automaticConnection = false;
    retryPending = false;
    lilka::wifiConnection.cancel(false);
    requestedSSID = ssid;
    lastPassword = password;
    credentialSavePending = false;
    KMTX_UNLOCK(mtxNetwork);
    setnetworkState(NETWORK_STATE_CONNECTING);
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    WiFi.setAutoReconnect(true);
    WiFi.disconnect();
    WiFi.begin(ssid.c_str(), password.c_str());
}

bool NetworkService::getCredentials(const String& ssid, String& password) {
    password = "";
    NVS_LOCK;
    Preferences prefs;
    bool found = false;
    if (prefs.begin(getName(), true)) {
        found = NetworkCredentials::read(prefs, ssid, password);
        prefs.end();
    }
    NVS_UNLOCK;
    return found;
}

String NetworkService::getPassword(String ssid) {
    String password;
    getCredentials(ssid, password);
    return password;
}

bool NetworkService::learnKnownNetwork(const String& ssid) {
    NVS_LOCK;
    Preferences prefs;
    String password;
    bool learned = false;
    if (prefs.begin(getName(), false)) {
        learned =
            NetworkCredentials::read(prefs, ssid, password) && NetworkCredentials::save(prefs, ssid, password, false);
        prefs.end();
    }
    NVS_UNLOCK;
    return learned;
}

std::vector<String> NetworkService::knownNetworks() {
    NVS_LOCK;
    Preferences prefs;
    std::vector<String> names;
    if (prefs.begin(getName(), true)) {
        names = NetworkCredentials::list(prefs);
        prefs.end();
    }
    NVS_UNLOCK;
    return names;
}

void NetworkService::persistCredentials() {
    // NVS first: Forget and a deferred save cannot interleave or resurrect entries.
    NVS_LOCK;
    KMTX_LOCK(mtxNetwork);
    if (!credentialSavePending) {
        KMTX_UNLOCK(mtxNetwork);
        NVS_UNLOCK;
        return;
    }
    const String ssid = pendingSSID;
    const String password = pendingPassword;
    credentialSavePending = false;
    pendingPassword = "";
    KMTX_UNLOCK(mtxNetwork);
    Preferences prefs;
    bool saved = false;
    if (prefs.begin(getName(), false)) {
        saved = NetworkCredentials::save(prefs, ssid, password);
        prefs.end();
    }
    NVS_UNLOCK;
    if (!saved) {
        lilka::serial.err("Could not save WiFi credentials");
    }
}

bool NetworkService::saveConnectedNetwork(const String& ssid) {
    NVS_LOCK;
    KMTX_LOCK(mtxNetwork);
    const bool matches = requestedSSID == ssid;
    const String password = lastPassword;
    KMTX_UNLOCK(mtxNetwork);
    Preferences prefs;
    bool saved = false;
    if (matches && getnetworkState() == NETWORK_STATE_ONLINE && WiFi.status() == WL_CONNECTED && WiFi.SSID() == ssid &&
        prefs.begin(getName(), false)) {
        saved = NetworkCredentials::save(prefs, ssid, password);
        prefs.end();
    }
    NVS_UNLOCK;
    return saved;
}

bool NetworkService::forgetNetwork(const String& ssid) {
    NVS_LOCK;
    KMTX_LOCK(mtxNetwork);
    // Invalidate the RAM snapshot too, including entries not currently selected.
    lilka::wifiConnection.cancel(false);
    retryPending = false;
    if (requestedSSID == ssid) {
        requestedSSID = "";
        lastPassword = "";
    }
    if (pendingSSID == ssid) {
        credentialSavePending = false;
        pendingSSID = pendingPassword = "";
    }
    KMTX_UNLOCK(mtxNetwork);
    Preferences prefs;
    bool forgotten = false;
    if (prefs.begin(getName(), false)) {
        forgotten = NetworkCredentials::forget(prefs, ssid);
        prefs.end();
    }
    NVS_UNLOCK;
    if (WiFi.SSID() == ssid) {
        WiFi.setAutoReconnect(false);
        WiFi.disconnect();
    }
    return forgotten;
}

void NetworkService::disconnectNetwork() {
    NVS_LOCK;
    KMTX_LOCK(mtxNetwork);
    automaticConnection = false;
    retryPending = false;
    lilka::wifiConnection.cancel(true);
    requestedSSID = "";
    lastPassword = pendingPassword = "";
    credentialSavePending = false;
    KMTX_UNLOCK(mtxNetwork);
    NVS_UNLOCK;
    setnetworkState(NETWORK_STATE_OFFLINE);
    setipAddr("");
}
