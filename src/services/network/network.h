#pragma once

#include <WiFi.h>
#include "keira/service.h"

#include "keira/mutex.h"
#include <vector>

enum NetworkState {
    NETWORK_STATE_DISABLED,
    NETWORK_STATE_OFFLINE,
    NETWORK_STATE_CONNECTING,
    NETWORK_STATE_ONLINE,
};

class NetworkService : public Service {
public:
    NetworkService();
    bool connect(String ssid);
    void connect(String ssid, String password);
    // This one is special, cause takes stuff from NVS
    String getPassword(String ssid);
    bool getCredentials(const String& ssid, String& password);
    bool learnKnownNetwork(const String& ssid);
    std::vector<String> knownNetworks();
    bool forgetNetwork(const String& ssid);
    bool saveConnectedNetwork(const String& ssid);
    void disconnectNetwork();
    KMTX_GETER(NetworkState, networkState, mtxNetwork);
    KMTX_GETER(int8_t, signalStrength, mtxNetwork);
    KMTX_GETER(String, ipAddr, mtxNetwork);

private:
    // util
    void run() override;
    void autoConnect();
    void persistCredentials();

    KMTX_SETER(NetworkState, networkState, mtxNetwork);
    KMTX_SETER(int, disconnectReason, mtxNetwork);
    KMTX_SETER(int8_t, signalStrength, mtxNetwork);
    KMTX_SETER(String, lastPassword, mtxNetwork);
    KMTX_SETER(String, ipAddr, mtxNetwork);

    SemaphoreHandle_t mtxNetwork = xSemaphoreCreateMutex();

    NetworkState networkState = NETWORK_STATE_OFFLINE;
    int disconnectReason = 0;
    int8_t signalStrength = 0; // Value in range [0,3]
    String lastPassword = ""; // Credentials for the explicitly requested connection.
    String requestedSSID;
    String pendingSSID;
    String pendingPassword;
    bool credentialSavePending = false;
    String ipAddr = "";
};
