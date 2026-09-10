/*
 * TIME / NTP — Europe/Budapest
 *
 * The system clock is kept in UTC by the ESP32 core's built-in SNTP client
 * (time.h — no extra library) and converted with the POSIX timezone rule
 * "CET-1CEST,M3.5.0,M10.5.0/3", which is exactly Europe/Budapest:
 *
 *   - CET  (UTC+1) in winter
 *   - CEST (UTC+2) from the last Sunday of March 01:00 UTC
 *   - back to CET from the last Sunday of October 01:00 UTC
 *
 * The CET/CEST switch is therefore handled by the timezone database itself:
 * no hard-coded UTC+1 offset and no local DST heuristics for the wall clock.
 *
 * TimeLib (now(), hour(), day(), ...) still drives the web pages, the
 * telegram timeStamp and the monthly-rollover logic, so after a successful
 * sync the Budapest local wall clock is mirrored into TimeLib with
 * setTime(makeTime(...)). The stored gmtOffset/DTS settings are no longer
 * used for the wall clock; sun_setrise() still reads them for its (cosmetic)
 * sunrise/sunset math.
 */
#include <time.h>

// Europe/Budapest in POSIX TZ notation (signs are inverted vs. common usage).
#define TZ_BUDAPEST "CET-1CEST,M3.5.0,M10.5.0/3"

// Minimum plausible epoch (2023-01-01): rejects a not-yet-synchronised clock.
#define NTP_MIN_EPOCH 1672531200UL

bool getTijd() {

  timeRetrieved = false; // stays false until the time is synchronised

  // (Re)start SNTP with the Budapest timezone. Non-blocking: the SNTP
  // client syncs in the background and applies CET/CEST automatically.
  configTzTime(TZ_BUDAPEST, "pool.ntp.org", "time.nist.gov");

  // Wait (max ~4 s, yielding so Wi-Fi/async_tcp stay responsive) until the
  // system clock is synchronised. getTijd() runs in setup() or in loop
  // context (actionFlag 25 / day rollover), never in the async_tcp task.
  unsigned long start = millis();
  time_t nowEpoch = time(nullptr);
  while (nowEpoch < (time_t)NTP_MIN_EPOCH && millis() - start < 4000UL) {
    delay(50);
    yield();
    nowEpoch = time(nullptr);
  }

  if (nowEpoch < (time_t)NTP_MIN_EPOCH) {
    consoleOut(F("ntp: time not synchronised yet"));
    return false;
  }

  // Convert the UTC epoch to Budapest local time and mirror it into TimeLib.
  struct tm tmNow;
  localtime_r(&nowEpoch, &tmNow);

  tmElements_t te;
  te.Year   = tmNow.tm_year + 1900 - 1970;
  te.Month  = tmNow.tm_mon + 1;
  te.Day    = tmNow.tm_mday;
  te.Hour   = tmNow.tm_hour;
  te.Minute = tmNow.tm_min;
  te.Second = tmNow.tm_sec;
  setTime(makeTime(te));  // TimeLib now() == Budapest wall clock

  dst = (tmNow.tm_isdst > 0) ? 1 : 2; // 1 = CEST (summer), 2 = CET (winter)

  timeRetrieved = true;
  #ifdef LOG
  Update_Log(1, "got time");
  #endif

  datum = day();

  delay(10);
  sun_setrise(); // to calculate moonshape, sunrise etc. and the switchtimes

  return true;
}
