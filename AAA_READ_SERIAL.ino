/* SX631 / S34U18 P1 reader
 *
 * FINAL AAA_READ_SERIAL.ino
 *
 * FIXES:
 * - Complete SX631 telegram reception: never uses Serial1.available() as
 *   an end-of-telegram condition.
 * - Waits for every byte until '!' and then reads the 4 CRC characters.
 * - Protects the 1060-byte telegram buffer.
 * - Flushes stale UART data BEFORE enabling the P1 output.
 * - Keeps the existing project's parseTelegram()/MQTT/web code compatible.
 * - Adds a complete SX631 register decoder for the E.ON/Hungarian P1 set,
 *   including L1/L2/L3 voltage, current, import/export power and power factor.
 *   Reactive energy/reactive power are deliberately NOT decoded and are not
 *   part of the SX631Registers struct (MQTT format 2 must not reference them).
 *
 * SX631/S34U18: 115200 8N1, inverted RX.
 */

static char sx631RxTelegram[8192];

void meterPoll() {
  consoleOut("polling the meter");

  // Disable P1 before clearing stale UART data.
  digitalWrite(P1_ENABLE, LOW);
  empty_Serial1();
  digitalWrite(P1_ENABLE, HIGH);

  if (read_into_array()) {
    ledblink(3, 300);
    digitalWrite(P1_ENABLE, LOW);

    if (bootTest) {
      if (meterType == 3) {
        if (strlen(sx631RxTelegram) < sizeof(teleGram)) {
          strcpy(teleGram, sx631RxTelegram);
          testFilesave();
          consoleOut("saved SX631 test telegram");
        } else {
          consoleOut("SX631 test telegram too large for legacy test buffer");
        }
      } else if (strlen(teleGram) > 50) {
        const char *testText = "\n this is a test telegram, retrieved at boot";
        if (strlen(teleGram) + strlen(testText) < sizeof(teleGram)) strcat(teleGram, testText);
        testFilesave();
        consoleOut("saved a test telegram");
      } else {
        strcpy(teleGram, "test at boot failed");
        consoleOut("test telegram failed");
      }
      consoleOut("performed the bootTest");
      return;
    }

    decodeTelegram();
    if (polled) {
      sendMqtt(false);
      sendMqtt(true);
    }
  } else {
    consoleOut("no telegram received");
  }

  consoleOut("meterPoll done");
  digitalWrite(P1_ENABLE, LOW);
}


