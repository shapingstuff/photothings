// module_days.cpp
// Minimal display: show TODAY / YESTERDAY / N SLEEPS AGO
// Option: show SLEEPS/AGO as two lines (configurable).
// MQTT publishing (spinner/days) remains unchanged.

#include "module_days.h"
#include "shared.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <FastLED.h>
#include <WiFi.h>
#include <time.h>

// Fonts (edit these includes if you want different sizes)
#include <Fonts/FreeSans12pt7b.h>   // main bold (e.g. TODAY, "2 SLEEPS")
#include <Fonts/FreeSans9pt7b.h>       // second-line lighter (e.g. "AGO")
#include <Fonts/FreeSans7pt7b.h>       // optional (not used here) kept for availability

namespace {

// ===== CONFIG =====
const bool DEBUG_RAW = false;       // set true while calibrating
uint16_t RAW_OFFSET = 78;           // calibrate with 'c' if needed
const uint8_t HOME_SLICE = 6;       // which aligned slice corresponds to physical home marker
const bool REVERSE_ROTATION = true; // flip direction if needed
const int SLICE_COUNT = 7;
const char* TZ = "Europe/London";

// Set this to the slice index (0..SLICE_COUNT-1) that corresponds to MONDAY on your wheel.
// Example: if the slice that is physically Monday reads as 2, set sliceIndexForMonday = 2.
int sliceIndexForMonday = 6; // <-- change this to match your wheel

// Visual options
const bool SLEEPS_TWO_LINES = true; // if true: "N SLEEPS" / "AGO" (two lines). If false: "N SLEEPS AGO" single line.
const int LINE_GAP = 2;             // px gap between top and second line when using two-line layout

// LED colours for visual feedback (unchanged)
const CRGB sliceColors[SLICE_COUNT] = {
  CRGB::Green, CRGB::Blue, CRGB::Yellow, CRGB::Orange,
  CRGB::Purple, CRGB::Cyan, CRGB::Magenta
};

// internal state
uint16_t lastRaw = 0;
uint32_t lastRawMs = 0;
bool ntpInitialized = false;

// helper: signed delta for AS5600 wrapping
static int32_t signedRawDelta(uint16_t prev, uint16_t now) {
  int32_t d = int32_t(now) - int32_t(prev);
  if (d > 2048) d -= 4096;
  else if (d < -2048) d += 4096;
  return d;
}

// try NTP (short wait)
static void tryInitNtp() {
  if (ntpInitialized) return;
  if (WiFi.status() != WL_CONNECTED) {
    if (DEBUG_RAW) Serial.println("module_days: WiFi not connected; skipping NTP init");
    return;
  }
  setenv("TZ", TZ, 1);
  tzset();
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  uint32_t start = millis();
  while (time(nullptr) < 1600000000UL && millis() - start < 4000) delay(200);
  if (time(nullptr) >= 1600000000UL) {
    ntpInitialized = true;
    if (DEBUG_RAW) {
      struct tm nowtm; time_t now = time(nullptr); localtime_r(&now, &nowtm);
      Serial.printf("module_days: NTP OK %04d-%02d-%02d %02d:%02d:%02d\n",
                    nowtm.tm_year+1900, nowtm.tm_mon+1, nowtm.tm_mday,
                    nowtm.tm_hour, nowtm.tm_min, nowtm.tm_sec);
    }
  } else {
    if (DEBUG_RAW) Serial.println("module_days: NTP not acquired");
  }
}

// Serial commands:
//  c       -> set RAW_OFFSET = current raw (calibrate)
//  p       -> print diagnostic (raw/shifted/slice mapping)
//  M n     -> set sliceIndexForMonday = n at runtime (0..SLICE_COUNT-1)
static void handleSerialCommands(uint16_t raw) {
  if (!Serial.available()) return;
  String s = Serial.readStringUntil('\n');
  s.trim();
  if (s.length() == 0) return;

  if (s.equalsIgnoreCase("c")) {
    RAW_OFFSET = raw;
    Serial.printf("module_days: RAW_OFFSET set to %u\n", RAW_OFFSET);
  }
  else if (s.equalsIgnoreCase("p")) {
    int32_t shifted = int32_t(raw) - int32_t(RAW_OFFSET);
    if (shifted < 0) shifted += 4096;
    else if (shifted >= 4096) shifted -= 4096;
    int sliceRaw = (shifted * SLICE_COUNT) / 4096;
    int sliceAligned = (sliceRaw + SLICE_COUNT - int(HOME_SLICE)) % SLICE_COUNT;
    int slice = REVERSE_ROTATION ? (SLICE_COUNT - sliceAligned) % SLICE_COUNT : sliceAligned;
    if (slice < 0) slice += SLICE_COUNT;

    int labelWeekday = (slice - sliceIndexForMonday + 1 + 7) % 7; // 0=Sun..6=Sat
    time_t tnow = time(nullptr); struct tm tm_now; localtime_r(&tnow, &tm_now);
    int todayWday = tm_now.tm_wday;
    int daysAgo = (todayWday - labelWeekday + 7) % 7;

    Serial.printf("DIAG: raw=%u shifted=%ld sliceRaw=%d sliceAligned=%d slice=%d\n", raw, shifted, sliceRaw, sliceAligned, slice);
    Serial.printf("DIAG: sliceIndexForMonday=%d labelWeekday=%d todayWday=%d daysAgo=%d (0=Sun..6=Sat)\n",
                  sliceIndexForMonday, labelWeekday, todayWday, daysAgo);
  }
  else if (s.startsWith("M ")) {
    int n = s.substring(2).toInt();
    if (n >= 0 && n < SLICE_COUNT) {
      sliceIndexForMonday = n;
      Serial.printf("module_days: sliceIndexForMonday set to %d (temporary, recompile to persist)\n", sliceIndexForMonday);
    } else {
      Serial.printf("module_days: invalid M value '%s' (expect 0..%d)\n", s.substring(2).c_str(), SLICE_COUNT-1);
    }
  }
  else {
    Serial.printf("module_days: unknown cmd '%s' (c=calibrate,p=print,M n=setMondayIndex)\n", s.c_str());
  }
}

} // namespace


