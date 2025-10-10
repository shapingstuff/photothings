// module_timeline.cpp
// Simple age timeline (0 -> 3 years).
// Early entries use weeks (1w..4w) then months 1..36 ("1m".."36m").
// Encoder angle (AS5600) maps to a label index; the focused label is centered.
// Written to plug into your existing shared.h (as5600, display, leds, NUM_PIXELS).

#include "module_timeline.h"
#include "shared.h"

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <AS5600.h>
#include <FastLED.h>

#include <Fonts/Roboto_Condensed_Medium9pt7b.h>

namespace {

// ----- CONFIG -----
static const bool DEBUG = false;
bool ENABLE_TIMELINE = true;

// How many logical positions (derived from labels)
static int labelsCount = 0;

// horizontal spacing in pixels between adjacent labels (adjust to taste)
const int PIXELS_PER_STEP = 120; // set to ~screen width per step if you want big jumps

// vertical layout
const int TIMELINE_BASE_Y = 16;      // baseline Y for labels (increase to push labels down)
const int TICK_H = 10;               // optional tick height under each label
const int STATUS_Y_OFFSET = 6;       // offset for bottom status line

// fonts
const GFXfont* LABEL_FONT = &Roboto_Condensed_Medium9pt7b;

// if true, invert encoder mapping (clockwise -> earlier)
const bool REVERSE_DIRECTION = false;

// neopixel colors (if present)
const CRGB HIGHLIGHT_COLOR = CRGB::White;
const CRGB DEFAULT_COLOR   = CRGB::Grey;

// ----- STATE -----
static bool active = false;
static std::vector<String> labels; // label strings, length = labelsCount

} // namespace


// ----- Helpers -----
static void buildLabels()
{
  labels.clear();
  // add first 1..4 weeks
  for (int w = 1; w <= 4; ++w) {
    labels.push_back(String(w) + "w");
  }
  // add months 1..36
  for (int m = 1; m <= 36; ++m) {
    // optionally avoid duplicating 4w ~ 1m: keep both; user can adjust
    labels.push_back(String(m) + "m");
  }
  labelsCount = (int)labels.size();
  if (DEBUG) {
    Serial.print("module_timeline: built ");
    Serial.print(labelsCount);
    Serial.println(" labels");
  }
}

// Map AS5600 raw angle (0..4095) to 0..labelsCount-1
static int angleToIndex()
{
  uint16_t raw = as5600.readAngle();
  uint16_t val = REVERSE_DIRECTION ? (4095 - raw) : raw;
  // Guard: labelsCount should be > 0
  if (labelsCount <= 0) return 0;
  int idx = (int)((uint32_t)val * (uint32_t)labelsCount / 4096U);
  if (idx < 0) idx = 0;
  if (idx >= labelsCount) idx = labelsCount - 1;
  return idx;
}

static void drawCenteredTextAtX(const String &txt, int x, int y)
{
  int16_t bx, by; uint16_t bw, bh;
  display.getTextBounds(txt.c_str(), 0, 0, &bx, &by, &bw, &bh);
  int cx = x - ((int)bw / 2) - bx;
  display.setCursor(cx, y);
  display.print(txt);
}

// ----- Module API -----
void module_timeline_enable(bool on) { ENABLE_TIMELINE = on; }
bool module_timeline_isEnabled() { return ENABLE_TIMELINE; }

void module_timeline_setup()
{
  display.setTextWrap(false);
  buildLabels();
  // initial LED
  if (leds && NUM_PIXELS > 0) {
    leds[0] = DEFAULT_COLOR;
    FastLED.show();
  }
  if (DEBUG) Serial.println("module_timeline: setup complete");
}

void module_timeline_activate()
{
  active = true;
  if (!labels.size()) buildLabels();
  if (leds && NUM_PIXELS > 0) {
    leds[0] = DEFAULT_COLOR;
    FastLED.show();
  }
  if (DEBUG) Serial.println("module_timeline: activated");
}

void module_timeline_deactivate()
{
  active = false;
  if (leds && NUM_PIXELS > 0) {
    leds[0] = CRGB::Black;
    FastLED.show();
  }
  if (DEBUG) Serial.println("module_timeline: deactivated");
}

void module_timeline_loop()
{
  if (!ENABLE_TIMELINE || !active) return;
  if (labelsCount <= 0) return;

  const int centerX = SCREEN_W / 2;
  int focused = angleToIndex();

  // optionally set LED color when hitting key milestones (e.g., 12m, 24m, 36m)
  if (leds && NUM_PIXELS > 0) {
    // if label ends with "12m" or "24m" or "36m" highlight
    String lab = labels[focused];
    if (lab.endsWith("12m") || lab.endsWith("24m") || lab.endsWith("36m")) leds[0] = HIGHLIGHT_COLOR;
    else leds[0] = DEFAULT_COLOR;
    FastLED.show();
  }

  // draw background / baseline
  display.clearDisplay();
  display.drawFastHLine(0, TIMELINE_BASE_Y + TICK_H, SCREEN_W, SSD1306_WHITE);

  // draw labels around focused index
  display.setFont(LABEL_FONT);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // we will draw all labels that land within screen +/- margin
  const int margin = PIXELS_PER_STEP + 40;
  for (int i = 0; i < labelsCount; ++i) {
    int x = centerX + (i - focused) * PIXELS_PER_STEP;
    if (x < -margin || x > SCREEN_W + margin) continue;

    // label text centered at x
    drawCenteredTextAtX(labels[i], x, TIMELINE_BASE_Y);

    // optional tick under text
    int tickTop = TIMELINE_BASE_Y + 8;
    display.drawFastVLine(x, tickTop, TICK_H, SSD1306_WHITE);
  }

  // small "Now:" indicator above center
  String nowLbl = "Now: " + labels[focused];
  display.setFont(LABEL_FONT);
  display.setTextSize(1);
  drawCenteredTextAtX(nowLbl, centerX, TIMELINE_BASE_Y - 12);

  // bottom status line (counts / hint)
  char status[48];
  int months = 0;
  // If label is e.g. "3w" we report weeks, if "12m" -> convert to years if divisible by 12
  String lab = labels[focused];
  if (lab.endsWith("w")) {
    snprintf(status, sizeof(status), "Age: %s", lab.c_str());
  } else if (lab.endsWith("m")) {
    int mval = lab.substring(0, lab.length()-1).toInt();
    if (mval % 12 == 0) {
      snprintf(status, sizeof(status), "Age: %dyr (%dm)", mval/12, mval);
    } else {
      snprintf(status, sizeof(status), "Age: %dm", mval);
    }
  } else {
    snprintf(status, sizeof(status), "Age: %s", lab.c_str());
  }
  int16_t sx, sy; uint16_t sw, sh;
  display.getTextBounds(status, 0, 0, &sx, &sy, &sw, &sh);
  int statusY = TIMELINE_BASE_Y + TICK_H + 14 + STATUS_Y_OFFSET;
  display.setCursor((SCREEN_W - (int)sw) / 2 - sx, statusY);
  display.print(status);

  display.display();
  delay(10);
}