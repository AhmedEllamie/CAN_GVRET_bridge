/*
 * SDTS V2 Sniffer - GVRET-compatible CAN sniffer for ESP32
 *
 * File:    src/main.cpp
 * Author:  Ahmed Ellamiee <ahmed.ellamiee@gmail.com>
 * Copyright (c) 2026 Ahmed Ellamiee
 *
 * Main entry point.
 *
 * Architecture:
 *   - `setup()` boots hardware and spawns five FreeRTOS tasks:
 *       systemManagerTask  : heartbeat, flush policy, health
 *       can0ReaderTask     : drain CAN0 -> frame router
 *       can1ReaderTask     : drain CAN1 -> frame router
 *       usbWorkerTask      : USB serial RX + GVRET buffer flush
 *       wifiWorkerTask     : WiFi + telnet + web UI + wifi GVRET flush
 *   - Shared state is protected inside the respective module (CANManager,
 *     CommBuffer). The global `settings` struct is treated as mostly-read
 *     from worker tasks; writes go through `ConfigService`.
 *
 * Serial text policy: when USB transport + SERIAL_MODE_BINARY are active,
 * all text prints (heartbeat, menus, logs) are suppressed - the USB link is
 * dedicated to the GVRET binary framing that SavvyCAN expects.
 */

#include "config.h"
#include <esp32_can.h>
#include <SPI.h>
#include <Preferences.h>
#include <FastLED.h>
#include "SerialConsole.h"
#include "wifi_manager.h"
#include "web_config.h"
#include "gvret_comm.h"
#include "can_manager.h"
#include "config_service.h"
#include "Logger.h"

EEPROMSettings settings;
SystemSettings SysSettings;
Preferences nvPrefs;

GVRET_Comm_Handler serialGVRET;   // USB GVRET transport
GVRET_Comm_Handler wifiGVRET;     // WiFi GVRET transport
CANManager         canManager;
WiFiManager        wifiManager;
SerialConsole      console;

CRGB leds[NUM_LEDS];

// ---------------------------------------------------------------------------
// FreeRTOS task entry points
// ---------------------------------------------------------------------------

static void can0ReaderTask(void *) {
    const TickType_t idleTicks = pdMS_TO_TICKS(1);
    for (;;) {
        int drained = canManager.pumpCan0();
        if (drained == 0) vTaskDelay(idleTicks);
    }
}

static void can1ReaderTask(void *) {
    const TickType_t idleTicks = pdMS_TO_TICKS(1);
    for (;;) {
        int drained = canManager.pumpCan1();
        if (drained == 0) vTaskDelay(idleTicks);
    }
}

static void usbWorkerTask(void *) {
    const TickType_t idleTicks = pdMS_TO_TICKS(1);
    uint8_t scratch[SER_BUFF_SIZE];
    for (;;) {
        int readCount = 0;
        while (Serial.available() > 0 && readCount < 256) {
            serialGVRET.processIncomingByte((uint8_t)Serial.read());
            readCount++;
        }

        if (settings.activeTransport == TRANSPORT_USB ||
            settings.activeTransport == TRANSPORT_BOTH) {
            size_t n = serialGVRET.drainTo(scratch, sizeof(scratch));
            if (n > 0) Serial.write(scratch, n);
        } else {
            serialGVRET.clearBufferedBytes();
        }

        if (readCount == 0) vTaskDelay(idleTicks);
    }
}

static void wifiWorkerTask(void *) {
    const TickType_t tick = pdMS_TO_TICKS(2);
    for (;;) {
        wifiManager.loop();
        webConfig.loop();

        if (settings.activeTransport == TRANSPORT_WIFI ||
            settings.activeTransport == TRANSPORT_BOTH) {
            wifiManager.sendBufferedData();
        } else {
            wifiGVRET.clearBufferedBytes();
        }
        vTaskDelay(tick);
    }
}

static void systemManagerTask(void *) {
    const TickType_t tick = pdMS_TO_TICKS(50);
    uint32_t lastHeartbeat = 0;
    for (;;) {
        canManager.housekeeping();

        if (millis() - lastHeartbeat > 5000) {
            lastHeartbeat = millis();
            // Heartbeat is a debug aid - only emit it when the USB link is
            // either idle (WiFi-only transport) or in TEXT framing mode. In
            // BINARY + USB mode, SavvyCAN is on the other end and any ASCII
            // here would corrupt the protocol.
            if (serialTextAllowed() && Logger::getLogLevel() <= Logger::Info) {
                Serial.printf("[hb] uptime=%lus rx0=%u rx1=%u wifi=%d xport=%u\n",
                              (unsigned long)(millis() / 1000),
                              canManager.getReceivedFrameCount(0),
                              canManager.getReceivedFrameCount(1),
                              SysSettings.isWifiConnected,
                              settings.activeTransport);
            }
        }
        vTaskDelay(tick);
    }
}

// ---------------------------------------------------------------------------
// Arduino lifecycle
// ---------------------------------------------------------------------------

void setup() {
    SysSettings.LED_CANTX    = 255;
    SysSettings.LED_CANRX    = 255;
    SysSettings.LED_LOGGING  = 255;
    SysSettings.fancyLED     = true;
    SysSettings.txToggle     = true;
    SysSettings.rxToggle     = true;
    SysSettings.logToggle    = false;
    SysSettings.numBuses     = 2;
    SysSettings.isWifiConnected = false;
    SysSettings.isWifiActive    = false;

    ConfigService::load();

    Serial.begin(settings.usbBaudrate);
    delay(50);

    // Boot banner - only in text mode so we don't dirty SavvyCAN's binary
    // stream on first connect.
    if (serialTextAllowed()) {
        Serial.println();
        Serial.printf("SDTS V2 Sniffer - build %u\n", (unsigned)CFG_BUILD_NUM);
    }

    pinMode(IGN_PIN, INPUT);

    // CAN1 (MCP2517FD) lives on SPI; pins must be configured before `begin()`.
    CAN1.setCSPin(CAN2_CS_PIN);
    CAN1.setINTPin(CAN2_INT_PIN);
    SPI.begin(CAN2_SCK_PIN, CAN2_MISO_PIN, CAN2_MOSI_PIN, CAN2_CS_PIN);

    FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
    FastLED.setBrightness(BRIGHTNESS);
    leds[0] = CRGB::Gold;
    FastLED.show();

    canManager.setup();
    wifiManager.setup();

    leds[0] = SysSettings.isWifiConnected ? CRGB::Green : CRGB::Gold;
    FastLED.show();

    // Reader tasks are pinned to core 0 with higher priority so RX never
    // starves. Network/USB workers live on core 1.
    xTaskCreatePinnedToCore(can0ReaderTask,     "can0Reader", 4096, nullptr, 5, nullptr, 0);
    xTaskCreatePinnedToCore(can1ReaderTask,     "can1Reader", 4096, nullptr, 5, nullptr, 0);
    xTaskCreatePinnedToCore(usbWorkerTask,      "usbWorker",  4096, nullptr, 3, nullptr, 1);
    xTaskCreatePinnedToCore(wifiWorkerTask,     "wifiWorker", 8192, nullptr, 3, nullptr, 1);
    xTaskCreatePinnedToCore(systemManagerTask,  "sysMgr",     4096, nullptr, 2, nullptr, 1);

    if (serialTextAllowed()) {
        console.printMenu();
        Serial.println("[boot] tasks running. Use 'h' for CLI help, or browse to the device IP.");
    }
}

void loop() {
    // All real work happens in the FreeRTOS tasks above.
    vTaskDelay(pdMS_TO_TICKS(1000));
}
