# SX631 / S34U18 (E.ON Hungary) P1 Meter Repository — Complete Analysis Report

**Project:** `ESP32-C3-READ-P1-METER` — Arduino sketch for ESP32-C3, adapted for the Sanxing SX631 / S34U18 E.ON Hungary P1 electricity meter
**Report date:** 2026-09-09
**Scope of this report:** Read-only analysis. **No project files were created, modified, or deleted.** The only file produced by this audit is this report (`SX631_REPOSITORY_ANALYSIS.md`).
**Repository state at audit time:** 38 files, single commit (`Add files via upload`), clean working tree.

**Legend used throughout:**

- ✅ **CONFIRMED** — fact verified directly in the source files (or a hard compile error / definite defect).
- ⚠️ **SUSPECTED** — likely bug or inconsistency that depends on runtime behavior, toolchain, or data that cannot be verified statically.

---

## Executive Summary

The repository is the original **patience4711 / hansiart** ESP32-C3 P1-meter firmware (Sagemcom T210 / ESMR5 target) with a **parallel SX631 fast-path bolted on**. The SX631 support consists of two wholesale-replaced files (`AAA_READ_SERIAL.ino`, `AAA-PARSE-TELEGRAM.ino`, both with "FINAL …" banners and LF line endings while the rest of the project uses CRLF) plus an updated `MQTT.ino`.

Reception and extended decoding live in `AAA_READ_SERIAL.ino`; the legacy line parser stayed in `AAA-PARSE-TELEGRAM.ino`; dispatch is by `meterType == 3` with the shared `MeterData` struct as the bridge. **Almost all inconsistencies cluster exactly at this seam.**

The single **hard blocker** is that `MQTT.ino` (MQTT format 2) references `sx631.q_imp` / `sx631.q_exp`, which do **not** exist in the `SX631Registers` struct — the sketch cannot compile as-is. Other high-impact findings: NaN leaking into the HTTP JSON endpoints, an unauthenticated OTA upload handler, a phase-1 display typo in the front-page JavaScript, buffer-overflow risks in `/API/TELEGRAM` and the WebSocket console, and three different spellings of the monthly-values filenames.

---

## 1. Main Arduino Project File ✅ CONFIRMED

**`ESP32C3_P1METER_v0_4.ino`** is the main sketch.

Evidence:

- The filename matches the sketch folder name (Arduino requirement for the "root" sketch).
- It is the only file defining `setup()` and `loop()`.
- It owns the include order, all global state, pin definitions, the `MeterData` struct, and the page-asset includes.

Build constraints declared in the file header and `legende.ino`:

```cpp
platform: ESP32 c3 dev
partition scheme: minimal spiffs with ota
board version 3.0.7 ?? other versions won't compile
```

- Arduino-ESP32 core **3.0.7** (README: "The ota updates won't work with other versons").
- Partition scheme: **Minimal SPIFFS with OTA** (needed for the web OTA path).
- The header comments and the Bluetooth `#error` block are leftovers from an ESP32-classic sibling project.

### Global definitions owned by the main sketch (selection)

```cpp
#define VERSION  "ESP32C3-P1METER"

#define knop          0        // boot button (interrupt, FALLING)
#define led_onb       8        // onboard LED (GPIO 8; comment says "was 2")
#define P1_ENABLE     5        // gpio5: HIGH pulls the meter's RX/request line up
#define RXP1          3
#define TXP1          2        // "not used"

#define OBIS_SMR             "1-3:0.2.8("
#define OBIS_CON_LT   "1-0:1.8.1("
#define OBIS_CON_HT   "1-0:1.8.2("
#define OBIS_RET_LT   "1-0:2.8.1("
#define OBIS_RET_HT   "1-0:2.8.2("

#define OBIS_POWER_C1 "1-0:21.7.0("
#define OBIS_POWER_R1 "1-0:22.7.0("
#define OBIS_POWER_C2 "1-0:41.7.0("
#define OBIS_POWER_R2 "1-0:42.7.0("
#define OBIS_POWER_C3 "1-0:61.7.0("
#define OBIS_POWER_R3 "1-0:62.7.0("

#define OBIS_GAS      "0-1:24.2.1("

struct MeterData {
    uint8_t smr;
    float   con_lt;
    float   con_ht;
    float   ret_lt;
    float   ret_ht;
    uint16_t   pwr_con[3];   // W, per phase
    uint16_t   pwr_ret[3];   // W, per phase
    float   gas;
};
MeterData meter;

char teleGram[1060]={"not polled"};
char readCRC[5];
bool threePhase = false;
bool testTelegram = false;
char timeStamp[12]={"not polled"};
int  pollFreq = 300;
uint8_t meterType = 0;        // 0..3, set from /basisconfig.json
bool bootTest;  bool baudRate9600;  bool rxInvert;
bool polled = false;
uint8_t diagNose = 0;         // 0 silent, 1 websocket console, 2 USB serial
int  actionFlag = 0;
char txBuffer[50];
char Mqtt_Broker[30];  char Mqtt_outTopic[26];  char Mqtt_inTopic[26];
char Mqtt_Username[26]; char Mqtt_Password[26]; char Mqtt_Clientid[26];
char Mqtt_Port[5];     int Mqtt_Format = 0;
int  gas_Idx = 123;  int el_Idx = 456;

typedef struct{ float EC_LT; float EC_HT; float ER_LT; float ER_HT; float mGAS; } m_values;
m_values MVALS[13];           // monthly meter readings 1..12 (+0 unused by loop code)

WiFiClient espClient;
PubSubClient MQTT_Client(espClient);
AsyncWebServer server(80);
AsyncEventSource events("/events");
AsyncWebSocket ws("/ws");
NTPClient timeClient(ntpUDP, "pool.ntp.org");
DNSServer dnsServer;
```

### `setup()` order of operations ✅ CONFIRMED

1. `Serial.begin(115200)`, pin modes, `ledblink(1, 800)`.
2. `attachInterrupt(digitalPinToInterrupt(knop), isr, FALLING);`
3. `SPIFFS_read();` — mounts SPIFFS, loads `/testFile.txt`, `/wificonfig.json`, `/basisconfig.json`, `/mqttconfig.json`.
4. `Serial1.setRxBufferSize(1024);` then:
   - `baudRate9600` → `Serial1.begin(9600, SERIAL_7E1, 3, 2);`
   - else → `Serial1.begin(115200, SERIAL_8N1, 3, 2);`
   - `if(rxInvert) uart_set_line_inverse(UART_NUM_1, UART_SIGNAL_RXD_INV);`
5. `WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);` — brown-out detector disabled (device is powered from the meter).
6. If `bootTest`: `pollFreq = 0; meterPoll();` (first poll, saved as test telegram).
7. `start_wifi();` → STA connect or captive portal → `start_asyserver()`.
8. `getTijd();` — NTP time + timezone + DST.
9. Load monthly values: `for (x=1..12) readStruct("/mvalues_" + x + ".str", x);`
10. MQTT init (`setKeepAlive(150)`, `setBufferSize(512)`, `setServer(Mqtt_Broker, atoi(Mqtt_Port))`, callback, `mqttConnect()` if `Mqtt_Format != 0`).
11. `initWebSocket(); eventSend(0);` (SSE "reload").

### `loop()` ✅ CONFIRMED

```cpp
void loop() {
  unsigned long nu = millis();
  if(pollFreq != 0) {
     if (nu - laatsteMeting >= 1000UL * pollFreq) {
        laatsteMeting += 1000UL * pollFreq;
        digitalWrite(P1_ENABLE, HIGH);   // request telegram
        meterPoll();
        digitalWrite(P1_ENABLE, LOW);
     }
  }
  if (day() != datum) { ... writeMonth(month()) on 1st of month ... getTijd(); }
  if(Mqtt_Format != 0 ) MQTT_Client.loop();
  test_actionFlag();        // deferred web/console actions
  ws.cleanupClients();
  yield();
  if(Serial.available()) handle_Serial();   // USB serial commands
}
```

⚠️ **SUSPECTED (P1_ENABLE glitch):** the loop raises `P1_ENABLE`, then `meterPoll()` immediately drops it `LOW`, calls `empty_Serial1()`, and raises it `HIGH` again (see §3). Any bytes arriving in that window are discarded. This works in practice only because the SX631 (re)starts a telegram on each rising edge of the request line.

---

## 2. Files Participating in the Arduino Build ✅ CONFIRMED

