// module_date.cpp
// Date wheel module with "future" mode (inverted display) and multi-step fast-spin behavior.
//
// Replace your existing module_date.cpp with this file. It expects the following globals
// to come from shared.h / main.ino:
//   AS5600 as5600;
//   Adafruit_SSD1306 display;
//   CRGB *leds; (or CRGB leds[] / NUM_PIXELS)
//   PubSubClient mqttClient;
//   constants: SCREEN_W, SCREEN_H, NUM_PIXELS
//
// Serial commands available while module active:
//   c    -> calibrate RAW_OFFSET to current raw reading
//   p    -> print diagnostic
//   (Other future tuning available by changing constants below)

#include "module_date.h"
#include "shared.h"

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <FastLED.h>
#include <WiFi.h>
#include <time.h>

// font used for large year drawing
#include <Fonts/FreeMonoBold24pt7b.h>

namespace {

// ===== CONFIG =====
bool ENABLE_FUTURE = true;           // runtime toggle via module_date_enable()

const bool DEBUG_RAW = false;         // set true for serial diagnostics
uint16_t RAW_OFFSET = 2071;           // calibrate with 'c' command if needed
const uint8_t JAN_SLICE = 0;

const int SLICE_COUNT = 12;
const int MIN_YEAR = 2018;
const int MAX_YEAR = 2025;
const int START_YEAR = 2021;

// FUTURE-MODE CONFIG
const int FUTURE_STEP_YEARS = 5;
const int FUTURE_MAX_OFFSET = 20;
const int FUTURE_MIN_OFFSET = 5;
const int FUTURE_STEP_COOLDOWN_MS = 300;
const int FUTURE_SPIN_THRESHOLD = 120;      // raw delta threshold (keep)
const int FUTURE_SPIN_DT_MAX = 250;         // max ms between samples to consider
const int FUTURE_MAX_STEPS_PER_SPIN = 2;    // cap steps per spin

// NEW: require a minimum angular *velocity* (ticks per second) as well
// 1 tick = 360/4096 degrees ≈ 0.088 deg. For example 800 ticks/sec ≈ 70 deg/s.
const int FUTURE_SPIN_VELOCITY = 1600;       // ticks per second

// colors for months (LED)
const CRGB monthColors[SLICE_COUNT] = {
  CRGB::Red,    CRGB::Orange, CRGB::Yellow, CRGB::Green,
  CRGB::Cyan,   CRGB::Blue,   CRGB::Purple, CRGB::Magenta,
  CRGB::Pink,   CRGB::White,  CRGB::Lime,   CRGB::Brown
};

const char* pubTopic = "spinner/date";

// module-local state
uint16_t lastRaw = 0;
uint32_t lastRawMs = 0;
uint8_t lastMonth = 255; // sentinel
int year = START_YEAR;
int lastYearDrawn = -1;
int lastMonthSent = -1;
int lastYearSent = -1;

// future state
bool inFutureMode = false;
int futureOffsetYrs = FUTURE_MIN_OFFSET;
int futureYear = MAX_YEAR + FUTURE_MIN_OFFSET;
unsigned long lastFutureStepMs = 0;
unsigned long futureEnteredMs = 0;

// helper: compute signed delta between two AS5600 raw readings (-2047..+2048)
static int32_t signedRawDelta(uint16_t prev, uint16_t now) {
  int32_t delta = int32_t(now) - int32_t(prev);
  if (delta > 2048) delta -= 4096;
  else if (delta < -2048) delta += 4096;
  return delta;
}

// Draw the real year on the shared display (normal mode)
static void drawRealYearIfNeeded() {
  if (year != lastYearDrawn) {
    lastYearDrawn = year;
    display.clearDisplay();
    display.setFont(&FreeMonoBold24pt7b);
    display.setTextColor(SSD1306_WHITE);
    char buf[8];
    snprintf(buf, sizeof(buf), "%4d", year);
    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((SCREEN_W - w)/2 - x1,
                      (SCREEN_H - h)/2 - y1);
    display.print(buf);
    display.display();
    display.setFont();  // restore default
  }
}

// Serial commands:
//  c    -> calibrate RAW_OFFSET to current raw
//  p    -> print diagnostic
static void handleSerialCommands(uint16_t raw, int slice, int month, int32_t sdelta, unsigned long dt) {
  if (!Serial.available()) return;
  String s = Serial.readStringUntil('\n');
  s.trim();
  if (s.length() == 0) return;

  if (s.equalsIgnoreCase("c")) {
    RAW_OFFSET = raw;
    Serial.printf("module_date: RAW_OFFSET set to %u\n", RAW_OFFSET);
  } else if (s.equalsIgnoreCase("p")) {
    Serial.printf("raw=%u shifted=%ld slice=%d month=%d sdelta=%ld dt=%lu\n",
                  raw, long(int32_t(raw) - int32_t(RAW_OFFSET)), slice, month, (long)sdelta, dt);
    Serial.printf("year=%d lastMonth=%d lastMonthSent=%d\n", year, lastMonth, lastMonthSent);
    Serial.printf("inFutureMode=%d futureOffsetYrs=%d futureYear=%d\n", inFutureMode ? 1 : 0, futureOffsetYrs, futureYear);
  } else {
    Serial.printf("module_date: unknown serial cmd '%s' (c=calibrate,p=print)\n", s.c_str());
  }
}

static void enterFutureMode() {
  if (inFutureMode) return;
  inFutureMode = true;
  futureOffsetYrs = FUTURE_MIN_OFFSET;
  futureYear = MAX_YEAR + futureOffsetYrs;
  lastFutureStepMs = millis();
  futureEnteredMs = millis();

  // simple visual "transport" animation: flash LED a few times
  if (leds && NUM_PIXELS > 0) {
    for (int i=0;i<3;i++) {
      leds[0] = CRGB::White; FastLED.show(); delay(120);
      leds[0] = CRGB::Black; FastLED.show(); delay(80);
    }
  }

  // Show the entry year using white background + BLACK text (inverted look)
  display.clearDisplay();
  display.fillRect(0, 0, SCREEN_W, SCREEN_H, SSD1306_WHITE); // white background
  display.setFont(&FreeMonoBold24pt7b);
  display.setTextColor(SSD1306_BLACK); // black text on white bg
  char buf[8];
  snprintf(buf, sizeof(buf), "%4d", futureYear);
  int16_t x1,y1; uint16_t w,h;
  display.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_W - w)/2 - x1, (SCREEN_H - h)/2 - y1);
  display.print(buf);
  display.display();
}

