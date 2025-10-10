// module_themes.cpp
// 9-segment themes spinner with per-theme font support.
// Publishes JSON to MQTT topic "spinner/themes" when focus changes.

#include "module_themes.h"
#include "shared.h"

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <FastLED.h>
#include <AS5600.h>

// === FONTS ===
// You must have the chosen font header(s) in Fonts/ for compilation.
// Replace with fonts you actually have if necessary.
#include <Fonts/Helvetica_Neue_Condensed_Bold24pt7b.h>

namespace {
  // Toggle this to get serial debug of raw angle + mapping info.
  static const bool DEBUG = false;

  const uint8_t SLICE_COUNT = 9;
  const char* pubTopic = "spinner/themeA";

  // Labels (exactly 9)
  const char* themes[SLICE_COUNT] = {
    "Play",
    "Learn",
    "Sleep",
    "Read",
    "Run",
    "Ride",
    "Create",
    "Party",
    "Eat"
  };

  // Per-theme LED colours
  const CRGB themeColors[SLICE_COUNT] = {
    CRGB::Orange,
    CRGB::Green,
    CRGB::Yellow,
    CRGB::Purple,
    CRGB::Cyan,
    CRGB::Red,
    CRGB::Blue,
    CRGB::White,
    CRGB::HotPink
  };

  // Per-theme font pointers
  // If you only have one font, set all entries to that font pointer.
  const GFXfont* themeFonts[SLICE_COUNT] = {
    &Helvetica_Neue_Condensed_Bold24pt7b, // PLAY
    &Helvetica_Neue_Condensed_Bold24pt7b, // LEARN
    &Helvetica_Neue_Condensed_Bold24pt7b, // SLEEP
    &Helvetica_Neue_Condensed_Bold24pt7b, // READ
    &Helvetica_Neue_Condensed_Bold24pt7b, // RUN
    &Helvetica_Neue_Condensed_Bold24pt7b, // RIDE
    &Helvetica_Neue_Condensed_Bold24pt7b, // CREATE
    &Helvetica_Neue_Condensed_Bold24pt7b, // PARTY
    &Helvetica_Neue_Condensed_Bold24pt7b  // EAT
  };

  // If a particular theme's glyphs cause visual vertical shift,
  // add a per-theme Y offset (positive moves text down).
  // Tweak values after viewing.
  int themeYoffsets[SLICE_COUNT] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };

  // small mapping offset (if you want to rotate which physical angle maps to slice0)
  // To calibrate, set RAW_OFFSET to the `raw` value printed by Serial when DEBUG==true
  uint16_t RAW_OFFSET = 170;

  int lastIdx = -1;
  bool active = false;
} // namespace

// draw centered with the theme-specific font (safe fallback if null)
static void drawCenteredWithFont(const char* txt, const GFXfont* f, int yNudge) {
  display.clearDisplay();

  if (f) display.setFont(f);
  else display.setFont(&Helvetica_Neue_Condensed_Bold24pt7b); // fallback

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  int16_t bx, by; uint16_t bw, bh;
  display.getTextBounds(txt, 0, 0, &bx, &by, &bw, &bh);

  int16_t cx = (SCREEN_W - (int)bw) / 2 - bx;
  int16_t cy = (SCREEN_H - (int)bh) / 2 - by + yNudge;
  display.setCursor(cx, cy);
  display.print(txt);
  display.display();
}

void module_themes_setup() {
  lastIdx = -1;
  active = false;
  if (leds && NUM_PIXELS > 0) { leds[0] = CRGB::Black; FastLED.show(); }
  display.clearDisplay(); display.display();
  if (DEBUG) Serial.println("module_themes: setup");
}

void module_themes_activate() {
  lastIdx = -1; // force first update
  active = true;
  if (leds && NUM_PIXELS > 0) { leds[0] = CRGB::Black; FastLED.show(); }
  if (DEBUG) Serial.println("module_themes: activated");
}

void module_themes_deactivate() {
  active = false;
  if (leds && NUM_PIXELS > 0) { leds[0] = CRGB::Black; FastLED.show(); }
  display.clearDisplay(); display.display();
  if (DEBUG) Serial.println("module_themes: deactivated");
}

void module_themes_loop() {
  if (!active) return;

  // read raw and map to slice
  uint16_t raw = as5600.readAngle();
  int32_t shifted = int32_t(raw) - int32_t(RAW_OFFSET);
  if (shifted < 0) shifted += 4096;
  else if (shifted >= 4096) shifted -= 4096;

  uint8_t slice = (shifted * SLICE_COUNT) / 4096;
  uint8_t idx = slice % SLICE_COUNT;

  if (DEBUG) {
    Serial.print("module_themes: raw="); Serial.print(raw);
    Serial.print(" shifted="); Serial.print(shifted);
    Serial.print(" slice="); Serial.print(slice);
    Serial.print(" idx="); Serial.println(idx);
    // Helpful calibration hint: set RAW_OFFSET to the printed `raw` to make this physical pos map to idx=0.
    Serial.print("module_themes: (hint) to make current position idx=0 set RAW_OFFSET = "); Serial.println(raw);
  }

  if ((int)idx != lastIdx) {
    lastIdx = idx;

    // LED
    if (leds && NUM_PIXELS > 0) {
      leds[0] = themeColors[idx];
      FastLED.show();
    }

    // display with optional per-theme y-nudge
    int yn = (idx >=0 && idx < (int)SLICE_COUNT) ? themeYoffsets[idx] : 0;
    drawCenteredWithFont(themes[idx], themeFonts[idx], yn);

    // mqtt publish (JSON)
    char payload[128];
    snprintf(payload, sizeof(payload), "{\"name\":\"%s\",\"idx\":%d}", themes[idx], idx);
    
    if (mqttClient.connected()) {
      mqttClient.publish(pubTopic, payload);
      if (DEBUG) {
        Serial.print("module_themes: mqtt -> "); Serial.println(payload);
      } else {
        Serial.print("module_themes: focused: "); Serial.println(themes[idx]);
      }
    } else {
      Serial.print("module_themes: focused: ");
      Serial.println(themes[idx]);
      Serial.println("module_themes: mqtt not connected, publish skipped");
    }
  }

  delay(20);
}