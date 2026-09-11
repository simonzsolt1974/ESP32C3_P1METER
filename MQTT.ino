/*
 * AAA-MQTT-SX631-FINAL.ino
 *
 * MQTT part of the ESP32-C3 P1 meter project.
 *
 * FINAL fixes for Sanxing SX631 / S34U18:
 *  - Keeps the original mqttConnect() behaviour.
 *  - MQTT topics stay within the project's 25-character topic buffers.
 *  - MQTT JSON input is parsed with the supplied payload length.
 *  - For Mqtt_Format 1, actual power is TOTAL 3-phase power, not L1 only.
 *  - For Mqtt_Format 2, the original fields are preserved and SX631
 *    3-phase values are added (source-labelled: direct meter values when
 *    the meter transmits them, U x I x PF calculated values otherwise).
 *  - Meters without per-phase power registers (E.ON Hungary SX631/S34U18)
 *    publish the CALCULATED phase power (U x I x PF from 32/52/72,
 *    31/51/71 and 33/53/73.7.0) in phase_power_l1_w/l2_w/l3_w together
 *    with the source metadata phase_power_source ("meter" | "calculated"
 *    | "unavailable") and the boolean phase_power_calculated, so Home
 *    Assistant can never mistake calculated values for direct meter
 *    values. The direct import/export register split (p1_con..p3_ret)
 *    stays null for such meters and the real signed total from
 *    OBIS 1.7.0 - 2.7.0 is published in "total_power_w".
 *  - For Mqtt_Format 3, the original ThingSpeak field layout is preserved.
 *  - sendMqtt(true) is used only for the gas/extra Domoticz packet in
 *    format 1. Formats 2 and 3 publish the electricity packet only once;
 *    this prevents the original meterPoll() double-call from publishing
 *    duplicate JSON messages.
 *  - Gas is not published as NaN, because NaN is invalid JSON.
 *  - Publish is attempted only after mqttConnect() succeeds.
 */

bool mqttConnect() {
  if (MQTT_Client.connected()) {
    consoleOut("mqtt was connected");
    return true;
  }

  consoleOut(F("\nconnect mqtt"));

  uint8_t retry = 3;

  while (!MQTT_Client.connected()) {
    if (MQTT_Client.connect(Mqtt_Clientid, Mqtt_Username, Mqtt_Password)) {

      // Subscribe to the command topic when an MQTT input format is active.
      if (Mqtt_Format != 0) {
        if (MQTT_Client.subscribe(Mqtt_inTopic)) {
          consoleOut("subscribed to " + String(Mqtt_inTopic));
        }
      }

      consoleOut(F("mqtt connected"));
      return true;

    } else {
      if (!--retry) break;
      delay(500);
    }
  }

  consoleOut(F("mqtt connection failed"));
  return false;
}

// *************************************************************************
//                   process received mqtt
// *************************************************************************

void MQTT_Receive_Callback(char *topic, byte *payload, unsigned int length)
{
  JsonDocument doc;

  // MQTT payload is not required to be NUL terminated.  Use its real length.
  DeserializationError error = deserializeJson(doc, payload, length);
  if (error) {
    if (diagNose) ws.textAll("mqtt no valid json ");
    return;
  }

  if (!doc["poll"].isNull()) {
    consoleOut("got message containing \"poll\"");

    if (pollFreq == 0) {
      actionFlag = 26;
      return;
    } else {
      consoleOut("forbidden, automatic polling is on");
      return;
    }
  }
  else {
    consoleOut("polling = automatic, skipping");
  }
}

// *************************************************************************
//                              send MQTT
// *************************************************************************

// NaN is not valid JSON/ThingSpeak; replace missing readings with 0.
static float mqttFinite(float v) {
  return isfinite(v) ? v : 0.0f;
}

