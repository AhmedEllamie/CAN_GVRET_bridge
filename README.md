CAN_GVRET_Bridge
===============

CAN sniffer firmware for an ESP32 with an onboard TWAI transceiver (CAN0)
and an external MCP2517FD SPI controller (CAN1). Exposes the GVRET
protocol over USB and/or WiFi so tools like SavvyCAN can capture and
inject frames.

This is a revamp of the original ESP32RET / A0RET firmware, trimmed down
to focus on the sniffer workflow and reorganized around FreeRTOS tasks
plus a built-in web configuration page.

Features
--------
- Web-based configuration page (served over the device's WiFi).
- USB serial CLI for headless configuration.
- Selectable GVRET transport: USB, WiFi, or both simultaneously.
- Configurable USB baudrate (9600 ... 2000000).
- Text or binary GVRET framing (binary is auto-selected when a host sends
  the 0xE7 sync byte).
- Independent enable/disable/speed/listen-only per CAN channel.
- Runtime reconfiguration without reboot; settings persisted to NVS.
- FreeRTOS task split:
  - `can0Reader`, `can1Reader` on core 0
  - `usbWorker`, `wifiWorker`, `sysMgr` on core 1

Runtime configuration
---------------------

### Web UI
When WiFi is running (AP mode by default), browse to `http://<device-ip>/`.
The page displays the current status and lets you change every persisted
setting. Saving immediately applies the change and writes to NVS.

Web UI behavior:
- Live status section shows active transport, serial mode, selected CAN bus,
  and runtime counters.
- CAN profile fields (`CAN0/CAN1` enable, speed, listen-only) are applied
  without reboot and are reflected immediately in the worker tasks.
- Transport settings (`USB/WIFI/BOTH`) are validated in the UI before POSTing
  to reduce invalid runtime combinations.
- WiFi credentials updates are persisted immediately; if AP/STA mode changes,
  reconnect to the new IP after save.
- Factory reset clears persisted config and reboots into defaults.

API:
- `GET  /config` - JSON dump of current settings and runtime counters.
- `POST /config` - JSON body of the settings to update.
- `POST /reboot` - deferred reboot.
- `POST /reset`  - factory reset + reboot.

Example `POST /config` payload:
```json
{
  "transport": 2,
  "serMode": 1,
  "canSel": 3,
  "can0Speed": 500000,
  "can1Speed": 250000,
  "wifiMode": 2
}
```

### USB CLI
Connect at the configured baud (default 921600) and press `h` for help.
Commands follow `KEY=value` form; examples:

```
TRANSPORT=2          # 0=USB, 1=WIFI, 2=BOTH
USBBAUD=115200
SERMODE=1            # 0=TEXT, 1=BINARY
CANSEL=3             # 0=NONE, 1=CAN0, 2=CAN1, 3=BOTH
CAN0SPEED=500000
CAN1SPEED=250000
WIFIMODE=2           # 0=OFF, 1=STA, 2=AP
SSID=MyCar
WPA2KEY=supersecret
```

Default AP credentials are `CAN_GVRET_Bridge` / `CAN_GVRET_Bridge`; change them
as soon as possible.

Architecture overview
---------------------

```
           +-----------------+                 +-----------------+
USB host   |  usbWorker task |<-- Serial RX -->|  SerialConsole  |
(SavvyCAN) |   (core 1)      |                 +-----------------+
           |                 |<-----------+
           +------^----------+            |
                  |                       |
                  | drains                |
              +---+----+             +----+----+
              | serial |             |  wifi   |
              | GVRET  |             |  GVRET  |
              | buffer |             | buffer  |
              +---^----+             +----^----+
                  |                       |
                  | writes                | writes
           +------+----------+     +------+--------------+
           | can0ReaderTask  |     | can1ReaderTask      |
           | (core 0, pri 5) |     | (core 0, pri 5)     |
           +-----------------+     +---------------------+
                  |                       |
                  | routes via            | routes via
                  | CANManager            | CANManager
                  v                       v
                CAN0 (TWAI)            CAN1 (MCP2517FD)
```

The `wifiWorker` task handles WiFi association, telnet clients on port 23
(WiFi GVRET), and the HTTP configuration server on port 80. The
`sysMgr` task runs periodic bus-load accounting and heartbeat logs.

Hardware pinout
---------------
- CAN0 TWAI: RX=GPIO32, TX=GPIO33
- CAN1 MCP2517FD: CS=GPIO5, INT=GPIO21, SCK=GPIO18, MISO=GPIO19, MOSI=GPIO23
- Status LED: GPIO2 (WS2812)

Building
--------
```
pio run -e esp32dev
pio run -e esp32dev -t upload
pio device monitor -b 921600
```

License
-------
MIT - see `LICENSE`.