// Exit future mode (restore normal display)
static void exitFutureMode() {
  if (!inFutureMode) return;
  inFutureMode = false;

  // exit LED flash
  if (leds && NUM_PIXELS > 0) {
    for (int i=0;i<2;i++) {
      leds[0] = CRGB::Blue; FastLED.show(); delay(120);
      leds[0] = CRGB::Black; FastLED.show(); delay(80);
    }
  }

  // clear inverted screen and force redraw in normal mode
  display.clearDisplay();
  display.display();
  lastYearDrawn = -1;
}

// Attempt to step future by `steps` increments (positive or negative direction).
static void futureAttemptStep(int direction, int steps = 1) {
  unsigned long now = millis();
  if (now - lastFutureStepMs < FUTURE_STEP_COOLDOWN_MS) return; // cooldown
  lastFutureStepMs = now;

  if (steps < 1) steps = 1;
  if (steps > FUTURE_MAX_STEPS_PER_SPIN) steps = FUTURE_MAX_STEPS_PER_SPIN;

  if (direction > 0) {
    futureOffsetYrs += FUTURE_STEP_YEARS * steps;
    if (futureOffsetYrs > FUTURE_MAX_OFFSET) futureOffsetYrs = FUTURE_MAX_OFFSET;
  } else {
    futureOffsetYrs -= FUTURE_STEP_YEARS * steps;
    if (futureOffsetYrs < FUTURE_MIN_OFFSET) futureOffsetYrs = FUTURE_MIN_OFFSET;
  }
  futureYear = MAX_YEAR + futureOffsetYrs;

  // tiny twinkle and update display (keep inverted style)
  if (leds && NUM_PIXELS > 0) {
    leds[0] = CRGB::White; FastLED.show(); delay(80);
    leds[0] = CRGB::Black; FastLED.show();
  }

  // draw inverted-style year
  display.clearDisplay();
  display.fillRect(0, 0, SCREEN_W, SCREEN_H, SSD1306_WHITE);
  display.setFont(&FreeMonoBold24pt7b);
  display.setTextColor(SSD1306_BLACK);
  char buf[8];
  snprintf(buf, sizeof(buf), "%4d", futureYear);
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_W - w)/2 - x1, (SCREEN_H - h)/2 - y1);
  display.print(buf);
  display.display();
}

// Process fast-spin input while in future mode: allow multi-step based on spin magnitude
static void handleFutureModeInput(int32_t signedDelta, unsigned long dt) {
  if (dt == 0) return;
  // too slow overall between reads is ignored
  if (dt > FUTURE_SPIN_DT_MAX) return;

  int magnitude = abs(signedDelta);
  if (magnitude < FUTURE_SPIN_THRESHOLD) return;

  int vel = int((uint32_t)magnitude * 1000u / (uint32_t)dt);
  if (DEBUG_RAW) {
    Serial.printf("FUTURE step candidate: sdelta=%ld dt=%lu vel=%d\n", (long)signedDelta, dt, vel);
  }
  if (vel < FUTURE_SPIN_VELOCITY) return;

  // compute steps (existing logic), cap it
  int steps = magnitude / FUTURE_SPIN_THRESHOLD;
  if (steps < 1) steps = 1;
  if (steps > FUTURE_MAX_STEPS_PER_SPIN) steps = FUTURE_MAX_STEPS_PER_SPIN;

  if (signedDelta > 0) {
    futureAttemptStep(+1, steps);
  } else {
    if (futureOffsetYrs <= FUTURE_MIN_OFFSET) {
      exitFutureMode();
    } else {
      futureAttemptStep(-1, steps);
    }
  }
}

} // anonymous namespace


