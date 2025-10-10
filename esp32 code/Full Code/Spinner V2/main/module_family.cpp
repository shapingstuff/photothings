// module_family.cpp
#include "module_family.h"
#include "shared.h"

// fonts
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>

namespace {
  // Module local config (use same values you had)
  const bool DEBUG_RAW      = false;
  const uint16_t RAW_OFFSET = 2636;
  const uint8_t  HOME_SLICE = 0;
  const int SLICE_COUNT    = 6;   // 6 family segments

  // Data
  const char* familyNames[] = {
    "Shannon","Peter","Gillian","Mia","Joey","Cian"
  };
  const char* familyRels[] = {
    "Birth Mum","Pops","Nanny","Sister","Brother","Brother"
  };

  // 6 colours for 6 segments (fixed to match familyNames)
  const CRGB familyColors[] = {
    CRGB::Blue, CRGB::Blue, CRGB::Blue,
    CRGB::Yellow, CRGB::Yellow, CRGB::Green
  };

  static_assert(sizeof(familyNames)/sizeof(familyNames[0]) == SLICE_COUNT, "name count mismatch");
  static_assert(sizeof(familyRels)/sizeof(familyRels[0]) == SLICE_COUNT, "relation count mismatch");
  static_assert(sizeof(familyColors)/sizeof(familyColors[0]) == SLICE_COUNT, "color count mismatch");

  const int numSegments = sizeof(familyNames)/sizeof(familyNames[0]);

  const char* pubTopic = "spinner/birthfam";

  // module state
  uint16_t lastRaw = 0;
  int lastIdx = -1;
}

// helper: display update
static void updateDisplay(int idx) {
  display.clearDisplay();

  // Relationship (top half)
  display.setFont(&FreeSans12pt7b);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  int16_t x0,y0; uint16_t w0,h0;
  const char* rel  = familyRels[idx];
  display.getTextBounds(rel, 0, 0, &x0,&y0,&w0,&h0);
  int16_t rx = (SCREEN_W - w0)/2 - x0;
  int16_t ry = ((SCREEN_H/2) - h0)/2 - y0;
  display.setCursor(rx, ry);
  display.print(rel);

  // Name (bottom half)
  display.setFont(&FreeSansBold12pt7b);
  display.getTextBounds(familyNames[idx], 0,0, &x0,&y0,&w0,&h0);
  int16_t nx = (SCREEN_W - w0)/2 - x0;
  int16_t ny = SCREEN_H/2 + ((SCREEN_H/2 - h0)/2) - y0;
  display.setCursor(nx, ny);
  display.print(familyNames[idx]);

  display.display();
}


// API implementations
void module_family_setup() {
  // module expects shared hardware to be initialised already (Wire, as5600, leds, display, mqttClient)
  lastRaw = as5600.readAngle();
  lastIdx = -1;
  if (leds && NUM_PIXELS > 0) {
    leds[0] = CRGB::Black;
    FastLED.show();
  }
  display.clearDisplay();
  display.display();
  Serial.println("module_family: setup complete");
}

void module_family_activate() {
  lastIdx = -1;
  if (leds && NUM_PIXELS > 0) {
    leds[0] = CRGB::Black;
    FastLED.show();
  }
  Serial.println("module_family: activated");
}

void module_family_deactivate() {
  if (leds && NUM_PIXELS > 0) {
    leds[0] = CRGB::Black;
    FastLED.show();
  }
  Serial.println("module_family: deactivated");
}

void module_family_loop() {
  // keep mqtt loop processing (main.ino should handle reconnect)
  mqttClient.loop();

  // 1) Read raw angle
  uint16_t raw = as5600.readAngle();

  // 2) Calibrate & wrap
  int32_t shifted = int32_t(raw) - int32_t(RAW_OFFSET);
  if (shifted < 0)        shifted += 4096;
  else if (shifted >= 4096) shifted -= 4096;

  // 3) Compute slice 0…(SLICE_COUNT-1)
  uint8_t slice = (shifted * SLICE_COUNT) / 4096;

  // 4) Map slice → segment index (0…numSegments-1)
  uint8_t idx = (slice + SLICE_COUNT - HOME_SLICE) % SLICE_COUNT;
  if (idx >= numSegments) idx %= numSegments;

  if (DEBUG_RAW) {
    Serial.print("raw=");     Serial.print(raw);
    Serial.print("  shifted=");Serial.print(shifted);
    Serial.print("  slice=");  Serial.print(slice);
    Serial.print("  idx=");    Serial.println(idx);
  }

  // LED color
  if (leds && NUM_PIXELS > 0) {
    leds[0] = familyColors[idx];
    FastLED.show();
  }

  // Display & MQTT only on change
  if (idx != lastIdx) {
    lastIdx = idx;
    updateDisplay(idx);

    // Publish JSON
    char payload[96];
    snprintf(payload, sizeof(payload),
             "{\"name\":\"%s\",\"relation\":\"%s\"}",
             familyNames[idx],
             familyRels[idx]);

    if (mqttClient.connected()) {
      mqttClient.publish(pubTopic, payload);
      Serial.print("MQTT ▶ ");
      Serial.println(payload);
    } else {
      Serial.println("MQTT publish skipped — not connected");
    }
  }

  delay(20);
}