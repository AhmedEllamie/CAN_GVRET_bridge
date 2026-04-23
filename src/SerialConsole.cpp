/*
 * SDTS V2 Sniffer - GVRET-compatible CAN sniffer for ESP32
 *
 * File:    src/SerialConsole.cpp
 * Author:  Ahmed Ellamiee <ahmed.ellamiee@gmail.com>
 * Copyright (c) 2026 Ahmed Ellamiee
 */

#include "SerialConsole.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <esp32_can.h>
#include "config.h"
#include "Logger.h"
#include "config_service.h"
#include "can_manager.h"

SerialConsole::SerialConsole() { init(); }

void SerialConsole::init() {
    ptrBuffer = 0;
    state = STATE_ROOT_MENU;
}

void SerialConsole::printMenu() {
    // The CLI is a text-only interface; if USB is currently busy carrying
    // binary GVRET there is no point emitting a menu (and it would corrupt
    // the protocol stream).
    if (!serialTextAllowed()) return;
    Serial.println();
    Serial.println("=== SDTS V2 Sniffer CLI ===");
    Serial.print("Build: "); Serial.println(CFG_BUILD_NUM);
    Serial.println();
    Serial.println("Short commands:");
    Serial.println("  h, ?      Show this menu");
    Serial.println("  i         Show current configuration");
    Serial.println("  R         Factory reset (reboot afterwards)");
    Serial.println("  A         Apply configuration to running hardware");
    Serial.println("  r         Reboot");
    Serial.println();
    Serial.println("Config commands (key=value):");
    Serial.println("  TRANSPORT=<0|1|2>      0=USB, 1=WIFI, 2=BOTH");
    Serial.println("  USBBAUD=<baud>         USB UART baud (e.g. 115200, 921600)");
    Serial.println("  SERMODE=<0|1>          0=TEXT, 1=BINARY");
    Serial.println("  CANSEL=<0|1|2|3>       0=NONE, 1=CAN0, 2=CAN1, 3=BOTH");
    Serial.println("  CAN0SPEED=<baud>       CAN0 bitrate (Hz)");
    Serial.println("  CAN1SPEED=<baud>       CAN1 bitrate (Hz)");
    Serial.println("  CAN0LO=<0|1>           CAN0 listen-only");
    Serial.println("  CAN1LO=<0|1>           CAN1 listen-only");
    Serial.println("  WIFIMODE=<0|1|2>       0=OFF, 1=STA, 2=AP");
    Serial.println("  SSID=<string>          WiFi SSID");
    Serial.println("  WPA2KEY=<string>       WiFi password");
    Serial.println("  LOGLEVEL=<0..4>        0=Debug, 1=Info, 2=Warn, 3=Error, 4=Off");
    Serial.println();
    Serial.println("CAN send:");
    Serial.println("  CAN0SEND=<id>,<len>,<b0>,<b1>,... e.g. CAN0SEND=0x200,4,1,2,3,4");
    Serial.println("  CAN1SEND=<id>,<len>,<b0>,<b1>,...");
    Serial.println();
    Serial.println("Web UI: browse to http://<device-ip>/ when WiFi is connected.");
    Serial.println();
}

