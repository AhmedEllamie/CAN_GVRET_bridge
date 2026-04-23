/*
 * CAN_GVRET_Bridge - GVRET-compatible CAN sniffer for ESP32
 *
 * File:    include/wifi_manager.h
 * Author:  Ahmed Ellamiee <ahmed.ellamiee@gmail.com>
 * Copyright (c) 2026 Ahmed Ellamiee
 */

#pragma once
#include <WiFi.h>
#include <WiFiUdp.h>
#include "config.h"

// WiFi/TCP side of the sniffer. Responsible for:
//   - Starting STA or AP based on settings.wifiMode.
//   - Hosting the GVRET telnet server on port 23 (WiFi GVRET).
//   - Broadcasting a discovery ping on UDP 17222 every second.
// The HTTP configuration page is owned by `WebConfigServer`.
class WiFiManager {
public:
    WiFiManager();

    // One-shot startup. Reapplies mode/SSID from settings.
    void setup();

    // Pump connection/server state. Call from the wifi worker task.
    void loop();

    // Push any frames queued in `wifiGVRET` out to connected telnet clients.
    void sendBufferedData();

    // Drop connections + stop servers. Called before switching WiFi mode.
    void shutdown();

    // Apply mode changes at runtime without rebooting.
    void reconfigure();

private:
    WiFiServer wifiServer;
    WiFiUDP    wifiUDPServer;
    uint32_t   lastBroadcast;
    bool       serversStarted;

    void ensureServers();
};