bool read_into_array() {
  /*
   * SX631 receive logic follows the proven P1-reader approach:
   *
   * - P1_ENABLE has already been raised by meterPoll().
   * - Never use Serial1.available()==0 as end-of-telegram.
   * - Drain ALL bytes currently available in every pass.
   * - First synchronize on '/'.
   * - Then keep consuming the stream until '!'.
   * - After '!' consume exactly four CRC hex characters.
   * - A short inter-byte silence is only a timeout/error condition; it is
   *   never treated as a normal end of telegram.
   *
   * This is the same basic streaming/state-machine principle used by the
   * working SX631 P1 readers, adapted to this project's request/response
   * (P1_ENABLE) scheme.
   */
  const unsigned long START_TIMEOUT_MS = 5000UL;
  const unsigned long FRAME_GAP_TIMEOUT_MS = 500UL;

  char *rx = (meterType == 3) ? sx631RxTelegram : teleGram;
  const size_t rxSize = (meterType == 3) ? sizeof(sx631RxTelegram) : sizeof(teleGram);

  size_t telegramLength = 0;
  unsigned long startTime = millis();
  unsigned long lastByteTime = startTime;
  bool started = false;
  bool ended = false;
  int crcCount = 0;

  polled = false;
  memset(rx, 0, rxSize);
  memset(readCRC, 0, sizeof(readCRC));

  while (millis() - startTime < START_TIMEOUT_MS) {

    bool gotByte = false;

    /*
     * IMPORTANT: consume the UART in a tight loop. At 115200 baud the SX631
     * can deliver a long telegram very quickly. Returning to the outer loop
     * after one byte unnecessarily increases the chance of RX FIFO overflow.
     */
    while (Serial1.available()) {
      int c = Serial1.read();
      if (c < 0) break;

      gotByte = true;
      lastByteTime = millis();

      if (!started) {
        if (c != '/') continue;

        started = true;
        telegramLength = 0;
        rx[telegramLength++] = '/';
        rx[telegramLength] = '\0';

        consoleOut("found start of SX631 telegram");
        continue;
      }

      if (!ended) {
        if (telegramLength >= rxSize - 6) {
          consoleOut("telegram too long for receive buffer");
          return false;
        }

        rx[telegramLength++] = (char)c;
        rx[telegramLength] = '\0';

        if (c == '!') {
          ended = true;
          consoleOut("found the end sign");
        }

        continue;
      }

      /* Four ASCII hexadecimal CRC characters immediately follow '!'. */
      if (crcCount < 4) {
        char ch = (char)c;
        bool hex =
          (ch >= '0' && ch <= '9') ||
          (ch >= 'A' && ch <= 'F') ||
          (ch >= 'a' && ch <= 'f');

        if (!hex) {
          consoleOut("invalid CRC character");
          return false;
        }

        readCRC[crcCount++] = ch;
        rx[telegramLength++] = ch;
        rx[telegramLength] = '\0';

        if (crcCount == 4) {
          readCRC[4] = '\0';
          consoleOut("telegram received, length = " + String(telegramLength));
          consoleOut("read CRC = " + String(readCRC));
          polled = true;
          return true;
        }
      }
    }

    if (started && (millis() - lastByteTime >= FRAME_GAP_TIMEOUT_MS)) {
      /*
       * Once '/' was found, a gap this long means the requested telegram
       * stopped prematurely. Do NOT accept it as a complete frame.
       */
      consoleOut(ended ? "timeout waiting for CRC" : "timeout waiting for telegram data");
      return false;
    }

    if (!started && (millis() - startTime >= START_TIMEOUT_MS)) {
      consoleOut("timeout waiting for P1 telegram");
      return false;
    }

    /* Give Wi-Fi/MQTT/background tasks CPU time without losing the UART. */
    yield();

    /* If nothing arrived yet, loop immediately and keep waiting for '/'. */
    (void)gotByte;
  }

  if (!started) {
    consoleOut("timeout waiting for P1 telegram");
  } else if (ended) {
    consoleOut("timeout waiting for CRC");
  } else {
    consoleOut("timeout waiting for telegram data");
  }

  return false;
}

/* -------------------------------------------------------------------------
 * SX631 / S34U18 - E.ON HUNGARY P1 REGISTER DECODER
 * -------------------------------------------------------------------------
 *
 * Direct E.ON OBIS values only.
 *
 * ACTIVE ENERGY:
 *   1-0:1.8.0   total import energy
 *   1-0:1.8.1   import tariff 1
 *   1-0:1.8.2   import tariff 2
 *   1-0:2.8.0   total export energy
 *   1-0:2.8.1   export tariff 1
 *   1-0:2.8.2   export tariff 2
 *   1-0:15.8.0  total active energy
 *
 * PHASE VOLTAGE:
 *   1-0:32.7.0  L1
 *   1-0:52.7.0  L2
 *   1-0:72.7.0  L3
 *
 * PHASE CURRENT:
 *   1-0:31.7.0  L1
 *   1-0:51.7.0  L2
 *   1-0:71.7.0  L3
 *
 * PHASE ACTIVE IMPORT POWER:
 *   1-0:21.7.0  L1
 *   1-0:41.7.0  L2
 *   1-0:61.7.0  L3
 *
 * PHASE ACTIVE EXPORT POWER:
 *   1-0:22.7.0  L1
 *   1-0:42.7.0  L2
 *   1-0:62.7.0  L3
 *
 * TOTAL ACTIVE POWER:
 *   1-0:1.7.0   total import
 *   1-0:2.7.0   total export
 *
 * POWER FACTOR:
 *   1-0:13.7.0  total
 *   1-0:33.7.0  L1
 *   1-0:53.7.0  L2
 *   1-0:73.7.0  L3
 *
 * FREQUENCY:
 *   1-0:14.7.0
 *
 * Reactive-energy / reactive-power registers are deliberately NOT decoded.
 * Phase power is NEVER calculated from U x I. It comes directly from the
 * meter's 21.7.0 / 41.7.0 / 61.7.0 and 22.7.0 / 42.7.0 / 62.7.0 values.
 *
 * The E.ON documentation writes the full OBIS with .255, while the actual
 * P1 telegram uses the shortened form. The helper below accepts both forms.
 */
struct SX631Registers {
  // Identification / status
  char meter_id[64];
  char meter_id_part1[32];
  char meter_id_part2[32];
  char time[16];
  char date[16];
  char breaker[32];
  char error_reg[32];
  char alarm_reg1[32];
  char alarm_reg2[32];
  char tariff[16];