void sendMqtt(bool gas) {

  if (Mqtt_Format == 0) return;

  /*
   * meterPoll() calls sendMqtt(false) and sendMqtt(true).
   *
   * Format 1 has two separate Domoticz-style packets, therefore both calls
   * are useful there.
   *
   * Formats 2 and 3 are single electricity packets.  The old code published
   * the same packet twice because the gas flag was ignored in those cases.
   */
  if (gas && Mqtt_Format != 1) return;

  char Mqtt_send[128] = {0};
  snprintf(Mqtt_send, sizeof(Mqtt_send), "%s", Mqtt_outTopic);

  bool reTain = false;
  char toMQTT[1024] = {0};

  /*
   * Actual import/export power in W.
   *
   * When the meter transmits per-phase power registers (legacy meters),
   * the totals are the sum of the phases.  The E.ON Hungary SX631/S34U18
   * firmware does NOT transmit per-phase power; for that meter the real
   * SIGNED totals decoded from 1.7.0 / 2.7.0 are used, so the published
   * values match the meter exactly instead of always reading 0.
   */
  uint32_t pwrConTotal;
  uint32_t pwrRetTotal;

  /*
   * Direct per-phase import/export register split as JSON numbers, or the
   * literal "null" when the meter transmits no per-phase power registers
   * (E.ON Hungary SX631/S34U18). null is the representation this format
   * already uses for unavailable values (gas). The TOTAL is never copied
   * into the phase fields.
   */
  char p1ConStr[12], p2ConStr[12], p3ConStr[12];
  char p1RetStr[12], p2RetStr[12], p3RetStr[12];

  if (meter.pwr_phase_valid) {
    snprintf(p1ConStr, sizeof(p1ConStr), "%u", (unsigned int)meter.pwr_con[0]);
    snprintf(p2ConStr, sizeof(p2ConStr), "%u", (unsigned int)meter.pwr_con[1]);
    snprintf(p3ConStr, sizeof(p3ConStr), "%u", (unsigned int)meter.pwr_con[2]);
    snprintf(p1RetStr, sizeof(p1RetStr), "%u", (unsigned int)meter.pwr_ret[0]);
    snprintf(p2RetStr, sizeof(p2RetStr), "%u", (unsigned int)meter.pwr_ret[1]);
    snprintf(p3RetStr, sizeof(p3RetStr), "%u", (unsigned int)meter.pwr_ret[2]);
  } else {
    strcpy(p1ConStr, "null");
    strcpy(p2ConStr, "null");
    strcpy(p3ConStr, "null");
    strcpy(p1RetStr, "null");
    strcpy(p2RetStr, "null");
    strcpy(p3RetStr, "null");
  }

  /*
   * Best available SIGNED per-phase power (source priority: direct meter
   * registers first, then U x I x PF calculation) plus the source metadata
   * so consumers can label the values correctly.
   *
   * meter.pwr_calc[] is float W. Format with one decimal: the calculated
   * S34U18 values are fractional (e.g. 450.9 vs 450.8 W) and a (long) cast
   * would truncate two different phases into the same integer.
   */
  const char *phaseSrc;
  char p1PhStr[12], p2PhStr[12], p3PhStr[12];

  if (meter.pwr_phase_valid) {
    phaseSrc = "meter";
    snprintf(p1PhStr, sizeof(p1PhStr), "%.1f", (double)meter.pwr_calc[0]);
    snprintf(p2PhStr, sizeof(p2PhStr), "%.1f", (double)meter.pwr_calc[1]);
    snprintf(p3PhStr, sizeof(p3PhStr), "%.1f", (double)meter.pwr_calc[2]);
  } else if (meter.pwr_phase_calculated) {
    phaseSrc = "calculated";
    snprintf(p1PhStr, sizeof(p1PhStr), "%.1f", (double)meter.pwr_calc[0]);
    snprintf(p2PhStr, sizeof(p2PhStr), "%.1f", (double)meter.pwr_calc[1]);
    snprintf(p3PhStr, sizeof(p3PhStr), "%.1f", (double)meter.pwr_calc[2]);
  } else {
    phaseSrc = "unavailable";
    strcpy(p1PhStr, "null");
    strcpy(p2PhStr, "null");
    strcpy(p3PhStr, "null");
  }

  if (meter.pwr_phase_valid) {
    pwrConTotal =
        (uint32_t)meter.pwr_con[0] +
        (uint32_t)meter.pwr_con[1] +
        (uint32_t)meter.pwr_con[2];

    pwrRetTotal =
        (uint32_t)meter.pwr_ret[0] +
        (uint32_t)meter.pwr_ret[1] +
        (uint32_t)meter.pwr_ret[2];
  } else {
    pwrConTotal = (uint32_t)(meter.pwr_tot_con < 0 ? 0 : meter.pwr_tot_con);
    pwrRetTotal = (uint32_t)(meter.pwr_tot_ret < 0 ? 0 : meter.pwr_tot_ret);
  }

  switch (Mqtt_Format) {

    case 1:
      /*
       * Domoticz-style packet:
       *
       * electric:
       *   low import Wh; high import Wh; low export Wh; high export Wh;
       *   actual import W; actual export W
       *
       * For a 3-phase SX631, actual power is the TOTAL of L1+L2+L3.
       * This preserves the existing six-field interface.
       */
      if (!gas) {
        if (!isfinite(meter.con_lt) || !isfinite(meter.con_ht) ||
            !isfinite(meter.ret_lt) || !isfinite(meter.ret_ht)) {
          consoleOut("MQTT electric data incomplete - not published");
          return;
        }

        snprintf(
          toMQTT, sizeof(toMQTT),
          "{\"idx\":%d,\"nvalue\":0,\"svalue\":\"%.2f;%.2f;%.2f;%.2f;%lu;%lu\"}",
          el_Idx,
          meter.con_lt * 1000.0,
          meter.con_ht * 1000.0,
          meter.ret_lt * 1000.0,
          meter.ret_ht * 1000.0,
          (unsigned long)pwrConTotal,
          (unsigned long)pwrRetTotal
        );
      }
      else {
        // NaN is not valid JSON / Domoticz numeric input.
        if (!isfinite(meter.gas)) {
          consoleOut("MQTT gas value unavailable - not published");
          return;
        }

        snprintf(
          toMQTT, sizeof(toMQTT),
          "{\"idx\":%d,\"nvalue\":0,\"svalue\":\"%.3f;\"}",
          gas_Idx,
          meter.gas
        );
      }
      break;

    case 2:
      /*
       * JSON format.
       *
       * Original keys are retained for compatibility:
       *   econ_lt, econ_ht, eret_ht, eret_lt, actualp_con, actualp_ret, gas
       *
       * actualp_con / actualp_ret are now TOTAL 3-phase values.
       * total_power_w is the meter's SIGNED total (1.7.0 - 2.7.0,
       * import positive, export negative).  Per-phase fields are null
       * when the meter does not transmit per-phase power registers.
       */
      if (!isfinite(meter.con_lt) || !isfinite(meter.con_ht) ||
          !isfinite(meter.ret_lt) || !isfinite(meter.ret_ht)) {
        consoleOut("MQTT electric data incomplete - not published");
        return;
      }

      snprintf(
        toMQTT, sizeof(toMQTT),
        "{"
        "\"econ_lt\":%.3f,"
        "\"econ_ht\":%.3f,"
        "\"eret_ht\":%.3f,"
        "\"eret_lt\":%.3f,"
        "\"actualp_con\":%lu,"
        "\"actualp_ret\":%lu,"
        "\"total_power_w\":%ld,"
        "\"gas\":%s,"
        "\"p1_con\":%s,\"p2_con\":%s,\"p3_con\":%s,"
        "\"p1_ret\":%s,\"p2_ret\":%s,\"p3_ret\":%s,"
        "\"phase_power_source\":\"%s\","
        "\"phase_power_calculated\":%s,"
        "\"phase_power_l1_w\":%s,\"phase_power_l2_w\":%s,\"phase_power_l3_w\":%s,"
        "\"v1\":%.2f,\"v2\":%.2f,\"v3\":%.2f,"
        "\"i1\":%.3f,\"i2\":%.3f,\"i3\":%.3f,"
        "\"frequency\":%.3f,\"power_factor\":%.4f,"
        "\"e_imp\":%.3f,\"e_exp\":%.3f"
        "}",
        meter.con_lt,
        meter.con_ht,
        meter.ret_ht,
        meter.ret_lt,
        (unsigned long)pwrConTotal,
        (unsigned long)pwrRetTotal,
        (long)((int32_t)meter.pwr_tot_con - (int32_t)meter.pwr_tot_ret),
        isfinite(meter.gas) ? String(meter.gas, 3).c_str() : "null",
        p1ConStr,
        p2ConStr,
        p3ConStr,
        p1RetStr,
        p2RetStr,
        p3RetStr,
        phaseSrc,
        meter.pwr_phase_calculated ? "true" : "false",
        p1PhStr,
        p2PhStr,
        p3PhStr,
        // 0 instead of NaN: NaN would serialize as "nan" (invalid JSON).
        sx631.valid ? sx631.voltage_l1 : 0.0f,
        sx631.valid ? sx631.voltage_l2 : 0.0f,
        sx631.valid ? sx631.voltage_l3 : 0.0f,
        sx631.valid ? sx631.current_l1 : 0.0f,
        sx631.valid ? sx631.current_l2 : 0.0f,
        sx631.valid ? sx631.current_l3 : 0.0f,
        sx631.valid ? sx631.frequency : 0.0f,
        sx631.valid ? sx631.power_factor : 0.0f,
        sx631.valid ? sx631.e_imp : (meter.con_lt + meter.con_ht),
        sx631.valid ? sx631.e_exp : (meter.ret_lt + meter.ret_ht)
      );
      break;

    case 3:
      /*
       * Original ThingSpeak layout is intentionally preserved:
       * field1..field7 + status.
       *
       * field1 = T1 import kWh
       * field2 = T2 import kWh
       * field3 = T1 export kWh
       * field4 = T2 export kWh
       * field5 = total import W
       * field6 = total export W
       * field7 = gas
       */
      snprintf(
        toMQTT, sizeof(toMQTT),
        "field1=%.3f&field2=%.3f&field3=%.3f&field4=%.3f&field5=%lu&field6=%lu&field7=%.3f&status=MQTTPUBLISH",
        mqttFinite(meter.con_lt),
        mqttFinite(meter.con_ht),
        mqttFinite(meter.ret_lt),
        mqttFinite(meter.ret_ht),
        (unsigned long)pwrConTotal,
        (unsigned long)pwrRetTotal,
        isfinite(meter.gas) ? meter.gas : 0.0
      );
      reTain = false;
      break;

    default:
      consoleOut("unknown MQTT format");
      return;
  }

  if (mqttConnect()) {
    if (MQTT_Client.publish(Mqtt_send, toMQTT, reTain)) {
      consoleOut("mqtt publish OK");
    } else {
      consoleOut("mqtt publish FAILED");
    }
  }
}
