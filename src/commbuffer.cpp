/*
 * SDTS V2 Sniffer - GVRET-compatible CAN sniffer for ESP32
 *
 * File:    src/commbuffer.cpp
 * Author:  Ahmed Ellamiee <ahmed.ellamiee@gmail.com>
 * Copyright (c) 2026 Ahmed Ellamiee
 */

#include "commbuffer.h"
#include "Logger.h"

CommBuffer::CommBuffer() : transmitBufferLength(0) {
    mutex = xSemaphoreCreateMutex();
}

void CommBuffer::lock() {
    if (mutex) xSemaphoreTake(mutex, portMAX_DELAY);
}

void CommBuffer::unlock() {
    if (mutex) xSemaphoreGive(mutex);
}

size_t CommBuffer::numAvailableBytes() {
    lock();
    size_t n = transmitBufferLength;
    unlock();
    return n;
}

void CommBuffer::clearBufferedBytes() {
    lock();
    transmitBufferLength = 0;
    unlock();
}

uint8_t* CommBuffer::getBufferedBytes() {
    // Returned pointer must only be consumed while holding the external flush
    // protocol contract: the caller is expected to call `clearBufferedBytes()`
    // promptly. For safer usage prefer `drainTo()`.
    return transmitBuffer;
}

size_t CommBuffer::drainTo(uint8_t *out, size_t maxLen) {
    lock();
    size_t n = transmitBufferLength;
    if (n > maxLen) n = maxLen;
    if (n > 0 && out) memcpy(out, transmitBuffer, n);
    // Shift any remainder to the start (usually none, since we drain fully).
    if (transmitBufferLength > (int)n) {
        memmove(transmitBuffer, transmitBuffer + n, transmitBufferLength - n);
    }
    transmitBufferLength -= n;
    unlock();
    return n;
}

void CommBuffer::sendBytesToBuffer(uint8_t *bytes, size_t length) {
    lock();
    if (transmitBufferLength + (int)length > WIFI_BUFF_SIZE) {
        // Drop the chunk rather than overflow the buffer; overflow would
        // corrupt downstream GVRET frames.
        unlock();
        return;
    }
    memcpy(&transmitBuffer[transmitBufferLength], bytes, length);
    transmitBufferLength += length;
    unlock();
}

void CommBuffer::sendByteToBuffer(uint8_t byt) {
    lock();
    if (transmitBufferLength < WIFI_BUFF_SIZE) {
        transmitBuffer[transmitBufferLength++] = byt;
    }
    unlock();
}

void CommBuffer::sendString(String str) {
    sendCharString(str.c_str());
}

void CommBuffer::sendCharString(const char *str) {
    if (!str) return;
    size_t len = strlen(str);
    sendBytesToBuffer((uint8_t *)str, len);
}

void CommBuffer::sendFrameToBuffer(CAN_FRAME &frame, int whichBus) {
    lock();

    // Reserve headroom so we never write past the buffer for a worst-case frame.
    const size_t kMaxFrameBytes = 64;
    if (transmitBufferLength + kMaxFrameBytes > WIFI_BUFF_SIZE) {
        unlock();
        return;
    }

    if (settings.serialMode == SERIAL_MODE_BINARY) {
        // GVRET binary frame format - matches the stock protocol used by SavvyCAN.
        if (frame.extended) frame.id |= 1u << 31;
        transmitBuffer[transmitBufferLength++] = 0xF1;
        transmitBuffer[transmitBufferLength++] = 0; // 0 = CAN frame
        uint32_t now = micros();
        transmitBuffer[transmitBufferLength++] = (uint8_t)(now & 0xFF);
        transmitBuffer[transmitBufferLength++] = (uint8_t)(now >> 8);
        transmitBuffer[transmitBufferLength++] = (uint8_t)(now >> 16);
        transmitBuffer[transmitBufferLength++] = (uint8_t)(now >> 24);
        transmitBuffer[transmitBufferLength++] = (uint8_t)(frame.id & 0xFF);
        transmitBuffer[transmitBufferLength++] = (uint8_t)(frame.id >> 8);
        transmitBuffer[transmitBufferLength++] = (uint8_t)(frame.id >> 16);
        transmitBuffer[transmitBufferLength++] = (uint8_t)(frame.id >> 24);
        transmitBuffer[transmitBufferLength++] = frame.length + (uint8_t)(whichBus << 4);
        for (int c = 0; c < frame.length; c++) {
            transmitBuffer[transmitBufferLength++] = frame.data.uint8[c];
        }
        // Checksum byte left as zero to match legacy behavior.
        transmitBuffer[transmitBufferLength++] = 0;
    } else {
        size_t written = snprintf((char *)&transmitBuffer[transmitBufferLength],
                                  WIFI_BUFF_SIZE - transmitBufferLength,
                                  "%lu - %lx %s %i %i",
                                  (unsigned long)micros(),
                                  (unsigned long)frame.id,
                                  frame.extended ? "X" : "S",
                                  whichBus, frame.length);
        transmitBufferLength += written;
        for (int c = 0; c < frame.length; c++) {
            written = snprintf((char *)&transmitBuffer[transmitBufferLength],
                               WIFI_BUFF_SIZE - transmitBufferLength,
                               " %x", frame.data.uint8[c]);
            transmitBufferLength += written;
        }
        if (transmitBufferLength + 2 <= WIFI_BUFF_SIZE) {
            transmitBuffer[transmitBufferLength++] = '\r';
            transmitBuffer[transmitBufferLength++] = '\n';
        }
    }

    unlock();
}
