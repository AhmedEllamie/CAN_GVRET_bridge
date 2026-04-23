/*
 * CAN_GVRET_Bridge - GVRET-compatible CAN sniffer for ESP32
 *
 * File:    include/config_service.h
 * Author:  Ahmed Ellamiee <ahmed.ellamiee@gmail.com>
 * Copyright (c) 2026 Ahmed Ellamiee
 */

#pragma once
#include <Arduino.h>
#include "config.h"

// Central load/save/apply helpers for persistent settings.
// Web UI and USB CLI both call into this module so validation and
// the runtime-apply sequence stay in a single place.
class ConfigService {
public:
    // Read all settings from NVS (or set safe defaults on first boot).
    static void load();

    // Write the current in-memory settings to NVS.
    static void save();

    // Reset NVS to factory defaults (caller should reboot afterwards).
    static void factoryReset();

    // Apply the current settings to running hardware without rebooting.
    // Returns true if all requested changes were applied successfully.
    // Reinitializes CAN controllers and reconfigures the USB UART baud rate.
    static bool applyRuntime();

    // Validators used by the web API and CLI.
    static bool isValidCanSpeed(uint32_t speed);
    static bool isValidUsbBaudrate(uint32_t baud);
};
