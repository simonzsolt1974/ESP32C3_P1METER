/*
 * P1 TELEGRAM PARSER
 *
 * meterType:
 *   0 = no meter
 *   1 = Sagemcom XS210_D ESMR5
 *   2 = Landis Gyr E350 ZMF100
 *   3 = Sanxing SX631 / S34U18
 *
 * The SX631 uses the normal DSMR/P1 OBIS lines for the values already
 * represented by the project's MeterData structure.  Additional SX631
 * registers are decoded by decodeAllSX631Registers() in AAA_READ_SERIAL.ino.
 */

/*
 * OBIS codes shared with the main sketch.  The duplicate #define set that
 * used to live here was removed so the values cannot drift apart again.
 */

static bool lineHasObis(const char *p, const char *obis)
{
  return strncmp(p, obis, strlen(obis)) == 0;
}

void parseTelegram()
{
  consoleOut("parsing teleGram");

  // Start every accepted telegram from a known state.  This prevents a
  // missing line in one telegram from leaving an old value in RAM.
  meter.smr = 0;
  meter.con_lt = NAN;
  meter.con_ht = NAN;
  meter.ret_lt = NAN;
  meter.ret_ht = NAN;
  meter.pwr_con[0] = 0;
  meter.pwr_con[1] = 0;
  meter.pwr_con[2] = 0;
  meter.pwr_ret[0] = 0;
  meter.pwr_ret[1] = 0;
  meter.pwr_ret[2] = 0;
  meter.gas = NAN;

  const char *p = teleGram;

  while (*p)
  {
    if (lineHasObis(p, OBIS_SMR))
    {
      meter.smr = (uint8_t)parseValue(p, OBIS_SMR);
    }
    else if (lineHasObis(p, OBIS_CON_LT))
    {
      meter.con_lt = parseValue(p, OBIS_CON_LT);
    }
    else if (lineHasObis(p, OBIS_CON_HT))
    {
      meter.con_ht = parseValue(p, OBIS_CON_HT);
    }
    else if (lineHasObis(p, OBIS_RET_LT))
    {
      meter.ret_lt = parseValue(p, OBIS_RET_LT);
    }
    else if (lineHasObis(p, OBIS_RET_HT))
    {
      meter.ret_ht = parseValue(p, OBIS_RET_HT);
    }
    else if (lineHasObis(p, OBIS_POWER_C1))
    {
      meter.pwr_con[0] = powerToWatts(parseValue(p, OBIS_POWER_C1));
    }
    else if (lineHasObis(p, OBIS_POWER_R1))
    {
      meter.pwr_ret[0] = powerToWatts(parseValue(p, OBIS_POWER_R1));
    }
    else if (lineHasObis(p, OBIS_POWER_C2))
    {
      meter.pwr_con[1] = powerToWatts(parseValue(p, OBIS_POWER_C2));
    }
    else if (lineHasObis(p, OBIS_POWER_R2))
    {
      meter.pwr_ret[1] = powerToWatts(parseValue(p, OBIS_POWER_R2));
    }
    else if (lineHasObis(p, OBIS_POWER_C3))
    {
      meter.pwr_con[2] = powerToWatts(parseValue(p, OBIS_POWER_C3));
    }
    else if (lineHasObis(p, OBIS_POWER_R3))
    {
      meter.pwr_ret[2] = powerToWatts(parseValue(p, OBIS_POWER_R3));
    }
    else if (lineHasObis(p, OBIS_GAS))
    {
      meter.gas = parseGasValue(p);
    }

    while (*p && *p != '\n')
      p++;

    if (*p == '\n')
      p++;
  }

  // SX631-specific extended decoding is performed once by
  // decodeAllSX631Registers() in AAA_READ_SERIAL.ino (only for meterType 3;
  // this parser only handles the legacy meters after CRC validation).

  consoleOut("1.8.1 = " + String(meter.con_lt, 3) + " kWh");
  consoleOut("1.8.2 = " + String(meter.con_ht, 3) + " kWh");
  consoleOut("2.8.1 = " + String(meter.ret_lt, 3) + " kWh");
  consoleOut("2.8.2 = " + String(meter.ret_ht, 3) + " kWh");

  consoleOut(
    "P1=" + String(meter.pwr_con[0]) + " W " +
    "P2=" + String(meter.pwr_con[1]) + " W " +
    "P3=" + String(meter.pwr_con[2]) + " W"
  );

  consoleOut(
    "Export P1=" + String(meter.pwr_ret[0]) + " W " +
    "P2=" + String(meter.pwr_ret[1]) + " W " +
    "P3=" + String(meter.pwr_ret[2]) + " W"
  );
}

/*
 * Values in the P1 telegram are expressed in kW for the 21/41/61 and
 * 22/42/62 instantaneous active-power registers.
 * MeterData stores these values as uint16_t watts.
 *
 * Clamp instead of allowing a negative or out-of-range float conversion
 * to wrap around into a large uint16_t value.
 */
uint16_t powerToWatts(float kw)
{
  if (!isfinite(kw) || kw <= 0.0f)
    return 0;

  float watts = kw * 1000.0f;

  if (watts >= 65535.0f)
    return 65535;

  return (uint16_t)(watts + 0.5f);
}

float parseValue(const char *p, const char *obis)
{
  if (!p || !obis)
    return NAN;

  const char *v = p + strlen(obis);
  return atof(v);
}

float parseGasValue(const char *p)
{
  if (!p)
    return NAN;

  const char *first = strchr(p, '(');
  if (!first)
    return NAN;

  const char *second = strchr(first + 1, '(');
  if (!second)
    return NAN;

  return atof(second + 1);
}
