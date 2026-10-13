// Read-only EEPROM dumper for Daikin boards with an X41A service port.
//
// This firmware can put only two frames on the Daikin line, both read-only:
//   04 49 lo hi crc  ->  49 dd crc          1-byte EEPROM read (Probe, Dump)
//   04 52 80 06 crc  ->  52 d0..d15 crc     16-byte RAM read at the fixed address 0x0680, where the
//                                           firmware ID is (e.g. "DP0009_003"); only when the user
//                                           ticks the option on the page
// (crc = 0xFF - sum of the previous bytes)
// There is no generic "send these bytes" path on purpose. Do not add one: other commands erase
// or overwrite the controller's memory, and reading its register areas can clear status bits.
// Nothing is sent until you press Probe or Dump.
//
// Wi-Fi:
//   - station mode only: join the home Wi-Fi (setup.h, or the one already saved on the ESP) and
//     wait up to 60 s; a home Wi-Fi that is lost for 60 s restarts the ESP
//   - only if the home Wi-Fi cannot be joined, the ESP opens its own Wi-Fi (AP_SSID, page at
//     http://192.168.4.1); when nobody joins it for 5 min, the ESP restarts and tries again.
//     The ESP8266 has one radio, so it never searches for the home Wi-Fi while its own Wi-Fi is on.
//   - a finished dump that was not downloaded yet is never lost to a restart: the ESP keeps it in
//     RAM and opens its own Wi-Fi instead
//
// Going back to the previous firmware: OTA as usual, or upload its .bin on
// the page /update.

#include <Arduino.h>
#include <ArduinoOTA.h>

#ifdef ARDUINO_ARCH_ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <SoftwareSerial.h>
SoftwareSerial Daikin;
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer updatePage;
#else
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPUpdateServer.h>
HardwareSerial Daikin(2);
WebServer server(80);
HTTPUpdateServer updatePage;
#endif

#include "setup.h"

static const uint16_t EEPROM_SIZE = 0x400;
static const uint32_t REPLY_TIMEOUT_MS = 500;
static const uint32_t READ_GAP_MS = 30; // pause between reads so the board is not flooded
static const int RETRIES = 3;

static const uint32_t STA_WAIT_MS = 60000;
static const uint32_t AP_IDLE_RESTART_MS = 300000; // own Wi-Fi with nobody on it

enum State { IDLE, PROBING, DUMPING, DONE };
static State state = IDLE;
static uint8_t image[EEPROM_SIZE];
static bool valid[EEPROM_SIZE];
static uint16_t pos = 0;
static int okCount = 0, failCount = 0, badPairs = 0;
static bool downloaded = false;
static bool wantFirmwareId = false;
static String firmwareId; // empty: not read, or the board did not answer
static String message = "Ready. Press Probe first.";

static bool apMode = false;
static uint32_t lastConnected = 0, apIdleSince = 0;

// ---------------------------------------------------------------- Wi-Fi

// Station mode only, then wait up to 60 s.
static bool joinHomeWifi()
{
#ifdef ARDUINO_ARCH_ESP8266
  WiFi.mode(WIFI_STA);
  WiFi.hostname(HOSTNAME);
#else
  WiFi.setHostname(HOSTNAME);
  WiFi.mode(WIFI_STA);
#endif
  if (strlen(WIFI_SSID) > 0)
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD, 0, 0, true);
  else
    WiFi.begin(); // the Wi-Fi already saved on the ESP
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < STA_WAIT_MS)
    delay(500);
  return WiFi.status() == WL_CONNECTED;
}

static void openOwnWifi()
{
  WiFi.persistent(false); // do not save the own Wi-Fi in flash, so the next firmware does not inherit it
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  apMode = true;
  apIdleSince = millis();
}