// ---------------- Module API -----------------
void module_date_enable(bool on) {
  ENABLE_FUTURE = on;
}
bool module_date_isEnabled() {
  return ENABLE_FUTURE;
}

void module_date_setup() {
  lastRaw = as5600.readAngle();
  lastRawMs = millis();
  lastMonth = 255;
  year = START_YEAR;
  lastYearDrawn = -1;
  lastMonthSent = -1;
  lastYearSent = -1;
  inFutureMode = false;

  if (leds && NUM_PIXELS > 0) {
    leds[0] = CRGB::Black;
    FastLED.show();
  }
  display.clearDisplay();
  display.display();
  Serial.println("module_date: setup complete");
}

void module_date_activate() {
  lastMonth = 255;
  year = START_YEAR;
  lastYearDrawn = -1;
  inFutureMode = false;
  if (leds && NUM_PIXELS > 0) {
    leds[0] = CRGB::Black;
    FastLED.show();
  }
  Serial.println("module_date: activated");
}

void module_date_deactivate() {
  if (leds && NUM_PIXELS > 0) {
    leds[0] = CRGB::Black;
    FastLED.show();
  }
  inFutureMode = false;
  Serial.println("module_date: deactivated");
}

void module_date_loop() {
  // keep mqtt loop processing (main.ino handles reconnect)
  mqttClient.loop();

  unsigned long now = millis();

  // 1) read raw & compute timing
  uint16_t raw = as5600.readAngle();
  unsigned long dt = now - lastRawMs;
  if (dt == 0) dt = 1;
  int32_t sdelta = signedRawDelta(lastRaw, raw);

  // 2) calibrate & wrap
  int32_t shifted = int32_t(raw) - int32_t(RAW_OFFSET);
  if (shifted < 0) shifted += 4096;
  else if (shifted >= 4096) shifted -= 4096;

  // 3) compute slice & month (forward mapping: cw increases month)
  uint8_t slice = (shifted * SLICE_COUNT) / 4096;
  uint8_t month = ((slice - JAN_SLICE + SLICE_COUNT) % SLICE_COUNT) + 1;

  // debug print
  if (DEBUG_RAW) {
    Serial.print("raw="); Serial.print(raw);
    Serial.print(" shifted="); Serial.print(shifted);
    Serial.print(" slice="); Serial.print(slice);
    Serial.print(" month="); Serial.print(month);
    Serial.print(" sdelta="); Serial.println(sdelta);
  }

  // serial commands (pass diagnostics)
  handleSerialCommands(raw, slice, month, sdelta, dt);

  // 4) year rollover detection (month crossing)
  if (lastMonth != 255 && month != lastMonth) {
    if (lastMonth == 12 && month == 1) {
      year = min(year + 1, MAX_YEAR);
    } else if (lastMonth == 1 && month == 12) {
      year = max(year - 1, MIN_YEAR);
    }
  }
  lastMonth = month;

  // 5) decide future mode entry
if (ENABLE_FUTURE && !inFutureMode) {
  if (year >= MAX_YEAR && sdelta > 0 && (dt <= FUTURE_SPIN_DT_MAX) && (abs(sdelta) >= FUTURE_SPIN_THRESHOLD)) {
    // compute velocity in ticks per second
    int vel = int((uint32_t)abs(sdelta) * 1000u / (uint32_t)dt);
    if (DEBUG_RAW) {
      Serial.printf("FUTURE entry candidate: sdelta=%ld dt=%lu vel=%d\n", (long)sdelta, dt, vel);
    }
    if (vel >= FUTURE_SPIN_VELOCITY) {
      enterFutureMode();
    }
  }
}

  // 6) render & input handling
  if (inFutureMode) {
    // process quick spins while in future-mode
    handleFutureModeInput(sdelta, dt);

    // ambient twinkle LED
    if (leds && NUM_PIXELS > 0) {
      if ((now - futureEnteredMs) % 400 < 80) leds[0] = CRGB::White;
      else leds[0] = CRGB::Black;
      FastLED.show();
    }

    // do not publish normal timeline MQTT while in future-mode
  } else {
    // normal timeline: LED color by month
    if (leds && NUM_PIXELS > 0) {
      leds[0] = monthColors[month - 1];
      FastLED.show();
    }

    // redraw real year if needed
    drawRealYearIfNeeded();

    // publish month/year when changed
    if (month != lastMonthSent || year != lastYearSent) {
      lastMonthSent = month;
      lastYearSent  = year;
      char payload[32];
      snprintf(payload, sizeof(payload),
               "{\"month\":%d,\"year\":%d}", month, year);
      mqttClient.publish(pubTopic, payload);
      Serial.print("MQTT ▶ ");
      Serial.println(payload);
    }
  }

  // store last readings
  lastRaw = raw;
  lastRawMs = now;
}