void SerialConsole::printStatus() {
    if (!serialTextAllowed()) return;
    const char* xportNames[] = {"USB", "WIFI", "BOTH"};
    const char* serNames[]   = {"TEXT", "BINARY"};
    const char* canSelNames[] = {"NONE", "CAN0", "CAN1", "BOTH"};
    const char* wifiNames[]  = {"OFF", "STA", "AP"};

    Serial.println("--- current config ---");
    Serial.printf("  transport : %s\n", xportNames[settings.activeTransport % 3]);
    Serial.printf("  usbBaud   : %u\n", settings.usbBaudrate);
    Serial.printf("  serMode   : %s\n", serNames[settings.serialMode % 2]);
    Serial.printf("  canSel    : %s\n", canSelNames[settings.canSelection % 4]);
    Serial.printf("  CAN0      : %s, %u bps, listenOnly=%d\n",
                  settings.CAN0_Enabled ? "ENABLED" : "disabled",
                  settings.CAN0Speed, settings.CAN0ListenOnly);
    Serial.printf("  CAN1      : %s, %u bps, listenOnly=%d\n",
                  settings.CAN1_Enabled ? "ENABLED" : "disabled",
                  settings.CAN1Speed, settings.CAN1ListenOnly);
    Serial.printf("  WiFi      : mode=%s SSID=\"%s\" connected=%d\n",
                  wifiNames[settings.wifiMode % 3], settings.SSID, SysSettings.isWifiConnected);
    Serial.printf("  logLevel  : %u\n", settings.logLevel);
    Serial.printf("  RX frames : CAN0=%u CAN1=%u\n",
                  canManager.getReceivedFrameCount(0),
                  canManager.getReceivedFrameCount(1));
    Serial.println("----------------------");
}

void SerialConsole::rcvCharacter(uint8_t chr) {
    // When the USB link is dedicated to binary GVRET, unexpected bytes that
    // aren't consumed by the GVRET state machine would otherwise land here
    // and corrupt an in-progress command buffer. Silently drop them so
    // SavvyCAN's traffic doesn't trigger bogus CLI output.
    if (!serialTextAllowed()) { ptrBuffer = 0; return; }

    if (chr == 10 || chr == 13) {
        handleConsoleCmd();
        ptrBuffer = 0;
    } else {
        cmdBuffer[ptrBuffer++] = (unsigned char)chr;
        if (ptrBuffer > 79) ptrBuffer = 79;
    }
}

void SerialConsole::handleConsoleCmd() {
    if (state != STATE_ROOT_MENU) return;
    if (ptrBuffer == 0) return;
    if (ptrBuffer == 1) { handleShortCmd(); return; }

    bool equalSign = false;
    for (int i = 0; i < ptrBuffer; i++) if (cmdBuffer[i] == '=') { equalSign = true; break; }
    cmdBuffer[ptrBuffer] = 0;
    if (equalSign) handleConfigCmd();
    else Logger::console("Unknown command. Press 'h' for help.");
    ptrBuffer = 0;
}

void SerialConsole::handleShortCmd() {
    switch (cmdBuffer[0]) {
    case 'h':
    case '?':
    case 'H':
        printMenu();
        break;
    case 'i':
    case 'I':
        printStatus();
        break;
    case 'R':
        Logger::console("Factory reset requested. Rebooting in 2s.");
        ConfigService::factoryReset();
        delay(2000);
        ESP.restart();
        break;
    case 'A':
    case 'a':
        Logger::console("Applying runtime config...");
        ConfigService::applyRuntime();
        break;
    case 'r':
        Logger::console("Rebooting...");
        delay(500);
        ESP.restart();
        break;
    default:
        Logger::console("Unknown short command. Press 'h' for help.");
        break;
    }
}

