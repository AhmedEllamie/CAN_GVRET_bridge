/*
 * CAN_GVRET_Bridge - GVRET-compatible CAN sniffer for ESP32
 *
 * File:    src/config_service.cpp
 * Author:  Ahmed Ellamiee <ahmed.ellamiee@gmail.com>
 * Copyright (c) 2026 Ahmed Ellamiee
 */

#include "config_service.h"
#include "Logger.h"
#include "can_manager.h"
#include <esp32_can.h>

static const uint32_t kDefaultCan0Speed = 500000;
static const uint32_t kDefaultCan1Speed = 500000;
static const uint32_t kDefaultUsbBaud   = 921600;
static const char* kDefaultApPassword   = "P@ssw0rd";

void ConfigService::load() {
    nvPrefs.begin(PREF_NAME, false);

    settings.CAN0Speed       = nvPrefs.getUInt("can0speed", kDefaultCan0Speed);
    settings.CAN0_Enabled    = nvPrefs.getBool("can0_en", true);
    settings.CAN0ListenOnly  = nvPrefs.getBool("can0_lo", false);

    settings.CAN1Speed       = nvPrefs.getUInt("can1speed", kDefaultCan1Speed);
    settings.CAN1_Enabled    = nvPrefs.getBool("can1_en", true);
    settings.CAN1ListenOnly  = nvPrefs.getBool("can1_lo", false);

    settings.activeTransport = nvPrefs.getUChar("xport",   TRANSPORT_BOTH);
    settings.usbBaudrate     = nvPrefs.getUInt("usbBaud",  kDefaultUsbBaud);
    settings.serialMode      = nvPrefs.getUChar("serMode", SERIAL_MODE_TEXT);
    settings.canSelection    = nvPrefs.getUChar("canSel",  CAN_SEL_BOTH);

    settings.logLevel        = nvPrefs.getUChar("loglevel", 1);
    settings.wifiMode        = nvPrefs.getUChar("wifiMode", WIFI_MODE_AP_CFG);

    if (nvPrefs.getString("SSID", settings.SSID, 32) == 0) {
        strcpy(settings.SSID, DEVICE_NAME);
    }
    if (nvPrefs.getString("wpa2Key", settings.WPA2Key, 64) == 0) {
        strcpy(settings.WPA2Key, kDefaultApPassword);
    }

    nvPrefs.end();

    // Keep the derived CAN enable flags in sync with the selection enum.
    // The selection enum is authoritative whenever it has been explicitly set.
    switch (settings.canSelection) {
        case CAN_SEL_NONE: settings.CAN0_Enabled = false; settings.CAN1_Enabled = false; break;
        case CAN_SEL_CAN0: settings.CAN0_Enabled = true;  settings.CAN1_Enabled = false; break;
        case CAN_SEL_CAN1: settings.CAN0_Enabled = false; settings.CAN1_Enabled = true;  break;
        case CAN_SEL_BOTH: settings.CAN0_Enabled = true;  settings.CAN1_Enabled = true;  break;
        default: break;
    }

#ifdef EASYCAN_CAN1_FALLBACK
    // Single-TWAI targets alias CAN1 to CAN0; don't double-init the same controller.
    settings.CAN1_Enabled = false;
    settings.CAN1ListenOnly = false;
    if (settings.canSelection == CAN_SEL_CAN1 || settings.canSelection == CAN_SEL_BOTH) {
        settings.canSelection = CAN_SEL_CAN0;
        settings.CAN0_Enabled = true;
    }
    SysSettings.numBuses = 1;
#else
    SysSettings.numBuses = 2;
#endif

    Logger::setLoglevel((Logger::LogLevel)settings.logLevel);
}

void ConfigService::save() {
    nvPrefs.begin(PREF_NAME, false);

    nvPrefs.putUInt("can0speed",   settings.CAN0Speed);
    nvPrefs.putBool("can0_en",     settings.CAN0_Enabled);
    nvPrefs.putBool("can0_lo",     settings.CAN0ListenOnly);

    nvPrefs.putUInt("can1speed",   settings.CAN1Speed);
    nvPrefs.putBool("can1_en",     settings.CAN1_Enabled);
    nvPrefs.putBool("can1_lo",     settings.CAN1ListenOnly);

    nvPrefs.putUChar("xport",      settings.activeTransport);
    nvPrefs.putUInt("usbBaud",     settings.usbBaudrate);
    nvPrefs.putUChar("serMode",    settings.serialMode);
    nvPrefs.putUChar("canSel",     settings.canSelection);

    nvPrefs.putUChar("loglevel",   settings.logLevel);
    nvPrefs.putUChar("wifiMode",   settings.wifiMode);
    nvPrefs.putString("SSID",      settings.SSID);
    nvPrefs.putString("wpa2Key",   settings.WPA2Key);

    nvPrefs.end();
}

void ConfigService::factoryReset() {
    nvPrefs.begin(PREF_NAME, false);
    nvPrefs.clear();
    nvPrefs.end();
}

bool ConfigService::applyRuntime() {
    // Re-derive enable flags from the channel selection so the caller only
    // has to set one field.
    switch (settings.canSelection) {
        case CAN_SEL_NONE: settings.CAN0_Enabled = false; settings.CAN1_Enabled = false; break;
        case CAN_SEL_CAN0: settings.CAN0_Enabled = true;  settings.CAN1_Enabled = false; break;
        case CAN_SEL_CAN1: settings.CAN0_Enabled = false; settings.CAN1_Enabled = true;  break;
        case CAN_SEL_BOTH: settings.CAN0_Enabled = true;  settings.CAN1_Enabled = true;  break;
    }

    // Reapply serial framing mode flag used by CommBuffer to emit text vs binary frames.
    // NOTE: when a GVRET client sends 0xE7 it also flips this to binary at runtime.
    // Here we honor the persisted default on apply.
    Logger::setLoglevel((Logger::LogLevel)settings.logLevel);

    // Reconfigure both CAN controllers to match the new settings.
    canManager.reconfigure();
    return true;
}

bool ConfigService::isValidCanSpeed(uint32_t speed) {
    // Accept standard CAN2.0 rates plus any value up to 1 Mbps.
    if (speed == 0) return false;
    if (speed > 1000000) return false;
    return true;
}

bool ConfigService::isValidUsbBaudrate(uint32_t baud) {
    switch (baud) {
        case 9600: case 19200: case 38400: case 57600:
        case 115200: case 230400: case 460800: case 921600:
        case 1500000: case 2000000:
            return true;
        default:
            return false;
    }
}
