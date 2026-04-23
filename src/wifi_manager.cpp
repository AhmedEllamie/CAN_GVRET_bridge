/*
 * SDTS V2 Sniffer - GVRET-compatible CAN sniffer for ESP32
 *
 * File:    src/wifi_manager.cpp
 * Author:  Ahmed Ellamiee <ahmed.ellamiee@gmail.com>
 * Copyright (c) 2026 Ahmed Ellamiee
 */

#include "config.h"
#include "wifi_manager.h"
#include "gvret_comm.h"
#include "web_config.h"
#include <ESPmDNS.h>
#include <WiFi.h>

// Emit a log line to Serial only when the USB link is NOT acting as a
// dedicated GVRET binary transport. See `serialTextAllowed()` in config.h.
#define WIFI_LOG(...) do { if (serialTextAllowed()) { Serial.printf(__VA_ARGS__); } } while (0)
#define WIFI_LOGLN(msg) do { if (serialTextAllowed()) { Serial.println(msg); } } while (0)

static IPAddress broadcastAddr(255, 255, 255, 255);

static void buildDefaultApSsid(char *out, size_t outLen) {
    const uint16_t macLsb = (uint16_t)(ESP.getEfuseMac() & 0xFFFFu);
    snprintf(out, outLen, "CAN_GVRET_%04X", (unsigned)macLsb);
}

WiFiManager::WiFiManager() : wifiServer(23), lastBroadcast(0), serversStarted(false) {}

void WiFiManager::setup() {
    reconfigure();
}

void WiFiManager::shutdown() {
    if (serversStarted) {
        wifiServer.stop();
        serversStarted = false;
    }
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (SysSettings.clientNodes[i]) SysSettings.clientNodes[i].stop();
    }
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    SysSettings.isWifiConnected = false;
    SysSettings.isWifiActive = false;
}

void WiFiManager::reconfigure() {
    shutdown();

    switch (settings.wifiMode) {
    case WIFI_MODE_STA_CFG:
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(true);
        WiFi.begin((const char *)settings.SSID, (const char *)settings.WPA2Key);
        WIFI_LOG("[wifi] STA connecting to \"%s\"\n", settings.SSID);
        break;
    case WIFI_MODE_AP_CFG:
        // Enforce AP identity format required by project owner:
        // SSID: CAN_GVRET_XXXX (XXXX = lower 16 bits of ESP MAC)
        // Password: P@ssw0rd
        buildDefaultApSsid(settings.SSID, sizeof(settings.SSID));
        strncpy(settings.WPA2Key, "P@ssw0rd", sizeof(settings.WPA2Key) - 1);
        settings.WPA2Key[sizeof(settings.WPA2Key) - 1] = 0;

        WiFi.mode(WIFI_AP);
        WiFi.setSleep(true);
        WiFi.softAP((const char *)settings.SSID, (const char *)settings.WPA2Key);
        WIFI_LOG("[wifi] AP \"%s\" ip=%s\n",
                 settings.SSID, WiFi.softAPIP().toString().c_str());
        SysSettings.isWifiConnected = true;
        ensureServers();
        break;
    default:
        WIFI_LOGLN("[wifi] disabled");
        break;
    }
}

void WiFiManager::ensureServers() {
    if (serversStarted) return;

    if (!MDNS.begin(DEVICE_NAME)) {
        WIFI_LOGLN("[wifi] mDNS start failed");
    } else {
        MDNS.addService("telnet", "tcp", 23);
        MDNS.addService("http",   "tcp", 80);
    }
    wifiServer.begin(23);
    wifiServer.setNoDelay(true);
    webConfig.begin();
    serversStarted = true;
    WIFI_LOGLN("[wifi] GVRET telnet on :23, web config on :80");
}

void WiFiManager::loop() {
    if (settings.wifiMode == WIFI_MODE_DISABLED) return;

    // Handle connection state transitions (STA only - AP is always up).
    if (settings.wifiMode == WIFI_MODE_STA_CFG) {
        if (!SysSettings.isWifiConnected && WiFi.isConnected()) {
            WIFI_LOG("[wifi] STA connected %s rssi=%d\n",
                     WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
            SysSettings.isWifiConnected = true;
            ensureServers();
        } else if (SysSettings.isWifiConnected && !WiFi.isConnected()) {
            WIFI_LOGLN("[wifi] STA lost connection");
            SysSettings.isWifiConnected = false;
            SysSettings.isWifiActive = false;
        }
    }

    if (!serversStarted) return;

    // Accept / read from GVRET telnet clients.
    if (wifiServer.hasClient()) {
        bool accepted = false;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!SysSettings.clientNodes[i] || !SysSettings.clientNodes[i].connected()) {
                if (SysSettings.clientNodes[i]) SysSettings.clientNodes[i].stop();
                SysSettings.clientNodes[i] = wifiServer.available();
                SysSettings.clientNodes[i].setNoDelay(true);
                WIFI_LOG("[wifi] client %d: %s\n",
                         i, SysSettings.clientNodes[i].remoteIP().toString().c_str());
                accepted = true;
                break;
            }
        }
        if (!accepted) wifiServer.available().stop();
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (SysSettings.clientNodes[i] && SysSettings.clientNodes[i].connected()) {
            while (SysSettings.clientNodes[i].available()) {
                SysSettings.isWifiActive = true;
                wifiGVRET.processIncomingByte(SysSettings.clientNodes[i].read());
            }
        } else if (SysSettings.clientNodes[i]) {
            SysSettings.clientNodes[i].stop();
            SysSettings.isWifiActive = false;
        }
    }

    if (SysSettings.isWifiConnected && ((micros() - lastBroadcast) > 1000000ul)) {
        uint8_t buff[4] = {0x1C, 0xEF, 0xAC, 0xED};
        lastBroadcast = micros();
        wifiUDPServer.beginPacket(broadcastAddr, 17222);
        wifiUDPServer.write(buff, 4);
        wifiUDPServer.endPacket();
    }
}

void WiFiManager::sendBufferedData() {
    if (!serversStarted) return;
    uint8_t scratch[WIFI_BUFF_SIZE];
    size_t n = wifiGVRET.drainTo(scratch, sizeof(scratch));
    if (n == 0) return;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (SysSettings.clientNodes[i] && SysSettings.clientNodes[i].connected()) {
            SysSettings.clientNodes[i].write(scratch, n);
        }
    }
}