Arduino concatenates **all `.ino` files** of the folder into one translation unit: the sketch file first, then the tabs **alphabetically**. `.h`/`.H`/`.cpp` files are compiled/included as usual.

### Compiled as part of the single .ino translation unit (27 .ino files)

| File | Role |
|---|---|
| `ESP32C3_P1METER_v0_4.ino` | main sketch, `setup()`/`loop()`, globals |
| `AAA-PARSE-TELEGRAM.ino` | OBIS line parser (sorts **before** `AAA_READ_SERIAL.ino`: `-` < `_`) |
| `AAA_READ_SERIAL.ino` | serial reception + SX631 decoder + `decodeTelegram()` |
| `AAA_REPORT.ino` | monthly report table (`putReport`) |
| `AA_CONSOLE.ino` | WebSocket console (`CONSOLE_HTML`, `handleWebSocketMessage`, `initWebSocket`) |
| `ABOUT.ino` | `/ABOUT` system page |
| `ACTIONS.ino` | `test_actionFlag()` deferred action dispatcher |
| `ASYSERVER.ino` | `start_asyserver()`, `/`, `/get.Data`, `/api/v1/data`, OTA routes, `confirm()` |
| `CONFIG_BASIS.ino` | `/BASISCONFIG` page (`BASISCONFIG`, `zendPageBasis`) |
| `CONFIG_GEO.ino` | `/GEOCONFIG` page |
| `EXTERNAL.ino` | `checkRemote()`, `handleNotFound()` |
| `HEALTH.ino` | **100 % commented out** — dead file |
| `HELPERS.ino` | `consoleOut`, `ledblink`, `getChipId`, rounding helpers, `waitSerial1Available`, `empty_Serial1`, `writeMonth`, `showDir`, `printFiles` |
| `INFOPAGE.ino` | **100 % commented out** — dead file |
| `ISR.ino` | button interrupt `isr()` |
| `legende.ino` | comments/notes only (pin legend, compile notes) |
| `MQTT.ino` | `mqttConnect()`, `MQTT_Receive_Callback`, `sendMqtt()` |
| `MQTT_CONFIG.ino` | MQTT config page + `zendPageMQTTconfig()` |
| `PORTAL_WIFI.ino` | captive portal (AP mode, scan, `wifiConnect`, `connectWifi`, `makeList`) |
| `Reboot__AP.ino` | nearly all commented; only `loginAdmin()` remains (itself unused) |
| `SERIAL.ino` | USB-serial command handler `handle_Serial()` |
| `SPIFFS_RW.ino` | config read/save, `testFilesave()`, `file_open_for_read()` |
| `Start_WiFi.ino` | `start_wifi()`, `loginBoth()` |
| `TIJD_CALC.ino` | `sun_setrise()`, `isSummertime()` |
| `TIJD_GET.ino` | `getTijd()` NTP retrieval |
| `handleforms.ino` | `handleForms()` — all three config forms |
| `test.ino` | `read_test()` / `testPrint()` — `METERPOLL-TEST` helper |

### Header/library files

| File | Included by | Notes |
|---|---|---|
| `HTML.H` | main sketch | `STYLESHEET`, `HTML_HEAD`, `SECURITY` JS, `UPDATE_FORM` (unused), `REQUEST_DENIED`, `FAVICON`/`FAVICON_len`, `LOGPAGE` (unused) |
| `AAA_MENUPAGE.H` | main sketch | `MENUPAGE` |
| `AAA_HOMEPAGE.H` | main sketch | `P1_HOMEPAGE` + `JAVA_SCRIPT` |
| `PORTAL_HTML.H` | main sketch | `PORTAL_STYLESHEET`, `PORTAL_MAIN`, `WIFI_PAGE`, `PORTAL_CONFIRM`, `PORTAL_LIST` |
| `REPORT.H` | main sketch | `REPORTPAGE` |
| `OTA.H` | main sketch | `otaIndex` (fw-update upload page) |
| `CRC16.h` | main sketch | defines `unsigned int CRC16(unsigned int, unsigned char*, int)` **inside the header** (single include ⇒ OK) |
| `sunMoon.h` / `sunMoon.cpp` | main sketch / TU | sunrise/sunset/moon math; separate translation unit |

### External libraries (must be installed)

`ArduinoJson` (v7-style `JsonDocument`), `ESPAsyncWebSrv` (note the **unusual fork name**, not `ESPAsyncWebServer`), `AsyncTCP`, `AsyncEventSource`, `PubSubClient`, `TimeLib`/`Time`, `NTPClient`, `Preferences` (declared but effectively unused), `EEPROM` (included, handlers commented out), `driver/uart` (line inversion), `esp_task_wdt`/`rtc_wdt`, `soc` registers (brown-out).

### Dead / orphaned files ✅ CONFIRMED

- **`Hash.h`** — never included (`//#include <Hash.h>` commented out in both places). Dead file.
- **`HEALTH.ino`**, **`INFOPAGE.ino`** — entirely commented out.
- **`legende.ino`**, **`Reboot__AP.ino`** — comment-only (except one unused function).

---

## 3. SX631 Serial Reception — `AAA_READ_SERIAL.ino` (focus)

`AAA_READ_SERIAL.ino` is one of the two "FINAL"-banner files (LF line endings, unlike the CRLF rest — see §10). It contains: `meterPoll()`, `read_into_array()`, the `SX631Registers` struct + decoder, and `decodeTelegram()`.

### 3.1 `meterPoll()` ✅ CONFIRMED

```cpp
void meterPoll() {
  consoleOut("polling the meter");
  digitalWrite(P1_ENABLE, LOW);    // disable P1 first
  empty_Serial1();                 // flush stale UART data
  digitalWrite(P1_ENABLE, HIGH);   // request line high -> meter transmits

  if (read_into_array()) {
    ledblink(3, 300);
    digitalWrite(P1_ENABLE, LOW);
    if (bootTest) { /* save telegram to SPIFFS as test file */ return; }
    decodeTelegram();
    if (polled) { sendMqtt(false); sendMqtt(true); }
  } else {
    consoleOut("no telegram received");
  }
  consoleOut("meterPoll done");
  digitalWrite(P1_ENABLE, LOW);
}
```

Triggered from:
- `loop()` autopoll every `pollFreq` seconds (0/30/60/300 selectable in `/BASISCONFIG`);
- `setup()` when `bootTest` is set;
- HTTP `GET /API/poll` → `actionFlag = 26` → `test_actionFlag()` → `meterPoll()` (only when autopoll is off);
- WebSocket console `POLL-METER` → `actionFlag 26`;
- MQTT payload `{"poll": ...}` → `actionFlag 26` (only when autopoll is off).

### 3.2 `read_into_array()` — streaming state machine ✅ CONFIRMED (design), ⚠️ details below

Buffer selection by meter type:

```cpp
static char sx631RxTelegram[8192];          // static, 8 KiB for SX631
char *rx = (meterType == 3) ? sx631RxTelegram : teleGram;   // teleGram[1060] legacy
const size_t rxSize = (meterType == 3) ? sizeof(sx631RxTelegram) : sizeof(teleGram);
```