static void superviseWifi()
{
  bool busy = state == PROBING || state == DUMPING;
  bool keepDump = state == DONE && !downloaded; // a restart would lose it
  if (apMode)
  {
    if (WiFi.softAPgetStationNum() > 0)
      apIdleSince = millis();
    else if (!busy && !keepDump && millis() - apIdleSince > AP_IDLE_RESTART_MS)
      ESP.restart(); // nobody came: try the home Wi-Fi again
    return;
  }
  if (WiFi.status() == WL_CONNECTED)
    lastConnected = millis();
  else if (!busy && millis() - lastConnected > STA_WAIT_MS)
  {
    if (keepDump)
      openOwnWifi();
    else
      ESP.restart();
  }
}

// ---------------------------------------------------------------- Daikin EEPROM read

static uint8_t crc(const uint8_t *buf, int len)
{
  uint8_t sum = 0;
  for (int i = 0; i < len; i++)
    sum += buf[i];
  return (uint8_t)~sum;
}

// Returns true and fills *value when the board answered "49 dd crc" with a valid checksum.
static bool readByte(uint16_t addr, uint8_t *value)
{
  uint8_t frame[5] = {0x04, 0x49, (uint8_t)(addr & 0xFF), (uint8_t)(addr >> 8), 0};
  frame[4] = crc(frame, 4);

  while (Daikin.available())
    Daikin.read(); // drop leftovers of an earlier reply
  Daikin.write(frame, sizeof(frame));

  uint8_t reply[3];
  int len = 0;
  uint32_t start = millis();
  while (len < 3 && millis() - start < REPLY_TIMEOUT_MS)
  {
    if (Daikin.available())
    {
      reply[len++] = Daikin.read();
      if (len == 2 && reply[0] == 0x15 && reply[1] == 0xEA)
        return false; // NAK: the board does not support this command
    }
    yield();
  }
  if (len != 3 || reply[0] != 0x49 || crc(reply, 2) != reply[2])
    return false;
  *value = reply[1];
  return true;
}

static bool readWithRetry(uint16_t addr, uint8_t *value)
{
  for (int i = 0; i < RETRIES; i++)
  {
    if (readByte(addr, value))
      return true;
    server.handleClient(); // keep the page responsive between retries
    delay(300);
  }
  return false;
}

// Optional: the 16-byte RAM read at the fixed address 0x0680 (see the top of this file). Returns
// the firmware ID, e.g. "DP0009_003", or "" when the board does not answer or holds no ID there.
static String readFirmwareId()
{
  uint8_t frame[5] = {0x04, 0x52, 0x80, 0x06, 0}; // 'R', address low byte first
  frame[4] = crc(frame, 4);
  for (int attempt = 0; attempt < RETRIES; attempt++)
  {
    while (Daikin.available())
      Daikin.read();
    Daikin.write(frame, sizeof(frame));

    uint8_t reply[18];
    int len = 0;
    uint32_t start = millis();
    while (len < 18 && millis() - start < REPLY_TIMEOUT_MS)
    {
      if (Daikin.available())
      {
        reply[len++] = Daikin.read();
        if (len == 2 && reply[0] == 0x15 && reply[1] == 0xEA)
          return ""; // NAK: the board does not support this command
      }
      yield();
    }
    if (len == 18 && reply[0] == 0x52 && crc(reply, 17) == reply[17])
    {
      String id;
      for (int i = 1; i <= 16 && (isalnum(reply[i]) || reply[i] == '_' || reply[i] == '-' || reply[i] == '.'); i++)
        id += (char)reply[i];
      return id.length() >= 4 ? id : String("");
    }
    delay(300);
  }
  return "";
}

