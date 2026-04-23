/*
 * SDTS V2 Sniffer - GVRET-compatible CAN sniffer for ESP32
 *
 * File:    include/commbuffer.h
 * Author:  Ahmed Ellamiee <ahmed.ellamiee@gmail.com>
 * Copyright (c) 2026 Ahmed Ellamiee
 */

#pragma once
#include <Arduino.h>
#include "config.h"
#include "esp32_can.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Fixed-capacity transmit buffer shared between producer tasks (CAN readers
// writing frames) and consumer tasks (USB / WiFi workers flushing bytes).
// All mutating operations are serialized through an internal FreeRTOS mutex.
class CommBuffer {
public:
    CommBuffer();

    size_t numAvailableBytes();
    uint8_t* getBufferedBytes();
    void clearBufferedBytes();

    void sendFrameToBuffer(CAN_FRAME &frame, int whichBus);
    void sendBytesToBuffer(uint8_t *bytes, size_t length);
    void sendByteToBuffer(uint8_t byt);
    void sendString(String str);
    void sendCharString(const char *str);

    // Atomically copy currently buffered bytes to `out` (at most `maxLen`)
    // and clear the buffer. Returns number of bytes copied.
    size_t drainTo(uint8_t *out, size_t maxLen);

protected:
    byte transmitBuffer[WIFI_BUFF_SIZE];
    int transmitBufferLength;
    SemaphoreHandle_t mutex;

    void lock();
    void unlock();
};
