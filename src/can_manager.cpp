/*
 * CAN_GVRET_Bridge - GVRET-compatible CAN sniffer for ESP32
 *
 * File:    src/can_manager.cpp
 * Author:  Ahmed Ellamiee <ahmed.ellamiee@gmail.com>
 * Copyright (c) 2026 Ahmed Ellamiee
 */

#include <Arduino.h>
#include "can_manager.h"
#include "esp32_can.h"
#include "config.h"
#include "gvret_comm.h"

// ESP32 rev > 2 boards in this project have historically needed doubled
// timing values to achieve the requested nominal bitrate on the built-in
// TWAI controller (CAN0). The MCP2517FD (CAN1) has its own crystal, so
// it must NOT get the rev-doubling.
static uint32_t effectiveCanSpeed(uint32_t speed) {
    if (ESP.getChipRevision() > 2) return speed * 2;
    return speed;
}

CANManager::CANManager() : busLoadTimer(0), sendMutex(nullptr) {
    for (int j = 0; j < NUM_BUSES; j++) {
        receivedFrameCount[j] = 0;
        busLoad[j].bitsPerQuarter = 125000;
        busLoad[j].bitsSoFar = 0;
        busLoad[j].busloadPercentage = 0;
    }
}

void CANManager::setup() {
    if (!sendMutex) sendMutex = xSemaphoreCreateMutex();

    // Configure pins once; subsequent reconfigure() calls only toggle enable/speed.
    CAN0.setCANPins((gpio_num_t)CAN0_RX_PIN, (gpio_num_t)CAN0_TX_PIN);
    // CAN1 (MCP2517FD SPI) pins are configured by the sketch at startup.

    reconfigure();
}

void CANManager::startBus(int which) {
    if (which == 0) {
        // Clamp to a sane range. CAN0 (built-in TWAI) runs with a doubled
        // divisor on ESP32 rev > 2; doubling above 500 kbps would exceed the
        // 1 Mbps CAN spec and leave the driver wedged.
        uint32_t reqSpeed = settings.CAN0Speed;
        if (reqSpeed > 500000 && ESP.getChipRevision() > 2) reqSpeed = 500000;
        CAN0.enable();
        CAN0.begin(effectiveCanSpeed(reqSpeed), 255);
        CAN0.setListenOnlyMode(settings.CAN0ListenOnly);
        CAN0.watchFor();
        busLoad[0].bitsPerQuarter = settings.CAN0Speed / 4;
        if (busLoad[0].bitsPerQuarter == 0) busLoad[0].bitsPerQuarter = 125000;
    } else if (which == 1 && SysSettings.numBuses > 1) {
        CAN1.enable();
        // CAN1 is MCP2517FD on SPI with its own oscillator - pass raw speed.
        CAN1.begin(settings.CAN1Speed, 255);
        CAN1.setListenOnlyMode(settings.CAN1ListenOnly);
        CAN1.watchFor();
        busLoad[1].bitsPerQuarter = settings.CAN1Speed / 4;
        if (busLoad[1].bitsPerQuarter == 0) busLoad[1].bitsPerQuarter = 125000;
    }
}

void CANManager::stopBus(int which) {
    if (which == 0) CAN0.disable();
    else if (which == 1 && SysSettings.numBuses > 1) CAN1.disable();
}

void CANManager::reconfigure() {
    if (sendMutex) xSemaphoreTake(sendMutex, portMAX_DELAY);

    stopBus(0);
    stopBus(1);

    if (settings.CAN0_Enabled) startBus(0);
    if (settings.CAN1_Enabled && SysSettings.numBuses > 1) startBus(1);

    for (int j = 0; j < NUM_BUSES; j++) {
        busLoad[j].bitsSoFar = 0;
        busLoad[j].busloadPercentage = 0;
    }
    busLoadTimer = millis();

    // Suppress this banner when the USB link is dedicated to binary GVRET -
    // a reconfigure can happen from the web UI while SavvyCAN is connected,
    // and any ASCII here would corrupt the protocol stream.
    if (serialTextAllowed()) {
        Serial.printf("[CAN] reconfigured: CAN0 en=%d speed=%u LO=%d | CAN1 en=%d speed=%u LO=%d\n",
                      settings.CAN0_Enabled, settings.CAN0Speed, settings.CAN0ListenOnly,
                      settings.CAN1_Enabled, settings.CAN1Speed, settings.CAN1ListenOnly);
    }

    if (sendMutex) xSemaphoreGive(sendMutex);
}