// One address per loop() pass, so the web server keeps answering during the dump.
static void step()
{
  if (state == PROBING)
  {
    uint8_t v;
    if (readWithRetry(0, &v))
      message = "Probe OK: address 000 = " + String(v, HEX) + ". You can start the dump.";
    else
      message = "Probe FAILED: no valid reply. Check wiring and GND; this board may not support 0x49.";
    state = IDLE;
  }
  else if (state == DUMPING)
  {
    uint8_t v;
    valid[pos] = readWithRetry(pos, &v);
    delay(READ_GAP_MS);
    if (valid[pos])
    {
      image[pos] = v;
      okCount++;
    }
    else
    {
      failCount++;
      if (pos == 0)
      {
        message = "First read failed, dump stopped. Run Probe and check the wiring.";
        state = IDLE;
        return;
      }
    }
    if (++pos >= EEPROM_SIZE)
    {
      badPairs = 0;
      for (uint16_t a = 0; a < EEPROM_SIZE; a += 2)
        if (!valid[a] || !valid[a + 1] || (image[a] ^ image[a + 1]) != 0xFF)
          badPairs++;
      message = badPairs == 0 ? "Dump complete, all 512 pairs valid. Download the file."
                              : "Dump finished with " + String(badPairs) + " bad pairs. Run Dump again.";
      if (wantFirmwareId)
      {
        firmwareId = readFirmwareId();
        message += firmwareId.length() ? " Firmware: " + firmwareId + "." : String(" Firmware ID could not be read.");
      }
      downloaded = false;
      state = DONE;
    }
  }
}

// ---------------------------------------------------------------- web page