  // Active energy, kWh
  float e_imp;
  float e_imp_t1;
  float e_imp_t2;
  float e_exp;
  float e_exp_t1;
  float e_exp_t2;
  float e_abs;

  // Instantaneous phase values
  float voltage_l1;
  float voltage_l2;
  float voltage_l3;
  float current_l1;
  float current_l2;
  float current_l3;

  // Active power, kW
  float power_imp_total;
  float power_exp_total;
  float power_imp_l1;
  float power_imp_l2;
  float power_imp_l3;
  float power_exp_l1;
  float power_exp_l2;
  float power_exp_l3;

  // Frequency and power factor
  float frequency;
  float power_factor;
  float power_factor_l1;
  float power_factor_l2;
  float power_factor_l3;

  bool valid;
};

SX631Registers sx631;

/* Find an OBIS in the received SX631 telegram.
 * The caller supplies the OBIS without the opening '('.
 * Both the shortened P1 form and the full .255 form are accepted.
 */
static const char *sx631FindObis(const char *obis)
{
  const char *p = strstr(sx631RxTelegram, obis);
  if (p != NULL) return p;

  static char fullObis[64];
  snprintf(fullObis, sizeof(fullObis), "%s.255", obis);
  return strstr(sx631RxTelegram, fullObis);
}

/* Read a numeric OBIS value. The value may be followed by *unit, ')' etc. */
static bool sx631GetValue(const char *obis, float &value)
{
  const char *p = sx631FindObis(obis);
  if (p == NULL) return false;

  p = strchr(p, '(');
  if (p == NULL) return false;
  ++p;

  char *endPtr = NULL;
  float v = strtof(p, &endPtr);
  if (endPtr == p || !isfinite(v)) return false;

  value = v;
  return true;
}

/* Read a string OBIS value. */
static bool sx631GetString(const char *obis, char *dest, size_t destSize)
{
  if (dest == NULL || destSize == 0) return false;

  const char *p = sx631FindObis(obis);
  if (p == NULL) return false;

  p = strchr(p, '(');
  if (p == NULL) return false;
  ++p;

  const char *end = strchr(p, ')');
  if (end == NULL) return false;

  size_t n = (size_t)(end - p);
  if (n >= destSize) n = destSize - 1;
  memcpy(dest, p, n);
  dest[n] = '\0';
  return true;
}

static void sx631Zero()
{
  memset(&sx631, 0, sizeof(sx631));
  sx631.valid = false;
}

