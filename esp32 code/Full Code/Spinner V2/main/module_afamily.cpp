// module_afamily.cpp
// Simple spinner module for a 4-person family ("Mum","Dad","Maddison","Maddie")
// Mirrors the style of your module_friend implementation.

#include "module_afamily.h"
#include "shared.h"

// Fonts used by the display — if you don't have these swap to fonts you do have
#include <Fonts/Rabito_font34pt7b.h>
#include <Fonts/Rabito_font28pt7b.h>
#include <Fonts/Rabito_font20pt7b.h>

namespace {

// Debug & calibration
const bool DEBUG_RAW = false;
const uint16_t RAW_OFFSET = 2019;  // calibrate to your home marker (adjust as needed)
const uint8_t HOME_SLICE = 0;      // which slice corresponds to index 0 (adjust to wheel position)

// family names & mapping
const char* family[] = { "Mum", "Dad", "Maddison", "Maddie" };
const uint8_t numFamily = sizeof(family) / sizeof(family[0]);
const uint8_t SLICE_COUNT = numFamily;

// LED colours and fonts (one per member)
const CRGB familyColors[] = {
  CRGB::Red, CRGB::Blue, CRGB::Green, CRGB::Yellow
};
const GFXfont* nameFonts[] = {
  &Rabito_font34pt7b,
  &Rabito_font34pt7b,
  &Rabito_font20pt7b,
  &Rabito_font28pt7b
};

static_assert(sizeof(familyColors)/sizeof(familyColors[0]) == sizeof(family)/sizeof(family[0]),
              "Array lengths must match");
static_assert(sizeof(nameFonts)/sizeof(nameFonts[0]) == sizeof(family)/sizeof(family[0]),
              "Array lengths must match");

// MQTT topic
const char* pubTopic = "spinner/afamily";

// state
int lastIdx = -1;
bool enabled = true;

} // namespace


// helper: draw centered name
static void updateDisplay(int idx) {
  display.clearDisplay();
  display.setFont(nameFonts[idx]);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  const char* name = family[idx];
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(name, 0, 0, &x1, &y1, &w, &h);

  int16_t cx = (SCREEN_W - (int)w) / 2 - x1;
  int16_t cy = (SCREEN_H - (int)h) / 2 - y1;
  display.setCursor(cx, cy);
  display.print(name);
  display.display();
}


// Module API
void module_afamily_enable(bool on) {
  enabled = on;
}

bool module_afamily_isEnabled() {
  return enabled;
}

void module_afamily_setup() {
  lastIdx = -1;
  // ensure LED safe state
  if (leds && NUM_PIXELS > 0) {
    leds[0] = CRGB::Black;
    FastLED.show();
  }
  // clear display
  display.clearDisplay();
  display.display();
  Serial.println("module_afamily: setup done");
}

void module_afamily_activate() {
  lastIdx = -1; // force a redraw on first loop
  if (leds && NUM_PIXELS > 0) {
    leds[0] = CRGB::Black;
    FastLED.show();
  }
  Serial.println("module_afamily: activated");
}

void module_afamily_deactivate() {
  if (leds && NUM_PIXELS > 0) {
    leds[0] = CRGB::Black;
    FastLED.show();
  }
  Serial.println("module_afamily: deactivated");
}

void module_afamily_loop() {
  if (!enabled) return;

  // keep MQTT alive
  mqttClient.loop();

  // 1) read raw angle
  uint16_t raw = as5600.readAngle();

  // 2) apply calibration offset & wrap
  int32_t shifted = int32_t(raw) - RAW_OFFSET;
  if (shifted < 0) shifted += 4096;
  else if (shifted >= 4096) shifted -= 4096;

  // 3) compute slice 0 … SLICE_COUNT-1
  uint8_t slice = (shifted * SLICE_COUNT) / 4096;

  // 4) map slice -> family index (accounting for HOME_SLICE)
  uint8_t idx = (slice + SLICE_COUNT - HOME_SLICE) % SLICE_COUNT;

  if (DEBUG_RAW) {
    Serial.printf("raw=%u shifted=%ld slice=%u idx=%u\n", raw, shifted, slice, idx);
  }

  // 5) if changed, update LED/display and publish
  if ((int)idx != lastIdx) {
    lastIdx = idx;

    // update LED
    if (leds && NUM_PIXELS > 0) {
      leds[0] = familyColors[idx];
      FastLED.show();
    }

    // update display
    updateDisplay(idx);

    // publish JSON payload
    char payload[64];
    snprintf(payload, sizeof(payload), "{\"name\":\"%s\"}", family[idx]);
    if (mqttClient.connected()) {
      mqttClient.publish(pubTopic, payload);
      Serial.print("MQTT ▶ ");
      Serial.println(payload);
    } else {
      if (DEBUG_RAW) Serial.println("MQTT publish skipped — not connected");
    }
  }

  delay(20); // small sleep to avoid hammering CPU
}