static const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Daikin EEPROM Dump</title>
<style>
body{font-family:system-ui,sans-serif;max-width:560px;margin:0 auto;padding:16px;background:#fff;color:#222}
button{font-size:16px;padding:10px 14px;margin:4px 0;border-radius:6px;border:0;background:#0a66c2;color:#fff}
button:disabled{background:#999}
#bar{height:14px;background:#ddd;border-radius:7px;overflow:hidden}#fill{height:100%;width:0;background:#0a66c2}
#msg{padding:10px;background:#f2f2f2;border-radius:6px;margin:12px 0}
.warn{font-size:14px;color:#a00}.small{font-size:13px;color:#555}
</style></head><body>
<h2>Daikin X41A EEPROM dump</h2>
<p class="warn">Read-only: this device only sends the 0x49 EEPROM read command (and, if ticked below, one RAM read).</p>
<div id="msg">...</div>
<div id="bar"><div id="fill"></div></div>
<p id="count"></p>
<button id="probe" onclick="post('/probe')">Probe</button>
<button id="dump" onclick="post('/dump?fw='+($('fw').checked?1:0))">Dump (a few minutes)</button>
<p class="small"><label><input type="checkbox" id="fw"> After the dump, also read the board's firmware
version (one extra read-only command: 16 bytes of RAM at 0x0680)</label></p>
<p><a id="dl" href="/dump.tsv" style="display:none"><button>Download .tsv</button></a></p>
<p id="dev" class="small"></p>
<p class="small">Done? Go back to your previous firmware: <a href="/update">upload its .bin</a>, or use OTA.</p>
<script>
function $(i){return document.getElementById(i)}
function post(u){fetch(u,{method:'POST'}).then(poll)}
function poll(){fetch('/status').then(r=>r.json()).then(s=>{
$('msg').textContent=s.message;
$('fill').style.width=(s.pos*100/1024)+'%';
$('count').textContent='Read '+s.pos+' / 1024, failed '+s.fail;
var busy=s.state=='probing'||s.state=='dumping';
$('probe').disabled=busy;$('dump').disabled=busy;
$('dl').style.display=s.state=='done'?'':'none';
$('dev').textContent='Wi-Fi: '+s.wifi;})}
setInterval(poll,2000);poll();
</script></body></html>)HTML";

static String jsonText(String s)
{
  s.replace("\\", "\\\\");
  s.replace("\"", "\\\"");
  return "\"" + s + "\"";
}

static void handleStatus()
{
  const char *names[] = {"idle", "probing", "dumping", "done"};
  String wifi = apMode ? String("own Wi-Fi ") + AP_SSID + ", 192.168.4.1"
                       : WiFi.SSID() + ", " + WiFi.localIP().toString() + ", " + WiFi.RSSI() + " dBm";
  String json = "{\"state\":\"" + String(names[state]) + "\",\"pos\":" + pos + ",\"ok\":" + okCount +
                ",\"fail\":" + failCount + ",\"message\":" + jsonText(message) + ",\"wifi\":" + jsonText(wifi) + "}";
  server.send(200, "application/json", json);
}

static void handleProbe()
{
  if (state != PROBING && state != DUMPING)
  {
    state = PROBING;
    message = "Probing...";
  }
  server.send(204);
}

static void handleDump()
{
  if (state != PROBING && state != DUMPING)
  {
    memset(valid, 0, sizeof(valid));
    pos = 0;
    okCount = failCount = badPairs = 0;
    wantFirmwareId = server.arg("fw") == "1";
    firmwareId = "";
    state = DUMPING;
    message = "Dumping... keep this page open or come back later.";
  }
  server.send(204);
}

// The "AS number" of the EEPROM spec: logical 0x08B-0x08F in BCD, e.g. 01 70 43 96 01 = AS1704396-01.
// Empty when those bytes were not read correctly.
static String asNumber()
{
  uint8_t b[5];
  for (int i = 0; i < 5; i++)
  {
    uint16_t p = 2 * (0x08B + i); // physical address of the value; the complement follows
    if (!valid[p] || !valid[p + 1] || (image[p] ^ image[p + 1]) != 0xFF)
      return "";
    b[i] = image[p];
    if ((b[i] >> 4) > 9 || (b[i] & 0x0F) > 9)
      return "";
  }
  char text[16];
  snprintf(text, sizeof(text), "AS%X%02X%02X%02X-%02X", b[0], b[1], b[2], b[3], b[4]);
  return text;
}

static void handleFile()
{
  if (state != DONE)
  {
    server.send(404, "text/plain", "No dump yet.");
    return;
  }
  String as = asNumber();
  String fw = firmwareId.length() ? firmwareId : String(wantFirmwareId ? "unknown (no reply)" : "not read");
  String out = "# model/board: fill in\n# EEPROM spec: " + (as.length() ? as : String("unknown")) +
               "\n# Firmware: " + fw + "\n# Read with 0x49 over X41A (web dumper). Bad pairs: " + String(badPairs) +
               "\n# Columns: physical address, value (hex).\n";
  char line[12];
  for (uint16_t a = 0; a < EEPROM_SIZE; a++)
  {
    if (!valid[a])
      continue;
    snprintf(line, sizeof(line), "%03X\t%02X\n", a, image[a]);
    out += line;
  }
  String name = "eeprom";
  if (as.length())
    name += "_" + as;
  if (firmwareId.length())
    name += "_" + firmwareId;
  name += name == "eeprom" ? "_dump.tsv" : ".tsv";
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
  server.send(200, "text/tab-separated-values", out);
  downloaded = true;
}

// ---------------------------------------------------------------- setup / loop

void setup()
{
  // No Serial.begin(): X41A is often wired to GPIO3/GPIO1 (RX/TX), the USB UART pins.
#ifdef ARDUINO_ARCH_ESP8266
  Daikin.begin(9600, SWSERIAL_8E1, DAIKIN_RX_PIN, DAIKIN_TX_PIN);
#else
  Daikin.begin(9600, SERIAL_8E1, DAIKIN_RX_PIN, DAIKIN_TX_PIN);
#endif

  if (joinHomeWifi())
    lastConnected = millis();
  else
    openOwnWifi();

  // OTA and the /update page work on the home Wi-Fi and on the own Wi-Fi. Add
  // `#define OTA_PASSWORD "..."` to setup.h to protect both (/update user name: admin).
  // ArduinoOTA also starts mDNS (http://HOSTNAME.local).
  ArduinoOTA.setHostname(HOSTNAME);
#ifdef OTA_PASSWORD
  ArduinoOTA.setPassword(OTA_PASSWORD);
  updatePage.setup(&server, "/update", "admin", OTA_PASSWORD);
#else
  updatePage.setup(&server, "/update");
#endif
  ArduinoOTA.begin();

  server.on("/", []() { server.send_P(200, "text/html", PAGE); });
  server.on("/status", handleStatus);
  server.on("/probe", HTTP_POST, handleProbe);
  server.on("/dump", HTTP_POST, handleDump);
  server.on("/dump.tsv", handleFile);
  server.begin();
}

void loop()
{
  server.handleClient();
  ArduinoOTA.handle();
  superviseWifi();
  step();
}