void CANManager::addBits(int offset, CAN_FRAME &frame) {
    if (offset < 0 || offset >= NUM_BUSES) return;
    busLoad[offset].bitsSoFar += 41 + (frame.length * 9);
    if (frame.extended) busLoad[offset].bitsSoFar += 18;
}

bool CANManager::sendFrame(CAN_COMMON *bus, CAN_FRAME &frame) {
    if (!bus) return false;
    if (sendMutex) xSemaphoreTake(sendMutex, portMAX_DELAY);
    int whichBus = (bus == &CAN0) ? 0 : (bus == &CAN1 ? 1 : -1);
    bool ok = bus->sendFrame(frame);
    if (whichBus >= 0) addBits(whichBus, frame);
    if (sendMutex) xSemaphoreGive(sendMutex);
    return ok;
}

void CANManager::displayFrame(CAN_FRAME &frame, int whichBus) {
    // Honor transport selection - a frame is mirrored to USB, WiFi, or both.
    const uint8_t xport = settings.activeTransport;
    if (xport == TRANSPORT_USB || xport == TRANSPORT_BOTH) {
        serialGVRET.sendFrameToBuffer(frame, whichBus);
    }
    if ((xport == TRANSPORT_WIFI || xport == TRANSPORT_BOTH) && SysSettings.isWifiConnected) {
        wifiGVRET.sendFrameToBuffer(frame, whichBus);
    }
}

int CANManager::pumpCan0() {
    if (!settings.CAN0_Enabled) return 0;
    int count = 0;
    CAN_FRAME incoming;
    // Bounded per-pump read count to avoid starving other tasks.
    while (count < 32 && CAN0.available() > 0) {
        if (!CAN0.read(incoming)) break;
        receivedFrameCount[0]++;
        addBits(0, incoming);
        displayFrame(incoming, 0);
        count++;
    }
    return count;
}

int CANManager::pumpCan1() {
    if (SysSettings.numBuses < 2) return 0;
    if (!settings.CAN1_Enabled) return 0;
    int count = 0;
    CAN_FRAME incoming;
    while (count < 32 && CAN1.available() > 0) {
        if (!CAN1.read(incoming)) break;
        receivedFrameCount[1]++;
        addBits(1, incoming);
        displayFrame(incoming, 1);
        count++;
    }
    return count;
}

void CANManager::housekeeping() {
    if (millis() <= (busLoadTimer + 250)) return;
    busLoadTimer = millis();
    for (int j = 0; j < NUM_BUSES; j++) {
        uint32_t denom = busLoad[j].bitsPerQuarter;
        if (denom == 0) denom = 125000;
        busLoad[j].busloadPercentage = ((busLoad[j].busloadPercentage * 3) +
                                       (((busLoad[j].bitsSoFar * 1000) / denom) / 10)) / 4;
        if (busLoad[j].busloadPercentage == 0 && busLoad[j].bitsSoFar > 0) {
            busLoad[j].busloadPercentage = 1;
        }
        busLoad[j].bitsSoFar = 0;
    }
    busLoad[0].bitsPerQuarter = settings.CAN0Speed / 4;
    busLoad[1].bitsPerQuarter = settings.CAN1Speed / 4;
    if (busLoad[0].bitsPerQuarter == 0) busLoad[0].bitsPerQuarter = 125000;
    if (busLoad[1].bitsPerQuarter == 0) busLoad[1].bitsPerQuarter = 125000;
}

uint32_t CANManager::getReceivedFrameCount(uint8_t whichBus) const {
    if (whichBus >= NUM_BUSES) return 0;
    return receivedFrameCount[whichBus];
}