// ---- Module API ----
void module_days_enable(bool /*on*/) {}
bool module_days_isEnabled() { return true; }

void module_days_setup() {
  lastRaw = as5600.readAngle();
  lastRawMs = millis();
  tryInitNtp();

  if (leds && NUM_PIXELS > 0) { leds[0] = CRGB::Black; FastLED.show(); }
  display.clearDisplay();
  display.display();

  if (DEBUG_RAW) Serial.printf("module_days: setup raw=%u RAW_OFFSET=%u sliceIndexForMonday=%d\n",
                              lastRaw, RAW_OFFSET, sliceIndexForMonday);
}

void module_days_activate() {
  if (leds && NUM_PIXELS > 0) { leds[0] = CRGB::Black; FastLED.show(); }
  if (DEBUG_RAW) Serial.println("module_days: activated");
}

void module_days_deactivate() {
  if (leds && NUM_PIXELS > 0) { leds[0] = CRGB::Black; FastLED.show(); }
  if (DEBUG_RAW) Serial.println("module_days: deactivated");
}

void module_days_loop() {
  mqttClient.loop();

  unsigned long nowMs = millis();

  // read sensor
  uint16_t raw = as5600.readAngle();
  unsigned long dt = nowMs - lastRawMs; if (dt == 0) dt = 1;
  int32_t sdelta = signedRawDelta(lastRaw, raw);

  // serial control
  handleSerialCommands(raw);

  // calibration offset & wrap
  int32_t shifted = int32_t(raw) - int32_t(RAW_OFFSET);
  if (shifted < 0) shifted += 4096;
  else if (shifted >= 4096) shifted -= 4096;

  // compute slices
  int sliceRaw = (shifted * SLICE_COUNT) / 4096;
  int sliceAligned = (sliceRaw + SLICE_COUNT - int(HOME_SLICE)) % SLICE_COUNT;
  int slice = REVERSE_ROTATION ? (SLICE_COUNT - sliceAligned) % SLICE_COUNT : sliceAligned;
  if (slice < 0) slice += SLICE_COUNT;

  if (DEBUG_RAW) {
    Serial.printf("module_days: raw=%u shifted=%ld sliceRaw=%d sliceAligned=%d slice=%d sdelta=%ld dt=%lu\n",
                  raw, shifted, sliceRaw, sliceAligned, slice, (long)sdelta, dt);
  }

  // maybe init NTP if wifi came up later
  if (!ntpInitialized && WiFi.status() == WL_CONNECTED) tryInitNtp();

  // LED for slice
  if (leds && NUM_PIXELS > 0) { leds[0] = sliceColors[slice % SLICE_COUNT]; FastLED.show(); }

  // map slice -> weekday using sliceIndexForMonday
  int labelWeekday = (slice - sliceIndexForMonday + 1 + 7) % 7; // 0=Sun..6=Sat

  // today's weekday
  time_t tnow = time(nullptr);
  struct tm tm_now; localtime_r(&tnow, &tm_now);
  int todayWday = tm_now.tm_wday;

  // daysAgo
  int daysAgo = (todayWday - labelWeekday + 7) % 7;

  if (DEBUG_RAW) {
    Serial.printf("module_days: labelWeekday=%d todayWday=%d daysAgo=%d\n", labelWeekday, todayWday, daysAgo);
  }

  // build text to display
  char topLine[48] = {0};
  char bottomLine[48] = {0};
  bool useTwoLines = false;

  if (daysAgo == 0) {
    snprintf(topLine, sizeof(topLine), "Today");
    bottomLine[0] = '\0';
    useTwoLines = false;
  } else if (daysAgo == 1) {
    snprintf(topLine, sizeof(topLine), "Yesterday");
    bottomLine[0] = '\0';
    useTwoLines = false;
  } else {
    if (SLEEPS_TWO_LINES) {
      snprintf(topLine, sizeof(topLine), "%d Sleeps", daysAgo);
      snprintf(bottomLine, sizeof(bottomLine), "Ago");
      useTwoLines = true;
    } else {
      snprintf(topLine, sizeof(topLine), "%d Sleeps Ago", daysAgo);
      bottomLine[0] = '\0';
      useTwoLines = false;
    }
  }

// MQTT publish (PhotoPrism q) - compute normalized local date and publish once per change
if (ntpInitialized) {
  struct tm tm_target = tm_now;
  tm_target.tm_mday -= daysAgo;
  time_t t_target = mktime(&tm_target);
  struct tm tlocal; localtime_r(&t_target, &tlocal);

  char dateIso[12] = {0};
  strftime(dateIso, sizeof(dateIso), "%Y-%m-%d", &tlocal);

  // Build photoprism query WITHOUT inner quotes: taken:YYYY-MM-DD
  char ppq[32] = {0};
  snprintf(ppq, sizeof(ppq), "taken:%s", dateIso);

  static char lastDateSent[12] = "";
  if (strcmp(lastDateSent, dateIso) != 0) {
    char payload[128];
    // photoprism_q now contains no internal double-quotes, so this is valid JSON
    snprintf(payload, sizeof(payload),
             "{\"days_ago\":%d,\"date\":\"%s\",\"photoprism_q\":\"%s\"}",
             daysAgo, dateIso, ppq);
    mqttClient.publish("spinner/days", payload);
    if (DEBUG_RAW) Serial.printf("module_days: MQTT ▶ %s\n", payload);
    strncpy(lastDateSent, dateIso, sizeof(lastDateSent));
  }
} else {
  static int lastPublishedAgo = -1;
  if (lastPublishedAgo != daysAgo) {
    char payload[64];
    snprintf(payload, sizeof(payload), "{\"days_ago\":%d,\"date\":\"\"}", daysAgo);
    mqttClient.publish("spinner/days", payload);
    lastPublishedAgo = daysAgo;
    if (DEBUG_RAW) Serial.printf("module_days: MQTT ▶ %s\n", payload);
  }
}

  // --------- DRAW: center text ---------
  display.clearDisplay();

  if (!useTwoLines) {
    // single line centered
    display.setFont(&FreeSans12pt7b);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    int16_t x1, y1; uint16_t w1, h1;
    display.getTextBounds(topLine, 0, 0, &x1, &y1, &w1, &h1);
    int16_t cx = (SCREEN_W - w1) / 2 - x1;
    int16_t cy = (SCREEN_H - h1) / 2 - y1;
    display.setCursor(cx, cy);
    display.print(topLine);
  } else {
    // two-line grouped center: top in top half of group, bottom just below it with LINE_GAP
    display.setFont(&FreeSans12pt7b);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    int16_t tx, ty; uint16_t tw, th;
    display.getTextBounds(topLine, 0, 0, &tx, &ty, &tw, &th);
    int topH = th;

    display.setFont(&FreeSans9pt7b);
    display.getTextBounds(bottomLine, 0, 0, &tx, &ty, &tw, &th);
    int bottomH = th;

    int totalH = topH + LINE_GAP + bottomH;
    int16_t startY = (SCREEN_H - totalH) / 2;

    // draw top
    display.setFont(&FreeSans12pt7b);
    display.getTextBounds(topLine, 0, 0, &tx, &ty, &tw, &th);
    int16_t topX = (SCREEN_W - tw) / 2 - tx;
    int16_t topY = startY - ty;
    display.setCursor(topX, topY);
    display.print(topLine);

    // draw bottom
    display.setFont(&FreeSans12pt7b);
    display.getTextBounds(bottomLine, 0, 0, &tx, &ty, &tw, &th);
    int16_t bottomX = (SCREEN_W - tw) / 2 - tx;
    int16_t bottomY = startY + topH + LINE_GAP - ty;
    display.setCursor(bottomX, bottomY);
    display.print(bottomLine);
  }

  display.display();

  // store last readings
  lastRaw = raw;
  lastRawMs = nowMs;
}