void SerialConsole::handleConfigCmd() {
    if (ptrBuffer < 3) return;
    cmdBuffer[ptrBuffer] = 0;

    String cmdString;
    int i = 0;
    while (cmdBuffer[i] != '=' && i < ptrBuffer) cmdString.concat(String(cmdBuffer[i++]));
    i++;
    if (i >= ptrBuffer) { Logger::console("missing value"); return; }

    long newValue   = strtol((char *)(cmdBuffer + i), NULL, 0);
    char *newString = (char *)(cmdBuffer + i);

    cmdString.toUpperCase();
    bool writeEE = false;

    if (cmdString == "TRANSPORT") {
        if (newValue < 0 || newValue > 2) { Logger::console("invalid transport"); return; }
        settings.activeTransport = (uint8_t)newValue;
        writeEE = true;
    } else if (cmdString == "USBBAUD") {
        if (!ConfigService::isValidUsbBaudrate((uint32_t)newValue)) {
            Logger::console("invalid usb baudrate");
            return;
        }
        settings.usbBaudrate = (uint32_t)newValue;
        writeEE = true;
    } else if (cmdString == "SERMODE") {
        if (newValue < 0 || newValue > 1) { Logger::console("invalid serMode"); return; }
        settings.serialMode = (uint8_t)newValue;
        writeEE = true;
    } else if (cmdString == "CANSEL") {
        if (newValue < 0 || newValue > 3) { Logger::console("invalid canSel"); return; }
        settings.canSelection = (uint8_t)newValue;
        writeEE = true;
    } else if (cmdString == "CAN0SPEED") {
        if (!ConfigService::isValidCanSpeed((uint32_t)newValue)) { Logger::console("invalid CAN0 speed"); return; }
        settings.CAN0Speed = (uint32_t)newValue;
        writeEE = true;
    } else if (cmdString == "CAN1SPEED") {
        if (!ConfigService::isValidCanSpeed((uint32_t)newValue)) { Logger::console("invalid CAN1 speed"); return; }
        settings.CAN1Speed = (uint32_t)newValue;
        writeEE = true;
    } else if (cmdString == "CAN0LO") {
        settings.CAN0ListenOnly = (newValue != 0);
        writeEE = true;
    } else if (cmdString == "CAN1LO") {
        settings.CAN1ListenOnly = (newValue != 0);
        writeEE = true;
    } else if (cmdString == "WIFIMODE") {
        if (newValue < 0 || newValue > 2) { Logger::console("invalid wifiMode"); return; }
        settings.wifiMode = (uint8_t)newValue;
        writeEE = true;
    } else if (cmdString == "SSID") {
        strncpy(settings.SSID, newString, sizeof(settings.SSID) - 1);
        settings.SSID[sizeof(settings.SSID) - 1] = 0;
        writeEE = true;
    } else if (cmdString == "WPA2KEY") {
        strncpy(settings.WPA2Key, newString, sizeof(settings.WPA2Key) - 1);
        settings.WPA2Key[sizeof(settings.WPA2Key) - 1] = 0;
        writeEE = true;
    } else if (cmdString == "LOGLEVEL") {
        if (newValue < 0 || newValue > 4) { Logger::console("invalid loglevel"); return; }
        settings.logLevel = (uint8_t)newValue;
        Logger::setLoglevel((Logger::LogLevel)settings.logLevel);
        writeEE = true;
    } else if (cmdString == "CAN0SEND") {
        handleCANSend(CAN0, newString);
    } else if (cmdString == "CAN1SEND") {
        handleCANSend(CAN1, newString);
    } else {
        Logger::console("Unknown command");
        return;
    }

    if (writeEE) {
        ConfigService::save();
        ConfigService::applyRuntime();
        Logger::console("OK");
    }
}

bool SerialConsole::handleCANSend(CAN_COMMON &port, char *inputString) {
    char *idTok  = strtok(inputString, ",");
    char *lenTok = strtok(NULL, ",");
    char *dataTok;
    CAN_FRAME frame;

    if (!idTok || !lenTok) return false;

    int idVal  = strtol(idTok, NULL, 0);
    int lenVal = strtol(lenTok, NULL, 0);
    if (lenVal < 0 || lenVal > 8) return false;

    for (int i = 0; i < lenVal; i++) {
        dataTok = strtok(NULL, ",");
        if (!dataTok) return false;
        frame.data.byte[i] = strtol(dataTok, NULL, 0);
    }
    frame.id = idVal;
    frame.extended = (idVal >= 0x7FF);
    frame.rtr = 0;
    frame.length = lenVal;
    canManager.sendFrame(&port, frame);
    Logger::console("Sent frame id=0x%x len=%i", frame.id, frame.length);
    return true;
}
