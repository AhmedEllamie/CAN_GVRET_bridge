/*
 * CAN_GVRET_Bridge - GVRET-compatible CAN sniffer for ESP32
 *
 * File:    include/can_manager.h
 * Author:  Ahmed Ellamiee <ahmed.ellamiee@gmail.com>
 * Copyright (c) 2026 Ahmed Ellamiee
 */

#pragma once
#include "config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Per-bus bitrate accounting for bus load reporting.
struct BusLoad {
    uint32_t bitsPerQuarter;
    uint32_t bitsSoFar;
    uint8_t  busloadPercentage;
};

class CAN_COMMON;
class CAN_FRAME;

// Owns both CAN controllers (CAN0 = TWAI, CAN1 = MCP2517FD) and mediates all
// sends/receives. Reads happen in dedicated reader tasks; sends from any task
// are serialized via an internal mutex.
class CANManager {
public:
    CANManager();

    // Initial hardware setup called once from `setup()`.
    void setup();

    // Disable both controllers, then re-initialize each according to the
    // current `settings` struct (enable flags, speeds, listen-only).
    // Safe to call from any task; takes the send mutex internally.
    void reconfigure();

    // Thread-safe send. Returns true on success.
    bool sendFrame(CAN_COMMON *bus, CAN_FRAME &frame);

    // Route a frame to the appropriate GVRET transmit buffers based on the
    // currently selected transport (USB / WiFi / BOTH).
    void displayFrame(CAN_FRAME &frame, int whichBus);

    // Drain frames from CAN0 / CAN1 respectively. Returns number of frames read.
    // Designed to be called from dedicated FreeRTOS reader tasks.
    int pumpCan0();
    int pumpCan1();

    // Periodic housekeeping: update bus load counters, etc.
    void housekeeping();

    uint32_t getReceivedFrameCount(uint8_t whichBus) const;

private:
    void addBits(int offset, CAN_FRAME &frame);
    void startBus(int which);
    void stopBus(int which);

    BusLoad busLoad[NUM_BUSES];
    uint32_t busLoadTimer;
    uint32_t receivedFrameCount[NUM_BUSES];

    // Serializes concurrent `sendFrame()` and `reconfigure()` calls.
    SemaphoreHandle_t sendMutex;
};
