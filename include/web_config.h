/*
 * SDTS V2 Sniffer - GVRET-compatible CAN sniffer for ESP32
 *
 * File:    include/web_config.h
 * Author:  Ahmed Ellamiee <ahmed.ellamiee@gmail.com>
 * Copyright (c) 2026 Ahmed Ellamiee
 */

#pragma once
#include <WebServer.h>
#include "config.h"

// Lightweight HTTP server exposing:
//   GET  /           - HTML configuration page
//   GET  /config     - JSON dump of current settings + runtime status
//   POST /config     - JSON body with fields to update; persists + applies
//   POST /reboot     - trigger a deferred reboot
//   POST /reset      - factory reset + reboot
class WebConfigServer {
public:
    WebConfigServer();
    // Start the embedded HTTP server on port 80. Safe to call multiple times.
    void begin();
    // Must be polled (e.g. from the wifi worker task) to service requests.
    void loop();
    bool isRunning() const { return running; }

private:
    WebServer server;
    bool running;
    bool rebootRequested;
    uint32_t rebootAt;

    void handleRoot();
    void handleGetConfig();
    void handlePostConfig();
    void handleReboot();
    void handleReset();
    void handleNotFound();
};

extern WebConfigServer webConfig;
