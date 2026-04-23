/*
 * CAN_GVRET_Bridge - GVRET-compatible CAN sniffer for ESP32
 *
 * File:    src/web_config.cpp
 * Author:  Ahmed Ellamiee <ahmed.ellamiee@gmail.com>
 * Copyright (c) 2026 Ahmed Ellamiee
 */

#include "web_config.h"
#include "config_service.h"
#include "can_manager.h"
#include <WiFi.h>
#include <Arduino.h>

WebConfigServer webConfig;

// Embedded HTML page - kept intentionally small and self-contained so we
// don't need SPIFFS / LittleFS. Served as a PROGMEM string to save RAM.
static const char kIndexHtml[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>CAN_GVRET_Bridge - Config</title>
<style>
  :root { --bg:#0f1115; --card:#171a21; --fg:#eaeef5; --muted:#8892a6; --accent:#4aa3ff; --ok:#3ddc97; --err:#ff5c7a; }
  * { box-sizing: border-box; }
  body { margin:0; font-family: system-ui, Segoe UI, Roboto, sans-serif; background:var(--bg); color:var(--fg); }
  header { padding:20px 24px; background:linear-gradient(135deg,#1a1f2b,#0f1115); border-bottom:1px solid #252a36; }
  header h1 { margin:0; font-size:1.3rem; letter-spacing:.3px; }
  header .sub { color:var(--muted); font-size:.85rem; margin-top:4px; }
  main { max-width:900px; margin:24px auto; padding:0 16px; display:grid; gap:16px; grid-template-columns: 1fr 1fr; }
  .card { background:var(--card); border:1px solid #252a36; border-radius:10px; padding:18px; }
  .card h2 { margin:0 0 12px; font-size:1rem; color:var(--accent); }
  .row { display:flex; justify-content:space-between; align-items:center; padding:6px 0; gap:10px; }
  .row label { color:var(--muted); font-size:.9rem; }
  input, select { background:#0b0d12; color:var(--fg); border:1px solid #2b3140; border-radius:6px; padding:6px 8px; font-size:.9rem; min-width:150px; }
  input[type=checkbox] { min-width:auto; }
  button { background:var(--accent); color:#06121e; border:0; border-radius:6px; padding:10px 16px; font-weight:600; cursor:pointer; }
  button.secondary { background:#2b3140; color:var(--fg); }
  button.danger { background:var(--err); color:#240008; }
  .actions { grid-column:1 / -1; display:flex; gap:10px; flex-wrap:wrap; justify-content:flex-end; }
  .status { grid-column:1 / -1; font-size:.85rem; color:var(--muted); }
  .ok { color:var(--ok); } .err { color:var(--err); }
  @media (max-width:700px){ main { grid-template-columns:1fr; } }
</style>
</head>
<body>
<header>
  <h1>CAN_GVRET_Bridge</h1>
  <div class="sub">Web configuration console</div>
</header>
<main>
  <section class="card">
    <h2>Transport</h2>
    <div class="row"><label>Active transport</label>
      <select id="transport">
        <option value="0">USB GVRET</option>
        <option value="1">WiFi GVRET</option>
        <option value="2">Both</option>
      </select>
    </div>
    <div class="row"><label>USB baudrate</label>
      <select id="usbBaud">
        <option>9600</option><option>19200</option><option>38400</option>
        <option>57600</option><option>115200</option><option>230400</option>
        <option>460800</option><option>921600</option>
        <option>1500000</option><option>2000000</option>
      </select>
    </div>
    <div class="row"><label>Serial mode</label>
      <select id="serMode"><option value="0">Text</option><option value="1">Binary</option></select>
    </div>
  </section>

  <section class="card">
    <h2>CAN channels</h2>
    <div class="row"><label>Enabled</label>
      <select id="canSel">
        <option value="0">None</option>
        <option value="1">CAN0 only</option>
        <option value="2">CAN1 only</option>
        <option value="3">Both</option>
      </select>
    </div>
    <div class="row"><label>CAN0 speed (bps)</label>
      <input id="can0Speed" type="number" min="10000" max="1000000" step="1000">
    </div>
    <div class="row"><label>CAN0 listen only</label><input id="can0Lo" type="checkbox"></div>
    <div class="row"><label>CAN1 speed (bps)</label>
      <input id="can1Speed" type="number" min="10000" max="1000000" step="1000">
    </div>
    <div class="row"><label>CAN1 listen only</label><input id="can1Lo" type="checkbox"></div>
  </section>

  <section class="card">
    <h2>WiFi</h2>
    <div class="row"><label>Mode</label>
      <select id="wifiMode">
        <option value="0">Off</option><option value="1">Station</option><option value="2">Access Point</option>
      </select>
    </div>
    <div class="row"><label>SSID</label><input id="ssid" maxlength="31"></div>
    <div class="row"><label>Password</label><input id="wpa2" maxlength="63"></div>
  </section>

  <section class="card">
    <h2>Status</h2>
    <div class="row"><label>IP address</label><span id="ip">-</span></div>
    <div class="row"><label>RX CAN0</label><span id="rx0">0</span></div>
    <div class="row"><label>RX CAN1</label><span id="rx1">0</span></div>
    <div class="row"><label>Build</label><span id="build">-</span></div>
  </section>

  <div class="actions">
    <button id="saveBtn">Save &amp; Apply</button>
    <button id="reloadBtn" class="secondary">Reload</button>
    <button id="rebootBtn" class="secondary">Reboot</button>
    <button id="resetBtn" class="danger">Factory Reset</button>
  </div>
  <div class="status" id="status"></div>
</main>
<script>
async function load(){
  const r = await fetch('/config'); const j = await r.json();
  transport.value = j.activeTransport;
  usbBaud.value = j.usbBaudrate;
  serMode.value = j.serialMode;
  canSel.value = j.canSelection;
  can0Speed.value = j.CAN0Speed;
  can1Speed.value = j.CAN1Speed;
  can0Lo.checked = j.CAN0ListenOnly;
  can1Lo.checked = j.CAN1ListenOnly;
  wifiMode.value = j.wifiMode;
  ssid.value = j.SSID; wpa2.value = j.WPA2Key;
  ip.textContent = j.ip; rx0.textContent = j.rxCan0; rx1.textContent = j.rxCan1;
  build.textContent = j.build;
}
async function save(){
  const body = {
    activeTransport:+transport.value, usbBaudrate:+usbBaud.value, serialMode:+serMode.value,
    canSelection:+canSel.value, CAN0Speed:+can0Speed.value, CAN1Speed:+can1Speed.value,
    CAN0ListenOnly:can0Lo.checked, CAN1ListenOnly:can1Lo.checked,
    wifiMode:+wifiMode.value, SSID:ssid.value, WPA2Key:wpa2.value
  };
  const r = await fetch('/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
  const t = await r.text();
  status.className = 'status ' + (r.ok ? 'ok' : 'err');
  status.textContent = (r.ok ? 'Saved: ' : 'Error: ') + t;
  if (r.ok) load();
}
saveBtn.onclick = save;
reloadBtn.onclick = load;
rebootBtn.onclick = async () => { await fetch('/reboot',{method:'POST'}); status.textContent='Rebooting...'; };
resetBtn.onclick = async () => {
  if (!confirm('Factory reset all settings?')) return;
  await fetch('/reset',{method:'POST'}); status.textContent='Factory reset, rebooting...';
};
load();
</script>
</body></html>
)HTML";

WebConfigServer::WebConfigServer() : server(80), running(false), rebootRequested(false), rebootAt(0) {}

void WebConfigServer::begin() {
    if (running) return;
    server.on("/",          HTTP_GET,  std::bind(&WebConfigServer::handleRoot,       this));
    server.on("/config",    HTTP_GET,  std::bind(&WebConfigServer::handleGetConfig,  this));
    server.on("/config",    HTTP_POST, std::bind(&WebConfigServer::handlePostConfig, this));
    server.on("/reboot",    HTTP_POST, std::bind(&WebConfigServer::handleReboot,     this));
    server.on("/reset",     HTTP_POST, std::bind(&WebConfigServer::handleReset,      this));
    server.onNotFound(std::bind(&WebConfigServer::handleNotFound, this));
    server.begin();
    running = true;
    if (serialTextAllowed()) Serial.println("[web] config server started on :80");
}

void WebConfigServer::loop() {
    if (!running) return;
    server.handleClient();
    if (rebootRequested && (int32_t)(millis() - rebootAt) >= 0) {
        if (serialTextAllowed()) Serial.println("[web] rebooting now");
        delay(100);
        ESP.restart();
    }
}

void WebConfigServer::handleRoot() {
    server.send_P(200, "text/html", kIndexHtml);
}

// Escape a JSON string value in-place into `out`, stopping at `outLen-1`.
static size_t jsonEscape(const char *in, char *out, size_t outLen) {
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 2 < outLen; i++) {
        char c = in[i];
        if (c == '"' || c == '\\') { out[o++] = '\\'; out[o++] = c; }
        else if (c == '\n') { out[o++] = '\\'; out[o++] = 'n'; }
        else if ((unsigned char)c < 0x20) { continue; }
        else out[o++] = c;
    }
    out[o] = 0;
    return o;
}

void WebConfigServer::handleGetConfig() {
    char ssidEsc[64]; char keyEsc[128];
    jsonEscape(settings.SSID,    ssidEsc, sizeof(ssidEsc));
    jsonEscape(settings.WPA2Key, keyEsc,  sizeof(keyEsc));

    IPAddress ip = (WiFi.getMode() & WIFI_AP) ? WiFi.softAPIP() : WiFi.localIP();
    char ipBuf[20]; snprintf(ipBuf, sizeof(ipBuf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);

    char buf[768];
    int n = snprintf(buf, sizeof(buf),
        "{"
            "\"activeTransport\":%u,"
            "\"usbBaudrate\":%u,"
            "\"serialMode\":%u,"
            "\"canSelection\":%u,"
            "\"CAN0Speed\":%u,"
            "\"CAN1Speed\":%u,"
            "\"CAN0ListenOnly\":%s,"
            "\"CAN1ListenOnly\":%s,"
            "\"wifiMode\":%u,"
            "\"SSID\":\"%s\","
            "\"WPA2Key\":\"%s\","
            "\"ip\":\"%s\","
            "\"rxCan0\":%u,"
            "\"rxCan1\":%u,"
            "\"build\":%u"
        "}",
        settings.activeTransport, settings.usbBaudrate, settings.serialMode,
        settings.canSelection, settings.CAN0Speed, settings.CAN1Speed,
        settings.CAN0ListenOnly ? "true" : "false",
        settings.CAN1ListenOnly ? "true" : "false",
        settings.wifiMode, ssidEsc, keyEsc, ipBuf,
        canManager.getReceivedFrameCount(0), canManager.getReceivedFrameCount(1),
        (unsigned)CFG_BUILD_NUM);
    (void)n;
    server.send(200, "application/json", buf);
}

// Minimal JSON parser for the fields we expect. Not a full implementation -
// enough for the flat object the web UI sends.
static bool jsonGetInt(const String &body, const char *key, long &out) {
    int k = body.indexOf(String("\"") + key + "\"");
    if (k < 0) return false;
    int colon = body.indexOf(':', k);
    if (colon < 0) return false;
    int i = colon + 1;
    while (i < (int)body.length() && (body[i] == ' ' || body[i] == '\t')) i++;
    int start = i;
    while (i < (int)body.length() && body[i] != ',' && body[i] != '}') i++;
    out = body.substring(start, i).toInt();
    return true;
}
static bool jsonGetBool(const String &body, const char *key, bool &out) {
    int k = body.indexOf(String("\"") + key + "\"");
    if (k < 0) return false;
    int colon = body.indexOf(':', k);
    if (colon < 0) return false;
    int t = body.indexOf("true",  colon);
    int f = body.indexOf("false", colon);
    int end = body.indexOf(',', colon); if (end < 0) end = body.indexOf('}', colon);
    if (t >= 0 && (end < 0 || t < end)) { out = true;  return true; }
    if (f >= 0 && (end < 0 || f < end)) { out = false; return true; }
    return false;
}
static bool jsonGetString(const String &body, const char *key, char *out, size_t outLen) {
    int k = body.indexOf(String("\"") + key + "\"");
    if (k < 0) return false;
    int colon = body.indexOf(':', k);
    if (colon < 0) return false;
    int q1 = body.indexOf('"', colon);
    if (q1 < 0) return false;
    int q2 = body.indexOf('"', q1 + 1);
    if (q2 < 0) return false;
    String v = body.substring(q1 + 1, q2);
    strncpy(out, v.c_str(), outLen - 1);
    out[outLen - 1] = 0;
    return true;
}

void WebConfigServer::handlePostConfig() {
    if (!server.hasArg("plain")) {
        server.send(400, "text/plain", "expected JSON body");
        return;
    }
    String body = server.arg("plain");
    long lv;

    if (jsonGetInt(body, "activeTransport", lv)) {
        if (lv < 0 || lv > 2) { server.send(400, "text/plain", "invalid activeTransport"); return; }
        settings.activeTransport = (uint8_t)lv;
    }
    if (jsonGetInt(body, "usbBaudrate", lv)) {
        if (!ConfigService::isValidUsbBaudrate((uint32_t)lv)) { server.send(400, "text/plain", "invalid usbBaudrate"); return; }
        settings.usbBaudrate = (uint32_t)lv;
    }
    if (jsonGetInt(body, "serialMode", lv)) {
        if (lv < 0 || lv > 1) { server.send(400, "text/plain", "invalid serialMode"); return; }
        settings.serialMode = (uint8_t)lv;
    }
    if (jsonGetInt(body, "canSelection", lv)) {
        if (lv < 0 || lv > 3) { server.send(400, "text/plain", "invalid canSelection"); return; }
        settings.canSelection = (uint8_t)lv;
    }
    if (jsonGetInt(body, "CAN0Speed", lv)) {
        if (!ConfigService::isValidCanSpeed((uint32_t)lv)) { server.send(400, "text/plain", "invalid CAN0Speed"); return; }
        settings.CAN0Speed = (uint32_t)lv;
    }
    if (jsonGetInt(body, "CAN1Speed", lv)) {
        if (!ConfigService::isValidCanSpeed((uint32_t)lv)) { server.send(400, "text/plain", "invalid CAN1Speed"); return; }
        settings.CAN1Speed = (uint32_t)lv;
    }
    bool bv;
    if (jsonGetBool(body, "CAN0ListenOnly", bv)) settings.CAN0ListenOnly = bv;
    if (jsonGetBool(body, "CAN1ListenOnly", bv)) settings.CAN1ListenOnly = bv;
    if (jsonGetInt(body, "wifiMode", lv)) {
        if (lv < 0 || lv > 2) { server.send(400, "text/plain", "invalid wifiMode"); return; }
        settings.wifiMode = (uint8_t)lv;
    }
    jsonGetString(body, "SSID",    settings.SSID,    sizeof(settings.SSID));
    jsonGetString(body, "WPA2Key", settings.WPA2Key, sizeof(settings.WPA2Key));

    ConfigService::save();
    ConfigService::applyRuntime();
    server.send(200, "text/plain", "saved");
}

void WebConfigServer::handleReboot() {
    server.send(200, "text/plain", "rebooting");
    rebootRequested = true;
    rebootAt = millis() + 500;
}

void WebConfigServer::handleReset() {
    ConfigService::factoryReset();
    server.send(200, "text/plain", "reset");
    rebootRequested = true;
    rebootAt = millis() + 500;
}

void WebConfigServer::handleNotFound() {
    server.send(404, "text/plain", "not found");
}
