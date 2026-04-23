/*
 * CAN_GVRET_Bridge - GVRET-compatible CAN sniffer for ESP32
 *
 * File:    config.h
 * Author:  Ahmed Ellamiee <ahmed.ellamiee@gmail.com>
 * Copyright (c) 2026 Ahmed Ellamiee
 *
 * Central configuration header for the CAN_GVRET_Bridge firmware.
 * Declares persistent (EEPROMSettings) and runtime (SystemSettings) state,
 * plus all hardware pins and compile-time constants.
 */

#ifndef CONFIG_H_
#define CONFIG_H_

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_idf_version.h>
#include <esp32_mcp2517fd.h>

// Size of each GVRET transmit buffer (used for both USB and WiFi handlers).
// Kept under the WiFi MTU so WiFi flushes fit into a single TCP segment.
#define SER_BUFF_SIZE       1024
#define WIFI_BUFF_SIZE      2048

// Hard flush interval for GVRET buffers (microseconds).
#define SER_BUFF_FLUSH_INTERVAL 20000

#define CFG_BUILD_NUM   700
#define CFG_VERSION     "SDTSv2 2026-04-22"
#define PREF_NAME       "SDTSv2"
#define DEVICE_NAME     "CAN_GVRET_Bridge"

#define NUM_ANALOG  NUM_ANALOG_INPUTS
#define NUM_DIGITAL 6
#define NUM_OUTPUT  6
#define NUM_BUSES   2

// Status LED (single WS2812 on dev board).
#define LED_PIN     2
#define NUM_LEDS    1
#define BRIGHTNESS  190
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB

// Secondary UART pins (not used by default; exposed via config).
#define RXD2 25
#define TXD2 26

// Board pinout mapping
#define HEARTBEAT_LED_PIN 2
#define IGN_PIN           14

// CAN0 (built-in ESP32 TWAI transceiver)
#define CAN0_RX_PIN       32
#define CAN0_TX_PIN       33

// CAN1 (MCP2517FD over SPI)
#define CAN2_INT_PIN      21
#define CAN2_CS_PIN       5
#define CAN2_SCK_PIN      18
#define CAN2_MISO_PIN     19
#define CAN2_MOSI_PIN     23

#define FORCE_SPI_CAN1 1

// Newer esp32_can only exposes CAN1 when the target has a second TWAI controller
// and the IDF version is 5.2+. Provide a compile-time fallback for single-TWAI chips.
#if !defined(FORCE_SPI_CAN1) && !(defined(SOC_TWAI_CONTROLLER_NUM) && (SOC_TWAI_CONTROLLER_NUM == 2) && defined(ESP_IDF_VERSION) && (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)))
#define CAN1 CAN0
#define EASYCAN_CAN1_FALLBACK 1
#endif

// Maximum simultaneous telnet clients for WiFi GVRET.
#define MAX_CLIENTS 1

// --- Enumerations used by both persisted and runtime config -----------------

// Transport used for GVRET traffic (CAN <-> host).
// The web page and USB CLI both let the user pick one of these values.
enum ActiveTransport : uint8_t {
    TRANSPORT_USB  = 0,
    TRANSPORT_WIFI = 1,
    TRANSPORT_BOTH = 2
};

// Serial framing mode used by the USB GVRET handler when emitting frames.
// GVRET clients (e.g. SavvyCAN) send 0xE7 to request binary mode at runtime.
enum SerialFrameMode : uint8_t {
    SERIAL_MODE_TEXT   = 0,
    SERIAL_MODE_BINARY = 1
};

// Selects which physical CAN channel(s) are active.
enum CanSelection : uint8_t {
    CAN_SEL_NONE = 0,
    CAN_SEL_CAN0 = 1,
    CAN_SEL_CAN1 = 2,
    CAN_SEL_BOTH = 3
};

// WiFi operating mode.
// NOTE: Arduino's `WiFiType.h` already defines `WIFI_OFF`, `WIFI_STA`, `WIFI_AP`
// as macros / enum values, so use distinct names here.
enum WiFiOpMode : uint8_t {
    WIFI_MODE_DISABLED = 0,
    WIFI_MODE_STA_CFG  = 1,
    WIFI_MODE_AP_CFG   = 2
};

// --- Persisted settings (stored in NVS via Preferences) ---------------------

struct EEPROMSettings {
    // CAN channel settings
    uint32_t CAN0Speed;
    bool     CAN0_Enabled;
    bool     CAN0ListenOnly;

    uint32_t CAN1Speed;
    bool     CAN1_Enabled;
    bool     CAN1ListenOnly;

    // Transport / framing selection
    uint8_t  activeTransport;   // ActiveTransport
    uint32_t usbBaudrate;       // USB CDC-ACM baud (921600 default)
    uint8_t  serialMode;        // SerialFrameMode (boot default; host can flip to binary)
    uint8_t  canSelection;      // CanSelection (derived from CAN0/CAN1 enable flags but stored)

    // Logging
    uint8_t  logLevel;

    // WiFi
    uint8_t  wifiMode;          // WiFiOpMode
    char     SSID[32];
    char     WPA2Key[64];
} __attribute__((__packed__));

// --- Runtime (non-persisted) system state -----------------------------------

struct SystemSettings {
    // LED helpers kept for API compatibility - set to 255 to disable.
    uint8_t LED_CANTX;
    uint8_t LED_CANRX;
    uint8_t LED_LOGGING;
    bool    fancyLED;
    bool    txToggle;
    bool    rxToggle;
    bool    logToggle;

    int8_t  numBuses;
    WiFiClient clientNodes[MAX_CLIENTS];
    bool    isWifiConnected;
    bool    isWifiActive;
};

// Forward declarations to reduce header coupling.
class GVRET_Comm_Handler;
class SerialConsole;
class CANManager;
class WiFiManager;
class WebConfigServer;

extern EEPROMSettings settings;
extern SystemSettings SysSettings;
extern Preferences nvPrefs;
extern GVRET_Comm_Handler serialGVRET;
extern GVRET_Comm_Handler wifiGVRET;
extern SerialConsole console;
extern CANManager canManager;
extern WiFiManager wifiManager;

// Minimal LED/IO stubs - the project removed the digital/analog IO module.
// These inline no-ops preserve legacy call sites without pulling in hardware.
inline void setLED(uint8_t /*which*/, bool /*hi*/) {}
inline void setOutput(uint8_t /*which*/, bool /*active*/) {}
inline void toggleRXLED() {}
inline void toggleTXLED() {}

// --- Serial text emission policy --------------------------------------------
//
// When the active transport is USB (or BOTH) AND the GVRET framing mode is
// BINARY, the USB CDC link is dedicated to the GVRET binary protocol. Any
// text emitted over `Serial` in that state would be interleaved into the
// binary stream and corrupt SavvyCAN's parser - the host would never receive
// a valid device info response and no CAN frames would appear.
//
// Every runtime log call in this firmware routes through this predicate
// before touching `Serial.*`. If it returns false, the caller must drop the
// message (the web UI and telnet log channel are still available).
inline bool serialTextAllowed() {
    if (settings.serialMode != SERIAL_MODE_BINARY) return true;
    if (settings.activeTransport == TRANSPORT_WIFI) return true;
    return false;
}

#endif /* CONFIG_H_ */