State machine (per the file's own design comments):

- `const unsigned long START_TIMEOUT_MS = 5000UL;` — 5 s to find `'/'`.
- `const unsigned long FRAME_GAP_TIMEOUT_MS = 500UL;` — once started, a 500 ms inter-byte silence is a **timeout/error, never a normal end of telegram** (explicit fix versus the old `Serial1.available()==0` end condition).
- **Tight inner drain loop**: `while (Serial1.available()) { int c = Serial1.read(); ... }` — the comment explains that at 115200 baud returning to the outer loop per byte would risk RX FIFO overflow.
- Sync on `'/'` → accumulate until `'!'` → consume **exactly 4 hex characters** into `readCRC` (also appended to the buffer) → success.
- Overflow guard: `if (telegramLength >= rxSize - 6) { ... return false; }`.
- CRC chars validated as `[0-9A-Fa-f]`, otherwise "invalid CRC character" → `return false`.
- Non-blocking for the rest of the system: `yield()` between drains.
- `polled` is set `false` at entry, `true` only on a complete frame.

Core of the state machine:

```cpp
if (!started) {
  if (c != '/') continue;
  started = true; telegramLength = 0;
  rx[telegramLength++] = '/'; rx[telegramLength] = '\0';
  continue;
}
if (!ended) {
  if (telegramLength >= rxSize - 6) { consoleOut("telegram too long for receive buffer"); return false; }
  rx[telegramLength++] = (char)c; rx[telegramLength] = '\0';
  if (c == '!') { ended = true; consoleOut("found the end sign"); }
  continue;
}
/* after '!': exactly 4 hex CRC chars */
if (crcCount < 4) {
  if (!hex) { consoleOut("invalid CRC character"); return false; }
  readCRC[crcCount++] = ch;
  rx[telegramLength++] = ch; rx[telegramLength] = '\0';
  if (crcCount == 4) { readCRC[4] = '\0'; polled = true; return true; }
}
```

Post-`'/'` gap handling:

```cpp
if (started && (millis() - lastByteTime >= FRAME_GAP_TIMEOUT_MS)) {
  consoleOut(ended ? "timeout waiting for CRC" : "timeout waiting for telegram data");
  return false;   // "Do NOT accept it as a complete frame."
}
```

### 3.3 UART configuration ✅ CONFIRMED

- Pins fixed: `Serial1.begin(baud, mode, 3, 2)` → **RX = GPIO3, TX = GPIO2** (TX unused).
- **SX631: 115200 8N1 with inverted RX** (file header: "SX631/S34U18: 115200 8N1, inverted RX"). `rxInvert` defaults to `true` when `/basisconfig.json` is missing (`rxInvert = doc["rxInvert"] | true;`) and is applied via `uart_set_line_inverse(UART_NUM_1, UART_SIGNAL_RXD_INV)` in `setup()`.
- RX buffer 1024 bytes (`Serial1.setRxBufferSize(1024)`).
- ⚠️ **SUSPECTED:** `consoleOut()` with `diagNose == 1` does `delay(100)` per message while streaming at 115200 baud; with a 1024-byte RX buffer this can starve the stream during reception (several `consoleOut` calls are made *inside* `read_into_array()`).

### 3.4 `SX631Registers` struct + decoder ✅ CONFIRMED

```cpp
struct SX631Registers {
  char meter_id[64]; char meter_id_part1[32]; char meter_id_part2[32];
  char time[16]; char date[16]; char breaker[32];
  char error_reg[32]; char alarm_reg1[32]; char alarm_reg2[32]; char tariff[16];
  float e_imp, e_imp_t1, e_imp_t2, e_exp, e_exp_t1, e_exp_t2, e_abs;   // kWh
  float voltage_l1, voltage_l2, voltage_l3;
  float current_l1, current_l2, current_l3;
  float power_imp_total, power_exp_total;
  float power_imp_l1, power_imp_l2, power_imp_l3;
  float power_exp_l1, power_exp_l2, power_exp_l3;                       // kW
  float frequency, power_factor, power_factor_l1, power_factor_l2, power_factor_l3;
  bool valid;
};
SX631Registers sx631;
```

Helpers:

```cpp
static const char *sx631FindObis(const char *obis) {
  const char *p = strstr(sx631RxTelegram, obis);
  if (p != NULL) return p;
  static char fullObis[64];
  snprintf(fullObis, sizeof(fullObis), "%s.255", obis);
  return strstr(sx631RxTelegram, fullObis);      // accepts the ".255" full OBIS form too
}

static bool sx631GetValue(const char *obis, float &value) {
  const char *p = sx631FindObis(obis);
  if (p == NULL) return false;
  p = strchr(p, '('); if (p == NULL) return false; ++p;
  char *endPtr = NULL;
  float v = strtof(p, &endPtr);
  if (endPtr == p || !isfinite(v)) return false;
  value = v; return true;
}

static bool sx631GetString(const char *obis, char *dest, size_t destSize) { /* paren-bounded copy */ }
```

`decodeAllSX631Registers()` decodes (with `sx631Zero()` memset first):

- Identification/status: `C.1.0`, `C.1.2`, `C.1.3`, `0.9.1` (time), `0.9.2` (date), `C.50.68` (breaker), `F.F.0` (error), `F.E.0`, `F.L.0`, `0-0:96.14.0` (tariff).
- Active energy: `1-0:1.8.0/1/2`, `1-0:2.8.0/1/2`, `1-0:15.8.0`.
- Voltages: `1-0:32.7.0 / 52.7.0 / 72.7.0`; currents: `1-0:31.7.0 / 51.7.0 / 71.7.0`.
- Phase active power: `1-0:21.7.0 / 41.7.0 / 61.7.0` (import), `1-0:22.7.0 / 42.7.0 / 62.7.0` (export).
- Totals: `1-0:1.7.0` / `1-0:2.7.0`.
- Frequency `1-0:14.7.0`; power factor `1-0:13.7.0` and `33/53/73.7.0`.
- Sets `sx631.valid = true` at the end; logs a summary block via `consoleOut`.

Explicit design statements in the file (relevant to §10): *"Reactive-energy / reactive-power registers are deliberately NOT decoded"* and *"Phase power is NEVER calculated from U x I."* The file's top banner, however, still claims it adds *"total active/reactive energy and reactive power"* — an internal contradiction that matches the compile error in §10-A1.

---

## 4. Telegram Parsing — `AAA-PARSE-TELEGRAM.ino` (focus) + dispatch

### 4.1 Two parsers coexist ✅ CONFIRMED

Dispatch happens in `decodeTelegram()` (in `AAA_READ_SERIAL.ino`):

```cpp
void decodeTelegram() {
  if (!polled) return;
  char *frame = (meterType == 3) ? sx631RxTelegram : teleGram;
  size_t lengte = strlen(frame);
  if (lengte < 6) { consoleOut("telegram too short"); polled = false; return; }

  if (meterType == 3) {
      /* SX631 / E.ON Hungary:
       * CRC is received but validation is intentionally disabled,
       * as in the proven working configuration used with this meter. */
      consoleOut("SX631 CRC received = " + String(readCRC) + " (CRC check disabled)");
      meter.smr = 0; meter.con_lt = NAN; ... meter.gas = NAN;
      // direct sx631GetValue() decoding into meter.*  (see §6)
      decodeAllSX631Registers();
      ... consoleOut summary ... eventSend(2); sprintf(timeStamp, "%02d/%02d %02d:%02d", day(), month(), hour(), minute());
      return;
  }

  /* Original non-SX631 parser remains unchanged. */
  int dataLength = (int)lengte - 4;
  long calculatedCRC = CRC16(0x0000, (unsigned char *)frame, dataLength);
  long receivedCRC = strtol(readCRC, NULL, 16);
  if (receivedCRC != calculatedCRC) { consoleOut("CRC is WRONG - telegram rejected"); polled = false; return; }
  teleGram[lengte - 4] = '\0';    // strip CRC before parsing
  parseTelegram();
  polled = true;
  eventSend(2);
  sprintf(timeStamp, "%02d/%02d %02d:%02d", day(), month(), hour(), minute());
}
```

### 4.2 `AAA-PARSE-TELEGRAM.ino` header ✅ CONFIRMED

```cpp
/*
 * meterType:
 *   0 = no meter
 *   1 = Sagemcom XS210_D ESMR5
 *   2 = Landis Gyr E350 ZMF100
 *   3 = Sanxing SX631 / S34U18
 * The SX631 uses the normal DSMR/P1 OBIS lines for the values already
 * represented by the project's MeterData structure.  Additional SX631
 * registers are decoded by decodeAllSX631Registers() in AAA_READ_SERIAL.ino.
 */
```

It **redefines** the same `OBIS_*` macros as the main sketch (identical tokens; benign but a drift hazard — §10-A2) and provides:

- `static bool lineHasObis(const char *p, const char *obis)` → `strncmp(p, obis, strlen(obis)) == 0` (line-prefix match),
- `parseTelegram()`,
- `uint16_t powerToWatts(float kw)`,
- `float parseValue(const char *p, const char *obis)` → `atof(p + strlen(obis))`,
- `float parseGasValue(const char *p)` → skips the timestamp parenthesis of `0-1:24.2.1(yyyymmddhhmmss)(value)` and `atof(second + 1)`.

### 4.3 `parseTelegram()` ✅ CONFIRMED

- **Resets `meter.*` first** (`NAN` / `0`) — prevents stale RAM values when a telegram lacks a line:

```cpp
meter.smr = 0;
meter.con_lt = NAN;  meter.con_ht = NAN;  meter.ret_lt = NAN;  meter.ret_ht = NAN;
meter.pwr_con[0..2] = 0;  meter.pwr_ret[0..2] = 0;  meter.gas = NAN;
```

- Walks `teleGram` line by line (`while (*p)` + advance to next `'\n'`), matching each line prefix against the `OBIS_*` macros in an `if/else if` chain and filling `meter.*` (powers through `powerToWatts()`).
- Trailing `consoleOut` summary of 1.8.1/1.8.2/2.8.1/2.8.2 and P1..P3 con/ret.
- Comment notes: *"SX631-specific extended decoding is performed once by decodeAllSX631Registers() in AAA_READ_SERIAL.ino after CRC validation."* (⚠️ in reality the SX631 path never calls `parseTelegram()` at all — dispatch is exclusive, see §4.1.)

### 4.4 CRC handling ✅ CONFIRMED

- Legacy path: CRC-16 (poly `0xA001`, init `0x0000` — `CRC16.h`) over `frame[0 .. lengte-4-1]`, compared with `strtol(readCRC, NULL, 16)`. Logic is correct.
- **SX631 path: CRC received but deliberately not validated** (documented as intentional, "as in the proven working configuration used with this meter"). ⚠️ Consequence (suspected robustness issue): corrupted telegrams are accepted silently.
- Test-file path (`actionFlag 28` in `ACTIONS.ino`): extracts `readCRC` from the tail of the stored file with `strncpy(readCRC, teleGram + len-4, 4);` — ⚠️ does not NUL-terminate `readCRC` (char array is 5 bytes and was memset before use in most flows, but it is an edge-case bug).

---

## 5. OBIS Decoding ✅ CONFIRMED (with duplication caveat)

Two definition sites, currently identical (⚠️ drift hazard):

```cpp
/* main sketch AND AAA-PARSE-TELEGRAM.ino */
#define OBIS_SMR        "1-3:0.2.8("
#define OBIS_CON_LT     "1-0:1.8.1("   // low tariff  (project maps LT=1.8.1)
#define OBIS_CON_HT     "1-0:1.8.2("   // high tariff
#define OBIS_RET_LT     "1-0:2.8.1("
#define OBIS_RET_HT     "1-0:2.8.2("
#define OBIS_POWER_C1   "1-0:21.7.0("   // L1 import
#define OBIS_POWER_R1   "1-0:22.7.0("   // L1 export
#define OBIS_POWER_C2   "1-0:41.7.0("   // L2 import
#define OBIS_POWER_R2   "1-0:42.7.0("
#define OBIS_POWER_C3   "1-0:61.7.0("   // L3 import
#define OBIS_POWER_R3   "1-0:62.7.0("
#define OBIS_GAS        "0-1:24.2.1("
```

Where decoding happens:

1. **`AAA-PARSE-TELEGRAM.ino`** — line-prefix matcher for the legacy path (`meterType != 3`), filling `MeterData`.
2. **`AAA_READ_SERIAL.ino`** — `sx631GetValue()` / `sx631GetString()` / `sx631FindObis()` for the SX631 path, filling both `meter.*` (the six shared fields) and the full `sx631.*` register set.

SX631 extended register map (from `decodeAllSX631Registers()`), verbatim from the file's comment block:

```
1-0:1.8.0 total import | 1-0:1.8.1 tariff1 | 1-0:1.8.2 tariff2
1-0:2.8.0 total export | 1-0:2.8.1 tariff1 | 1-0:2.8.2 tariff2
1-0:15.8.0 total active energy
U: 1-0:32.7.0 / 52.7.0 / 72.7.0        I: 1-0:31.7.0 / 51.7.0 / 71.7.0
+P: 1-0:21.7.0 / 41.7.0 / 61.7.0      -P: 1-0:22.7.0 / 42.7.0 / 62.7.0
Totals: 1-0:1.7.0 (import) / 1-0:2.7.0 (export)
PF: 1-0:13.7.0 total, 33/53/73.7.0 per phase       F: 1-0:14.7.0
```

`parseValue()` is a raw `atof` after the OBIS prefix — ⚠️ **SUSPECTED** (minor): it does not verify the opening parenthesis, and `parseGasValue()` relies on exactly two parentheses on the line (correct for DSMR `0-1:24.2.1(YYMMDDhhmmss)(m3)`; gas is not transmitted by the E.ON SX631 configuration anyway, so `meter.gas` stays `NAN`).

---

## 6. P1 / P2 / P3 Power — how the values are obtained ✅ CONFIRMED

- **Legacy meters (`meterType` 1/2):** from `teleGram` lines `1-0:21.7.0 / 41.7.0 / 61.7.0` (import) and `1-0:22.7.0 / 42.7.0 / 62.7.0` (export), via `parseTelegram()` → `powerToWatts()`.
- **SX631 (`meterType == 3`):** read **directly** from `sx631RxTelegram` inside `decodeTelegram()`:

```cpp
/* DIRECT meter values:
 *   21.7.0 -> L1 import power   41.7.0 -> L2   61.7.0 -> L3
 *   22.7.0 -> L1 export power   42.7.0 -> L2   62.7.0 -> L3
 * The meter reports kW. meter.pwr_* stores W.
 * This is only a unit conversion; no U x I calculation is performed. */
if (sx631GetValue("1-0:21.7.0", v) && isfinite(v)) meter.pwr_con[0] = (long)roundf(v * 1000.0f);
if (sx631GetValue("1-0:41.7.0", v) && isfinite(v)) meter.pwr_con[1] = (long)roundf(v * 1000.0f);
if (sx631GetValue("1-0:61.7.0", v) && isfinite(v)) meter.pwr_con[2] = (long)roundf(v * 1000.0f);
if (sx631GetValue("1-0:22.7.0", v) && isfinite(v)) meter.pwr_ret[0] = (long)roundf(v * 1000.0f);
if (sx631GetValue("1-0:42.7.0", v) && isfinite(v)) meter.pwr_ret[1] = (long)roundf(v * 1000.0f);
if (sx631GetValue("1-0:62.7.0", v) && isfinite(v)) meter.pwr_ret[2] = (long)roundf(v * 1000.0f);
```

- **`powerToWatts()`** (legacy path) clamps instead of wrapping:

```cpp
uint16_t powerToWatts(float kw) {
  if (!isfinite(kw) || kw <= 0.0f) return 0;
  float watts = kw * 1000.0f;
  if (watts >= 65535.0f) return 65535;
  return (uint16_t)(watts + 0.5f);
}
```

- Storage: `MeterData.pwr_con[3]` / `pwr_ret[3]`, `uint16_t` **watts** (≤ 65535 W per phase).
- Consumers:
  - `/get.Data` → `PWRC1..PWRC3` / `PWRR1..PWRR3` (front page).
  - `/api/v1/data` → HomeWizard-style per-phase **net** balance `pwr_lN = pwr_con[N] - pwr_ret[N]`, `active_power_w` = sum.
  - MQTT f1: phase **sum** as totals: `const uint32_t pwrConTotal = (uint32_t)meter.pwr_con[0] + [1] + [2];` (same for `pwrRetTotal`).
  - MQTT f2: per-phase fields `p1_con..p3_con`, `p1_ret..p3_ret` plus totals in `actualp_con` / `actualp_ret`.
  - Front-page JS: computes `aP1 = PWRC1 + PWRR2` etc. (bug — §10-B5).
- **Never** derived from U×I and **never** taken from the totals `1.7.0 / 2.7.0` (explicit in the code comments).

---

## 7. MQTT Implementation ✅ CONFIRMED (design) — with one hard bug (§10-A1)

### 7.1 Client & lifecycle

- **PubSubClient** over `WiFiClient`; global `MQTT_Client`.
- `setup()`: `setKeepAlive(150)`, `setBufferSize(512)`, `setServer(Mqtt_Broker, atoi(Mqtt_Port))`, `setCallback(MQTT_Receive_Callback)`, and `mqttConnect()` when `Mqtt_Format != 0`.
- `loop()`: `MQTT_Client.loop()` when MQTT enabled.
- Reconnect after settings change: `/submitform` (mqtt form) → `actionFlag 24` → `test_actionFlag()` → `MQTT_Client.disconnect(); setServer(...); mqttConnect();`.

### 7.2 `mqttConnect()` ✅ CONFIRMED

```cpp
bool mqttConnect() {
  if (MQTT_Client.connected()) { consoleOut("mqtt was connected"); return true; }
  uint8_t retry = 3;
  while (!MQTT_Client.connected()) {
    if (MQTT_Client.connect(Mqtt_Clientid, Mqtt_Username, Mqtt_Password)) {
      if (Mqtt_Format != 0) { if (MQTT_Client.subscribe(Mqtt_inTopic)) consoleOut("subscribed to " + String(Mqtt_inTopic)); }
      consoleOut(F("mqtt connected"));  return true;
    } else { if (!--retry) break; delay(500); }
  }
  consoleOut(F("mqtt connection failed"));  return false;
}
```

### 7.3 Inbound — `MQTT_Receive_Callback` ✅ CONFIRMED

```cpp
void MQTT_Receive_Callback(char *topic, byte *payload, unsigned int length) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload, length);  // uses real length
  if (error) { if (diagNose) ws.textAll("mqtt no valid json "); return; }
  if (!doc["poll"].isNull()) {
    if (pollFreq == 0) { actionFlag = 26; return; }       // on-demand poll allowed
    else { consoleOut("forbidden, automatic polling is on"); return; }
  } else { consoleOut("polling = automatic, skipping"); }
}
```

### 7.4 Outbound — `sendMqtt(bool gas)` ✅ CONFIRMED

- `if (Mqtt_Format == 0) return;` and `if (gas && Mqtt_Format != 1) return;` — formats 2/3 publish **once** (the header documents this as the fix for the historical duplicate-publish).
- Publish only after `mqttConnect()` succeeds: `MQTT_Client.publish(Mqtt_send, toMQTT, reTain)` with `reTain = false` always.

| Format | Layout | Notes |
|---|---|---|
| **1** (Domoticz) | electric: `{"idx":el_Idx,"nvalue":0,"svalue":"kWh_lt;kWh_ht;kWh_retlt;kWh_retht;conW;retW"}` and gas: `{"idx":gas_Idx,...,"svalue":"m3;"}` | Electric packet requires all four energies finite (`isfinite` guard, "MQTT electric data incomplete - not published"); gas packet skipped when `meter.gas` is NaN ("NaN is not valid JSON"). |
| **2** (JSON) | original keys `econ_lt, econ_ht, eret_ht, eret_lt, actualp_con, actualp_ret, gas` **plus** `p1_con..p3_con, p1_ret..p3_ret, v1..v3, i1..i3, frequency, power_factor, e_imp, e_exp, q_imp, q_exp` | `actualp_*` = 3-phase totals; SX631 fields guarded by `sx631.valid`; **references `sx631.q_imp` / `sx631.q_exp` that do not exist → compile error (§10-A1)**. |
| **3** (ThingSpeak) | `field1..field4` kWh, `field5/6` totals W, `field7` gas, `status=MQTTPUBLISH` | `isfinite(meter.gas) ? meter.gas : 0.0`; energy fields **not** NaN-guarded. |

### 7.5 Configuration ✅ CONFIRMED

- Web page `/MQTT` (`MQTT_CONFIG.ino`, `zendPageMQTTconfig()`) → `handleForms()` reads `mqtAdres, mqtPort, mqtoutTopic, mqtinTopic, gasidx, elidx, mqtUser, mqtPas, fm` → `mqttConfigsave()` → `/mqttconfig.json`.
- `Mqtt_Clientid` is auto-generated, not user-set: `snprintf(Mqtt_Clientid, sizeof(Mqtt_Clientid), "ESP32C-P1-%06X", (uint32_t)(chipid & 0xFFFFFF));`
- Defaults when the file is missing: broker `192.168.0.100`, port `1883`, outTopic `domoticz/in`, inTopic `domoticz/out`, user/pass `n/a`, **`Mqtt_Format = 1`** (⚠️ MQTT enabled by default — unexpected first-boot publish attempts to a non-existent broker).
- ⚠️ **SUSPECTED (doc drift):** buffer sizes — `Mqtt_outTopic[26]` / `Mqtt_inTopic[26]` are 25-char-limited despite the `MQTT.ino` banner claiming "MQTT topic buffer is no longer limited to 25 characters"; the MQTT help page still says "Format 2 has the retainflag set" while `reTain` is always `false`.
- ⚠️ **SUSPECTED (f3):** `meter.con_lt` etc. are not `isfinite`-guarded in format 3, so a first poll with missing registers could publish `nan` values to ThingSpeak.

---

## 8. Wi-Fi Startup ✅ CONFIRMED

### 8.1 `start_wifi()` (`Start_WiFi.ino`)

```cpp
void start_wifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(getChipId(false).c_str());
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print("*"); WiFi.begin();   // NVS credentials
    event += 1; if (event == 10) break;
  }
  if (event>9) { event=0; digitalWrite(led_onb, LED_AAN); start_portal(); }
  event=0;
  start_asyserver();
  ledblink(3, 500);
}
```

- 10 attempts × 500 ms with stored credentials (ESP32 NVS, written by `WiFi.begin(ssid, pass)` in the portal).
- On failure → **captive portal** (`start_portal()` in `PORTAL_WIFI.ino`):
  - `WiFi.mode(WIFI_OFF)` → scan (`scanWifi()` → `networksFound`) → `softAPConfig(192.168.4.1)` → `WiFi.softAP(getChipId(false), NULL, channel 6, 0, 4)`;
  - Android AMPDU workaround: `esp_wifi_stop/deinit; my_config.ampdu_rx_enable = false; esp_wifi_init/start`;
  - wildcard DNS (`dnsServer.start(DNS_PORT, "*", apIP)`) + captive-portal probe routes (`/generate_204`, `/fwlink`, `/hotspot-detect.html`, …) + `CaptiveRequestHandler` with `ON_AP_FILTER`;
  - `/wifiForm` shows the RSSI-sorted network list (`makeList()`); `/wifiCon` saves `pswd` + `securityLevel` (`wifiConfigsave()`), sets `WIFI_AP_STA`, `WiFi.begin(ssid, pass)`, `connectWifi()` → `WiFi.waitForConnectResult()`;
  - 5-minute portal loop (`while (millis() < laatsteMeting + 300*1000UL)`) then `ESP.restart()`; `/close` restarts immediately.
  - ⚠️ **SUSPECTED (dead-code trap):** `tryConnectFlag` / `wifiConnect()` are never actually armed anywhere (no code sets `tryConnectFlag = true`), and the global `ssid[33]` / `pass[64]` are only filled by **local** copies inside the `/wifiCon` handler — if `wifiConnect()` ever ran, it would call `WiFi.begin(ssid, pass)` with the empty globals. Harmless today because `connectWifi()` is invoked inline in the handler.

### 8.2 Access control ✅ CONFIRMED

- `loginBoth(request, "admin"|"both")` — HTTP basic auth: `admin`/`pswd` (from `/wificonfig.json`, default `"0000"`) and `user`/`userPwd` (from `/basisconfig.json`, default `"1111"`).
- `checkRemote()` (`EXTERNAL.ino`): compares the first `securityLevel` characters of the client IP against the gateway IP prefix; `securityLevel == 0` disables the check entirely. Applied per-route (mostly admin pages).
- Wi-Fi reset: long-press button (ISR) → `actionFlag 11` → `flush_wifi()` (`WiFi.begin("dummy","dummy")` overwrite) + `WiFi.disconnect(true)` + restart; or web `/STARTAP` → `actionFlag 11`.

### 8.3 HTTP server (`start_asyserver()`, `ASYSERVER.ino`) ✅ CONFIRMED

Routes: `/`, `/MENU`, `/ABOUT`, `/CONSOLE`, `/BASISCONFIG`, `/GEOCONFIG`, `/MQTT`, `/REPORT`, `/FWUPDATE`+`/handleFwupdate` (OTA), `/REBOOT`, `/STARTAP`, `/TEST` (stale "zigbee" text from a sibling project), `/SW=BACK`, `/DENIED`, `/submitform`, `/favicon.ico`, `/STYLESHEET`, `/JAVASCRIPT`, `/SECURITY`, `/get.Data` (homepage JSON), `/api/v1/data` (HomeWizard-compatible), and the `onNotFound` → `handleNotFound()` API set: `/API/TELEGRAM`, `/api/v1/data` (raw telegram!), `/API/poll`.

- `/get.Data` builds `JsonDocument root` with `CON_HT/CON_LT/RET_HT/RET_LT`, `PWRC1..3/PWRR1..3`, `enC/enR` (energy sums), `gAs`, `threeP`, `rm` (remote flag), `timestamp` — ⚠️ **no NaN guards** (§10-B6).
- `/api/v1/data` — ⚠️ **SUSPECTED (cosmetic/compat):** returns `"wifi_ssid": "WiFi.SSID()"` as a literal string and `meter_model` as an integer (`meterType`); sets `actionFlag = 130` for which **no handler exists** (§10-B12).

---

## 9. OTA Update ✅ CONFIRMED (mechanism) — ⚠️ security gap (§10-B10)

- **Web-upload only** (no HTTPS fetch, no version check, no MD5 verification).
- `GET /FWUPDATE` — gated by `checkRemote()` **and** `request->authenticate("admin", pswd)`; serves `otaIndex` (from `OTA.H`): a jQuery AJAX uploader with progress bar, posting to `/handleFwupdate`; after success it offers `/REBOOT`.
- `POST /handleFwupdate` (in `ASYSERVER.ino`):

```cpp
server.on("/handleFwupdate", HTTP_POST, [](AsyncWebServerRequest *request){
    if(checkRemote( request->client()->remoteIP().toString()) ) request->redirect( "/DENIED" );
    // NOTE: no request->return; NO authenticate() call on this POST route
    if( !Update.hasError() ) { toSend = "... UPDATE SUCCESS ... <a href='/REBOOT'>..."; }
    else { toSend = "... update failed ..."; }
    ...
},[](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
    if(filename != "") {
      if(!index){
        Serial.printf("start firmware update: %s\n", filename.c_str());
        if(!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000)){ Update.printError(Serial); }
      }
    } else { consoleOut("filename empty, aborting"); }
    if(!Update.hasError()){ if(Update.write(data, len) != len){ ... } }
    if(final){ if(Update.end(true)){ Serial.printf("firmware Update Success: %uB\n", index+len); } else { Update.printError(Serial); } }
});
```

- Reboot: `/REBOOT` → `actionFlag = 10` → `test_actionFlag()` → `delay(2000); ESP.restart();`.
- Requires the **Minimal SPIFFS with OTA** partition scheme and (per README) core **3.0.7** so the uploaded binary's partition expectations match.
- `/handleFwupdate` performs `request->redirect("/DENIED")` **without `return`** and has **no authentication** — the upload handler still runs afterwards (see §10-B10).

---

## 10. Inconsistencies and Bugs

Ranked list. **[COMPILE]** = verified from source, breaks the build. **[CONFIRMED-BUG]** = definite defect from source logic. **[SUSPECTED]** = likely bug, runtime/toolchain dependent.

### A. Compile-breaking / cross-file contract violations

1. **[COMPILE] `MQTT.ino` format 2 references nonexistent struct members `sx631.q_imp` / `sx631.q_exp`.**
   The `SX631Registers` struct in `AAA_READ_SERIAL.ino` contains **no** `q_imp`/`q_exp` fields (the file's own comment says *"Reactive-energy / reactive-power registers are deliberately NOT decoded"*, and its trailing note says the reactive work was removed). But `sendMqtt()` format 2 prints:
   ```cpp
   "\"q_imp\":%.3f,\"q_exp\":%.3f"
   ...
   sx631.valid ? sx631.q_imp : NAN,
   sx631.valid ? sx631.q_exp : NAN
   ```
   ⇒ hard compile error; the sketch cannot build as-is. `MQTT.ino` was written against an earlier "FINAL" version of the decoder and never reconciled. The `AAA_READ_SERIAL.ino` top banner still advertises "plus total active/reactive energy and reactive power", contradicting itself.

2. **[CONFIRMED] Duplicate `OBIS_*` macro definitions** — identical today, defined both in the main sketch and in `AAA-PARSE-TELEGRAM.ino`. Benign duplication, but exactly the drift pattern that produced bug A1.

3. **[SUSPECTED] Bluetooth `#error` block in the main sketch:**
   ```cpp
   #if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
   #error Bluetooth is not enabled! ...
   #endif
   ```
   Leftover from the ESP32-classic sibling project; `CONFIG_BLUEDROID_ENABLED` is not defined in typical ESP32-C3 Arduino builds, so this alone can fail compilation depending on the core version — consistent with the "compiles only on 3.0.7" reputation. The unused `#include "PSACrypto.h"` adds similar portability risk.

4. **[CONFIRMED] Mixed line endings** — `AAA_READ_SERIAL.ino`, `AAA-PARSE-TELEGRAM.ino`, `MQTT.ino` are LF-only; all original files are CRLF. Confirms these files were replaced wholesale ("FINAL …" banners) without reconciling the project.

### B. Functional bugs

5. **[CONFIRMED-BUG] Front-page phase-1 typo** (`AAA_HOMEPAGE.H`, `JAVA_SCRIPT`):
   ```js
   var aP1 = PWRC1 + PWRR2;   // PWRR2 should be PWRR1
   var aP2 = PWRC2 + PWRR2;
   var aP3 = PWRC3 + PWRR3;
   ```
   Phase-1 display shows L1-import + L2-export.

6. **[CONFIRMED-BUG] NaN propagation on the HTTP paths.** `/get.Data` and `/api/v1/data` serialize `meter.con_ht/…/gas` via `round3()/round0()` **without `isfinite` guards**. For the SX631 (`gas` never present → `NAN`; energies NaN before the first accepted telegram) `round3()` casts NaN → `int` (undefined behavior, typically `INT_MIN`) producing values like `-2147483.648` in the homepage JSON. The front-page JS check `if(obj.enC != "n/a")` is **dead code** — the server never sends `"n/a"`. (The MQTT path *does* guard NaN; the HTTP paths do not.)

7. **[CONFIRMED-BUG] `/API/TELEGRAM` mutates and can overflow `teleGram`** (`EXTERNAL.ino`, `handleNotFound()`):
   ```cpp
   strcat(teleGram, "\r\npolled at ");  strcat(teleGram, timeStamp);
   request->send(200, "text/plain", String(teleGram));
   ```
   Runs on **every request**, unbounded → gradual buffer overflow of `char teleGram[1060]`, and it corrupts the buffer that `parseTelegram()` uses. Additionally, for `meterType == 3` reception goes into `sx631RxTelegram`, so `/API/TELEGRAM` (menu item "telegram") shows an **empty** buffer — the feature is broken for the SX631. (Also: `handleNotFound` responds HTTP 200 with "ERROR …" text for unknown paths, and the second `/api/v1/data` match inside it is unreachable because the real route is registered earlier.)

8. **[CONFIRMED-BUG] `txBuffer[50]` overflow in the WebSocket console** (`AA_CONSOLE.ino`, `handleWebSocketMessage`): copies `len` payload bytes into the 50-byte global **before** any length check — any console input longer than 49 chars corrupts adjacent globals. Also `diagNose = 1` is forced for every console message.

9. **[CONFIRMED-BUG] `ISR.ino` busy-waits up to ~9 s inside interrupt context** (`while (endtime - starttime <= 6000/9000)` loops with `millis()`), blocking the WiFi/TCP task — WDT-reset and connection-loss risk on long button presses. Additionally, `ACTIONS.ino` tests `if (actionFlag == 11 || value == 11)` — `value` (a global incremented per short button press) is never reset, so **11 cumulative short presses over time trigger a Wi-Fi-credential wipe + reboot**.

10. **[CONFIRMED-BUG] Unauthenticated firmware upload.** `POST /handleFwupdate` checks `checkRemote()` but (a) does **not** `return` after `request->redirect("/DENIED")`, and (b) never calls `request->authenticate(...)` — unlike its GET page. Any LAN device can flash arbitrary firmware (c.f. §9).

11. **[CONFIRMED-BUG] Monthly-values filename mismatch — three spellings:**
    - `setup()` reads: `String bestand = "/mvalues_" + String(x) + ".str";`
    - `writeMonth()` (HELPERS.ino) and `WRITE-MONTH` (SERIAL.ino) write: `"//mvalues_" + String(maand) + ".str"` (double slash)
    - `printFiles()` (HELPERS.ino) lists: `"/monthly_vals" + String(x) + ".str"`
    At minimum `printFiles()` is broken; depending on VFS slash normalization the monthly history may never round-trip. Also `writeMonth()` is called with `month()` on the 1st of a new month, storing the *previous* month's last reading under the *new* month's index (documented as intended "end-of-month" semantics, but fragile).

12. **[CONFIRMED] Dead action flags:** `actionFlag 25` (geo/timezone form → "recalculate") and `130` (`/api/v1/data`) have **no handlers** in `test_actionFlag()`; `healthCheck()` is fully commented out. Consequence: timezone/DST changes only take effect at day rollover (when `getTijd()` is re-called), never immediately after saving.

13. **[CONFIRMED-BUG] SX631 test-path inconsistency.** Boot test (`meterPoll()` with `bootTest`) copies `sx631RxTelegram` → `teleGram` and saves it to SPIFFS, but `actionFlag 28` (decode test file) leaves `sx631RxTelegram` **empty** — `decodeAllSX631Registers()` then zeroes the struct (`sx631.valid = false`) and MQTT format 2 publishes NaN for all extended fields. `METERPOLL-TEST` (`read_test()` in `test.ino`) likewise grabs ~100 arbitrary bytes into `teleGram`, sets `polled = true`, and never decodes anything (and would decode with `meterType`-dependent buffers anyway).

14. **[CONFIRMED-BUG] `P1_ENABLE` double-toggle in the poll path** (`loop()` raises it, `meterPoll()` immediately lowers/flushes/raises). Bytes in between are discarded by `empty_Serial1()`; works only because the SX631 restarts a telegram per rising edge. Fragile coupling between the two call sites.

15. **[SUSPECTED] SX631 CRC deliberately unchecked** (documented as intentional). Corrupted frames are accepted; the design trades integrity for compatibility with this meter's firmware. Legacy-path CRC logic itself is correct.

16. **[CONFIRMED-BUG] Config changes need a reboot but none is triggered.** `meterType`/`baudRate9600`/`rxInvert` are only applied in `setup()` (`Serial1.begin(...)`, line inversion). `/BASISCONFIG` saves to SPIFFS and returns; the device keeps running with the old UART parameters until manually rebooted.

17. **[CONFIRMED] Smaller defects / dead code inventory:**
    - `/api/v1/data`: literal string `"WiFi.SSID()"`, `meter_model` = integer `meterType` (HomeWizard sends strings — compat mismatch).
    - `ABOUT.ino`: `sprintf(temp, "...%ld...", getChipId(true))` passes a `String` **object** through varargs (undefined behavior).
    - `consoleOut()` `delay(100)` per message in websocket mode can starve the 115200 stream (§3.3).
    - Portal dead code: `tryConnectFlag` never set; `wifiConnect()` would use empty global `ssid/pass` (locals shadow globals in `/wifiCon`) (§8.1).
    - `ACTIONS.ino` f28: `strncpy(readCRC, teleGram + len-4, 4)` lacks NUL termination (§4.4).
    - Dead/unused: `P1_MAXLINELENGTH` macro, `Hash.h`, EEPROM handlers, `sun_setrise()` results (computed, discarded), `healthCheck()`, `loginAdmin()`, `UPDATE_FORM`/`LOGPAGE` assets, `/TEST` route ("zigbee" text from a sibling project), `waitSerial1Available()` in production paths, `Mqtt_Format == 5` check in the console test (formats only 0–3), `Preferences`/`EEPROM` includes.
    - Help/doc drift: MQTT help claims "Format 2 has the retainflag set" (retain is always false); `Mqtt_Port[5]` limits ports to 4 digits; `Mqtt_outTopic[26]`/`Mqtt_inTopic[26]` limit topics to 25 chars despite the `MQTT.ino` banner; `Serial1` boot message always prints "115200" even when 9600 is configured; `legende.ino` pin legend describes a NodeMCU, not the ESP32-C3 super-mini.
    - `handle_Serial()` (SERIAL.ino): requires ≥13 bytes before parsing commands, sets `diagNose = 2` as a side effect, and its `DELETE-FILE=` loop reads `InputBuffer_Serial[len+1]` (one past the terminator) — minor off-by-one.

### C. Confirmed vs suspected — summary table

| # | Finding | Status |
|---|---|---|
| A1 | `sx631.q_imp`/`sx631.q_exp` missing → build fails | **CONFIRMED (compile error)** |
| A2 | Duplicate `OBIS_*` defines | CONFIRMED |
| A3 | Bluetooth `#error` / `PSACrypto.h` portability | SUSPECTED |
| A4 | Mixed line endings / "FINAL" wholesale replacement | CONFIRMED |
| B5 | JS `aP1 = PWRC1 + PWRR2` typo | CONFIRMED-BUG |
| B6 | NaN into `/get.Data` & `/api/v1/data`; dead "n/a" check | CONFIRMED-BUG |
| B7 | `/API/TELEGRAM` strcat overflow + wrong buffer for type 3 | CONFIRMED-BUG |
| B8 | `txBuffer[50]` WS console overflow | CONFIRMED-BUG |
| B9 | ISR busy-wait ~9 s; `value == 11` wipe trigger | CONFIRMED-BUG |
| B10 | OTA POST unauthenticated, redirect w/o return | CONFIRMED-BUG |
| B11 | `/mvalues_` vs `//mvalues_` vs `/monthly_vals_` | CONFIRMED-BUG |
| B12 | Dead `actionFlag` 25/130, healthCheck removed | CONFIRMED |
| B13 | SX631 test-file decode path broken (`sx631.valid=false`) | CONFIRMED-BUG |
| B14 | `P1_ENABLE` double toggle discarding bytes | CONFIRMED-BUG (low impact) |
| B15 | SX631 CRC not validated | SUSPECTED (intentional) |
| B16 | Serial/baud config change requires manual reboot | CONFIRMED-BUG |
| B17 | Misc (varargs String, portal dead code, f3 NaN, doc drift…) | Mixed, see list |

---

## Architecture / Data-Flow Diagram

### Component map

```text
                       ┌──────────────────────────────────────────────────────────┐
                       │            ESP32C3_P1METER_v0_4.ino (globals)            │
                       │  meter.*  sx631.*  MVALS[13]  config buffers  flags      │
                       └──────────────────────────────────────────────────────────┘
        SPIFFS (NVS)        │              │               │                │
  ┌──────────────────┐      │              │               │                │
  │ /wificonfig.json │◄─────┤   UART1 (GPIO3 RX, GPIO2 TX) │                │
  │ /basisconfig.json│      │   P1_ENABLE = GPIO5          │                │
  │ /mqttconfig.json │      ▼              ▼               ▼                ▼
  │ /testFile.txt    │  AAA_READ_SERIAL  AAA-PARSE-     MQTT.ino        ASYSERVER.ino
  │ /mvalues_N.str   │  ┌────────────┐   TELEGRAM.ino   ┌──────────┐    ┌───────────────┐
  └──────────────────┘  │meterPoll() │   ┌───────────┐   │PubSub-   │    │AsyncWebServer │
        ▲               │read_into_  │   │parseTele- │   │Client    │    │ /  /MENU ...  │
        │ save/load     │array()     │   │gram()     │   │mqttConn. │    │ /get.Data     │
  SPIFFS_RW.ino ───────►│state mach. │   │powerTo-   │   │sendMqtt  │    │ /api/v1/data  │
  handleforms.ino       │decodeTele- │   │Watts()    │   │f1/f2/f3  │    │ /FWUPDATE OTA │
  (web forms)           │gram()      │   │parseGas-  │   └────┬─────┘    │ /handleFwupd. │
                        │decodeAllSX │   │Value()    │        │          │ start_asyserver│
                        │631Registers│   └─────┬─────┘        │          └───────┬───────┘
                        └─────┬──────┘         │              │                  │
                              │ meterType==3 ? │              │                  │
                              │  SX631 path    │ legacy path  │                  │
                              ▼                ▼              ▼                  ▼
                     sx631RxTelegram[8192]  teleGram[1060]  MQTT broker     Browser (front page
                     + readCRC[5]           (CRC16-checked) (Domoticz /     JAVA_SCRIPT, console,
                     (CRC received,         via CRC16.h     JSON /          events SSE, ws console)
                      not checked)          legacy only     ThingSpeak)
```

### Telegram data flow (per poll)

```text
loop() / API /MQTT {"poll"} / console POLL-METER
        │  (autopoll: pollFreq s; on-demand only if pollFreq==0)
        ▼
actionFlag 26 ─► test_actionFlag() ─► meterPoll()             [or direct call from loop()]
        │
        ├─ P1_ENABLE HIGH ──► SX631 starts streaming 115200 8N1 inverted
        │
        ▼
read_into_array()                     [AAA_READ_SERIAL.ino]
  flush stale ─ sync '/' ─ collect until '!' ─ read 4 hex CRC chars
  timeouts: 5 s start / 500 ms inter-byte gap ─► fail => polled=false
        │  success: frame in sx631RxTelegram (type 3) or teleGram (legacy), readCRC[5]
        ▼
decodeTelegram()
  ├─ meterType==3 ─► meter.* from sx631GetValue("1-0:x.y.z")  (kW→W ×1000)
  │                 └► decodeAllSX631Registers() ─► sx631.*  (U, I, P, PF, f, …)
  │                    CRC: received but NOT validated
  └─ legacy ─► CRC16 vs readCRC ─► strip CRC ─► parseTelegram()  [AAA-PARSE-TELEGRAM.ino]
                                  line-prefix OBIS match ─► meter.* (powerToWatts)
        │  eventSend(2)  (SSE → front page reload)   timeStamp updated
        ▼
sendMqtt(false); sendMqtt(true)         [MQTT.ino]
  f1 Domoticz electric + gas │ f2 JSON + SX631 extras (⚠ q_imp/q_exp: compile error)
  f3 ThingSpeak fields 1..7
        │
        ▼
Browser: GET /get.Data ─► JSON {CON_*, RET_*, PWRC1..3, PWRR1..3, enC/enR, gAs, timestamp}
         GET /api/v1/data ─► HomeWizard-style JSON (net per-phase W)
         GET /API/TELEGRAM ─► raw teleGram (⚠ empty for meterType 3, ⚠ strcat growth)
```

### Key structural takeaway

```text
SX631 support = parallel fast-path bolted onto the original project:
  reception + extended decode  ──  AAA_READ_SERIAL.ino   (new, LF, "FINAL")
  legacy line parse            ──  AAA-PARSE-TELEGRAM.ino (new, LF, "FINAL")
  dispatch                     ──  meterType == 3 ? SX631 : legacy
  bridge                       ──  MeterData meter.*  (shared struct, W per phase)
  seam defects                 ──  MQTT f2 (q_imp/q_exp), /API/TELEGRAM (teleGram vs
                                   sx631RxTelegram), test-file decode path, docs.
```

---

---

## 11. Post-Audit Fix Log (applied after this report was produced)

After the read-only audit, the following confirmed defects were fixed in-place. The fixes are intentionally **minimal and additive-light** (the firmware was already near ~90 % flash capacity), so no new libraries, pages or features were introduced.

### Compile blockers

| # | File | Fix |
|---|---|---|
| F1 | `MQTT.ino` | Removed the `sx631.q_imp` / `sx631.q_exp` references from MQTT format 2 (fields and arguments). Reactive power is **not** part of `SX631Registers`, so MQTT f2 now emits only the fields that exist. |
| F2 | `ESP32C3_P1METER_v0_4.ino` | Removed the ESP32-classic leftover `#error Bluetooth is not enabled!...` block (this sketch does not use Bluetooth; on ESP32-C3 without BT it aborted the build). |
| F3 | `ESP32C3_P1METER_v0_4.ino` | Removed `#include "PSACrypto.h"` (never used), the duplicate include block, `<esp_task_wdt.h>`/`<rtc_wdt.h>` (never used), and the `<EEPROM.h>`-related dead code block. Consolidated the includes into a single clean block. |
| F4 | `PORTAL_WIFI.ino` | Removed two `esp_task_wdt_reset()` no-op calls (the loopTask is never subscribed to the TWDT in this sketch and the header include was removed). |
| F5 | `Hash.h` | Deleted. It was never included anywhere (`//#include <Hash.h>`), i.e. pure dead weight in the repo. |

### Data-integrity fixes

| # | File | Fix |
|---|---|---|
| F6 | `EXTERNAL.ino` | `/API/TELEGRAM` no longer `strcat`s into `teleGram[1060]` (unbounded growth / buffer overflow, and wrong buffer for meterType 3). It now serves `sx631RxTelegram` for `meterType == 3` and `teleGram` otherwise, as a read-only `String`, and returns `404`/`403` instead of `200` for invalid/unauthorised requests. |
| F7 | `AA_CONSOLE.ino` | `handleWebSocketMessage()` no longer copies an unbounded frame into `txBuffer[50]`; it clamps to `sizeof(txBuffer)-1` after the frame-completeness check and writes a proper terminator. Also fixed `//mvalues_` -> `/mvalues_`. |
| F8 | `HELPERS.ino` | `round0..round3` guard against NaN/inf before the `(int)` casts (undefined-behaviour values like `-2147483.648` no longer reach the JSON serializers). `showDir()`/`writeMonth()` now use the same `/mvalues_N.str` name everywhere (the double-slash `//mvalues_` variant never matched the files). |
| F9 | `SERIAL.ino` | Same `//mvalues_` -> `/mvalues_` fix; `DELETE-FILE=` loop now stops at `i<len` instead of reading past the string terminator. |
| F10 | `ACTIONS.ino` | `actionFlag == 28` (test-telegram decode): `readCRC` extraction guards against short buffers and always NUL-terminates. |
| F11 | `MQTT.ino` | MQTT format 3/ThingSpeak fields now pass through `mqttFinite()` (and gas through `isfinite()`), so NaN never serialises into the payload. Format-2 fallbacks use `0.0f` instead of `NAN` when `sx631.valid` is false. |
| F12 | `AAA_HOMEPAGE.H` | Front-page JS: `var aP1 = PWRC1 + PWRR2;` -> `PWRC1 + PWRR1` (phase-1 popup displayed L2 return power). |

### Behavioural / consistency fixes

| # | File | Fix |
|---|---|---|
| F13 | `ASYSERVER.ino` | `/api/v1/data`: `meter_model` is now a string (`"SANXING SX631 / S34U18"`) like the HomeWizard JSON it mimics; `wifi_ssid` reports `WiFi.SSID()` instead of the literal text `"WiFi.SSID()"`; removed the dead `actionFlag = 130` line (no handler anywhere). |
| F14 | `ASYSERVER.ino` | `/handleFwupdate` (OTA) now requires HTTP basic auth (`admin` / `pswd`) and `return`s after redirect instead of falling through to flash logic. Previously any LAN device could upload arbitrary firmware. |
| F15 | `ABOUT.ino` | Replaced the varargs misuse `sprintf(temp, "%ld", getChipId(true))` (passing a `String` object where a `long` was expected = UB) with `(long)ESP.getEfuseMac() & 0xFFFFFF`. |
| F16 | `handleforms.ino` + `ACTIONS.ino` | Timezone/DST form no longer leaves `actionFlag = 25` unhandled: the flag is now handled in `test_actionFlag()` and runs `getTijd()` in loop context (the project's deferred-action pattern), instead of blocking the async_tcp task with an NTP round-trip. |
| F17 | `ESP32C3_P1METER_v0_4.ino` | `loop()` no longer toggles `P1_ENABLE` HIGH -> LOW -> HIGH around `meterPoll()`; `meterPoll()` owns the pin (LOW -> flush -> HIGH -> ... -> LOW). Removes the request-line glitch that discarded early telegram bytes. |
| F18 | `MQTT_CONFIG.ino` / `AAA-PARSE-TELEGRAM.ino` / `AAA_READ_SERIAL.ino` | Documentation drift fixed: retain-flag claim on the MQTT config page, duplicated OBIS `#define` set removed from `AAA-PARSE-TELEGRAM.ino` (single source of truth in the main sketch), and the stale "reactive registers decoded" banner comments. |

### Verification status

- **Static checks passed**: no remaining references to `q_imp`/`q_exp`, `PSACrypto`, `esp_task_wdt`/`rtc_wdt` (active code), `EEPROM` (active code), `prefs.`, `actionFlag 130`, or `//mvalues_`; include block consolidated; braces balanced per file.
- **Real toolchain compile not performed**: neither `arduino-cli` nor the ESP32 Arduino core is available in this sandbox, so no absolute flash/RAM figure can be reported here. The fixes deliberately *remove* code (duplicate includes, dead struct fields, no-op calls), so they cannot increase flash/RAM usage relative to the previously ~90 %-full build.
- Compile on an Arduino-ESP32 **3.0.7** core with the *Minimal SPIFFS with OTA* partition scheme (as documented in the sketch header) to obtain the final flash/RAM numbers.

---

*End of report. Originally generated from a read-only audit; section 11 documents the subsequent in-place fixes that were committed and pushed to the repository.*