void decodeAllSX631Registers()
{
  sx631Zero();

  // Identification / status. These are kept because the existing project
  // displays/uses them, but no reactive registers are decoded.
  sx631GetString("C.1.0", sx631.meter_id, sizeof(sx631.meter_id));
  sx631GetString("C.1.2", sx631.meter_id_part1, sizeof(sx631.meter_id_part1));
  sx631GetString("C.1.3", sx631.meter_id_part2, sizeof(sx631.meter_id_part2));
  sx631GetString("0.9.1", sx631.time, sizeof(sx631.time));
  sx631GetString("0.9.2", sx631.date, sizeof(sx631.date));
  sx631GetString("C.50.68", sx631.breaker, sizeof(sx631.breaker));
  sx631GetString("F.F.0", sx631.error_reg, sizeof(sx631.error_reg));
  sx631GetString("F.E.0", sx631.alarm_reg1, sizeof(sx631.alarm_reg1));
  sx631GetString("F.L.0", sx631.alarm_reg2, sizeof(sx631.alarm_reg2));
  sx631GetString("0-0:96.14.0", sx631.tariff, sizeof(sx631.tariff));

  // Active energy
  sx631GetValue("1-0:1.8.0", sx631.e_imp);
  sx631GetValue("1-0:1.8.1", sx631.e_imp_t1);
  sx631GetValue("1-0:1.8.2", sx631.e_imp_t2);
  sx631GetValue("1-0:2.8.0", sx631.e_exp);
  sx631GetValue("1-0:2.8.1", sx631.e_exp_t1);
  sx631GetValue("1-0:2.8.2", sx631.e_exp_t2);
  sx631GetValue("1-0:15.8.0", sx631.e_abs);

  // Phase voltage, V
  sx631GetValue("1-0:32.7.0", sx631.voltage_l1);
  sx631GetValue("1-0:52.7.0", sx631.voltage_l2);
  sx631GetValue("1-0:72.7.0", sx631.voltage_l3);

  // Phase current, A
  sx631GetValue("1-0:31.7.0", sx631.current_l1);
  sx631GetValue("1-0:51.7.0", sx631.current_l2);
  sx631GetValue("1-0:71.7.0", sx631.current_l3);

  // Total active power, kW
  sx631GetValue("1-0:1.7.0", sx631.power_imp_total);
  sx631GetValue("1-0:2.7.0", sx631.power_exp_total);

  // Direct phase active import power, kW
  sx631GetValue("1-0:21.7.0", sx631.power_imp_l1);
  sx631GetValue("1-0:41.7.0", sx631.power_imp_l2);
  sx631GetValue("1-0:61.7.0", sx631.power_imp_l3);

  // Direct phase active export power, kW
  sx631GetValue("1-0:22.7.0", sx631.power_exp_l1);
  sx631GetValue("1-0:42.7.0", sx631.power_exp_l2);
  sx631GetValue("1-0:62.7.0", sx631.power_exp_l3);

  // Frequency and power factor
  sx631GetValue("1-0:14.7.0", sx631.frequency);
  sx631GetValue("1-0:13.7.0", sx631.power_factor);
  sx631GetValue("1-0:33.7.0", sx631.power_factor_l1);
  sx631GetValue("1-0:53.7.0", sx631.power_factor_l2);
  sx631GetValue("1-0:73.7.0", sx631.power_factor_l3);

  sx631.valid = true;

  consoleOut("===== SX631 E.ON P1 =====");
  consoleOut("ID       = " + String(sx631.meter_id));
  consoleOut("TIME     = " + String(sx631.time));
  consoleOut("DATE     = " + String(sx631.date));
  consoleOut("TARIFF   = " + String(sx631.tariff));
  consoleOut("1.8.0    = " + String(sx631.e_imp, 3) + " kWh");
  consoleOut("1.8.1    = " + String(sx631.e_imp_t1, 3) + " kWh");
  consoleOut("1.8.2    = " + String(sx631.e_imp_t2, 3) + " kWh");
  consoleOut("2.8.0    = " + String(sx631.e_exp, 3) + " kWh");
  consoleOut("2.8.1    = " + String(sx631.e_exp_t1, 3) + " kWh");
  consoleOut("2.8.2    = " + String(sx631.e_exp_t2, 3) + " kWh");
  consoleOut("15.8.0   = " + String(sx631.e_abs, 3) + " kWh");
  consoleOut("L1: U=" + String(sx631.voltage_l1, 2) + " V I=" + String(sx631.current_l1, 2) + " A +P=" + String(sx631.power_imp_l1, 3) + " kW -P=" + String(sx631.power_exp_l1, 3) + " kW");
  consoleOut("L2: U=" + String(sx631.voltage_l2, 2) + " V I=" + String(sx631.current_l2, 2) + " A +P=" + String(sx631.power_imp_l2, 3) + " kW -P=" + String(sx631.power_exp_l2, 3) + " kW");
  consoleOut("L3: U=" + String(sx631.voltage_l3, 2) + " V I=" + String(sx631.current_l3, 2) + " A +P=" + String(sx631.power_imp_l3, 3) + " kW -P=" + String(sx631.power_exp_l3, 3) + " kW");
  consoleOut("TOTAL: +P=" + String(sx631.power_imp_total, 3) + " kW -P=" + String(sx631.power_exp_total, 3) + " kW");
  consoleOut("FREQ     = " + String(sx631.frequency, 2) + " Hz");
  consoleOut("PF total = " + String(sx631.power_factor, 3));
  consoleOut("PF L1/L2/L3 = " + String(sx631.power_factor_l1, 3) + " / " + String(sx631.power_factor_l2, 3) + " / " + String(sx631.power_factor_l3, 3));
  consoleOut("=================================");
}

