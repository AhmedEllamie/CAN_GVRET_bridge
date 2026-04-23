/*
 * CAN_GVRET_Bridge - GVRET-compatible CAN sniffer for ESP32
 *
 * File:    include/SerialConsole.h
 * Author:  Ahmed Ellamiee <ahmed.ellamiee@gmail.com>
 * Copyright (c) 2026 Ahmed Ellamiee
 */

#ifndef SERIALCONSOLE_H_
#define SERIALCONSOLE_H_

#include "config.h"
#include "esp32_can.h"

// Simple line-based USB serial CLI. Kept alongside the web UI so a user can
// configure the device over a plain terminal when WiFi is unavailable.
class SerialConsole {
public:
    SerialConsole();
    void printMenu();
    void printStatus();
    void rcvCharacter(uint8_t chr);

protected:
    enum CONSOLE_STATE { STATE_ROOT_MENU };

private:
    char cmdBuffer[80];
    int  ptrBuffer;
    int  state;

    void init();
    void handleConsoleCmd();
    void handleShortCmd();
    void handleConfigCmd();
    bool handleCANSend(CAN_COMMON &port, char *inputString);
};

#endif /* SERIALCONSOLE_H_ */