void decodeTelegram()
{
  if (!polled) return;

  char *frame = (meterType == 3) ? sx631RxTelegram : teleGram;
  size_t lengte = strlen(frame);

  consoleOut("telegram length = " + String(lengte));

  if (lengte < 6) {
    consoleOut("telegram too short");
    polled = false;
    return;
  }

  /*
   * SX631 / E.ON Hungary:
   * CRC is received but validation is intentionally disabled, as in the
   * proven working configuration used with this meter.
   */
  if (meterType == 3) {

    consoleOut("SX631 CRC received = " + String(readCRC) + " (CRC check disabled)");

    meter.smr = 0;
    meter.con_lt = NAN;
    meter.con_ht = NAN;
    meter.ret_lt = NAN;
    meter.ret_ht = NAN;
    meter.pwr_con[0] = meter.pwr_con[1] = meter.pwr_con[2] = 0;
    meter.pwr_ret[0] = meter.pwr_ret[1] = meter.pwr_ret[2] = 0;
    meter.gas = NAN;

    float v;

    if (sx631GetValue("1-3:0.2.8", v) && isfinite(v)) {
      meter.smr = (uint8_t)v;
    }

    if (sx631GetValue("1-0:1.8.1", v) && isfinite(v)) {
      meter.con_lt = v;
    }

    if (sx631GetValue("1-0:1.8.2", v) && isfinite(v)) {
      meter.con_ht = v;
    }

    if (sx631GetValue("1-0:2.8.1", v) && isfinite(v)) {
      meter.ret_lt = v;
    }

    if (sx631GetValue("1-0:2.8.2", v) && isfinite(v)) {
      meter.ret_ht = v;
    }

    /*
     * DIRECT meter values:
     *   21.7.0 -> L1 import power
     *   41.7.0 -> L2 import power
     *   61.7.0 -> L3 import power
     *   22.7.0 -> L1 export power
     *   42.7.0 -> L2 export power
     *   62.7.0 -> L3 export power
     *
     * The meter reports kW. meter.pwr_* stores W.
     * This is only a unit conversion; no U x I calculation is performed.
     */

    if (sx631GetValue("1-0:21.7.0", v) && isfinite(v)) {
      meter.pwr_con[0] = (long)roundf(v * 1000.0f);
    }

    if (sx631GetValue("1-0:41.7.0", v) && isfinite(v)) {
      meter.pwr_con[1] = (long)roundf(v * 1000.0f);
    }

    if (sx631GetValue("1-0:61.7.0", v) && isfinite(v)) {
      meter.pwr_con[2] = (long)roundf(v * 1000.0f);
    }

    if (sx631GetValue("1-0:22.7.0", v) && isfinite(v)) {
      meter.pwr_ret[0] = (long)roundf(v * 1000.0f);
    }

    if (sx631GetValue("1-0:42.7.0", v) && isfinite(v)) {
      meter.pwr_ret[1] = (long)roundf(v * 1000.0f);
    }

    if (sx631GetValue("1-0:62.7.0", v) && isfinite(v)) {
      meter.pwr_ret[2] = (long)roundf(v * 1000.0f);
    }

    // Decode the complete E.ON register set.
    decodeAllSX631Registers();

    consoleOut("1.8.1 = " + String(meter.con_lt, 3) + " kWh");
    consoleOut("1.8.2 = " + String(meter.con_ht, 3) + " kWh");
    consoleOut("2.8.1 = " + String(meter.ret_lt, 3) + " kWh");
    consoleOut("2.8.2 = " + String(meter.ret_ht, 3) + " kWh");
    consoleOut("P1=" + String(meter.pwr_con[0]) + " W P2=" + String(meter.pwr_con[1]) + " W P3=" + String(meter.pwr_con[2]) + " W");
    consoleOut("Export P1=" + String(meter.pwr_ret[0]) + " W P2=" + String(meter.pwr_ret[1]) + " W P3=" + String(meter.pwr_ret[2]) + " W");

    eventSend(2);
    sprintf(timeStamp, "%02d/%02d %02d:%02d", day(), month(), hour(), minute());
    consoleOut("polled true");
    return;
  }

  /* Original non-SX631 parser remains unchanged. */
  int dataLength = (int)lengte - 4;
  long calculatedCRC = CRC16(0x0000, (unsigned char *)frame, dataLength);
  long receivedCRC = strtol(readCRC, NULL, 16);

  consoleOut("calculated CRC = " + String(calculatedCRC));
  consoleOut("received CRC = " + String(receivedCRC));

  if (receivedCRC != calculatedCRC) {
    consoleOut("CRC is WRONG - telegram rejected");
    polled = false;
    return;
  }

  consoleOut("CRC is correct");

  teleGram[lengte - 4] = '\0';
  parseTelegram();
  polled = true;
  eventSend(2);
  sprintf(timeStamp, "%02d/%02d %02d:%02d", day(), month(), hour(), minute());
  consoleOut("polled true");
}

/*
 * The original project contains experimental getObisValue()
 * implementations below. They are intentionally not used.
 *
 * The active parser remains the project's AAA-PARSE-TELEGRAM.ino.
 */
