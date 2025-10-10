// module_friend.cpp
#include "module_friend.h"
#include "shared.h"

// Fonts used by the display — keep these includes as in your original file
#include <Fonts/Rabito_font30pt7b.h>  // large
#include <Fonts/Rabito_font34pt7b.h>  // medium
#include <Fonts/Rabito_font28pt7b.h>  // small
#include <Fonts/Rabito_font26pt7b.h>  // smaller

// Module-local constants
namespace {
  const bool DEBUG_RAW = false;
  const uint16_t RAW_OFFSET = 2019;  // calibrate to your home marker
  const uint8_t HOME_SLICE = 0;

  // friend names & assets (same as your sketch)
  const char* friends[] = { "Asha", "Esta", "Seth", "Bo", "Bronn", "School" };
  const uint8_t numFriends = sizeof(friends) / sizeof(friends[0]);
  const uint8_t SLICE_COUNT = numFriends;

  // colours + fonts — note: CRGB is from FastLED
  const CRGB friendColors[] = {
    CRGB::Red, CRGB::Green, CRGB::Blue,
    CRGB::Yellow, CRGB::Cyan, CRGB::Magenta
  };
  const GFXfont* nameFonts[] = {
    &Rabito_font34pt7b,
    &Rabito_font34pt7b,
    &Rabito_font34pt7b,
    &Rabito_font34pt7b,
    &Rabito_font28pt7b,
    &Rabito_font26pt7b
  };

  static_assert(sizeof(friendColors)/sizeof(friendColors[0]) == sizeof(friends)/sizeof(friends[0]),
                "Array lengths must match");
  static_assert(sizeof(nameFonts)/sizeof(nameFonts[0]) == sizeof(friends)/sizeof(friends[0]),
                "Array lengths must match");

  const char* pubTopic = "spinner/friend";

  // module-local state (file-scoped)
  int lastIdx = -1;
}

// ----- helper functions -----
static void updateDisplay(int idx) {
  // use shared display object (constructed in main)
  display.clearDisplay();
  display.setFont(nameFonts[idx]);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  const char* name = friends[idx];
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(name, 0, 0, &x1, &y1, &w, &h);

  int16_t cx = (SCREEN_W - w) / 2 - x1;
  int16_t cy = (SCREEN_H - h) / 2 - y1;
  display.setCursor(cx, cy);
  display.print(name);
  display.display();
}

// ----- module API -----
void module_friend_setup() {
  // module initial state - assume shared hardware (Wire, AS5600, FastLED, display, mqtt) already initialised
  lastIdx = -1;
  // ensure LED safe state
  leds[0] = CRGB::Black;
  FastLED.show();
  // clear display
  display.clearDisplay();
  display.display();
  Serial.println("module_friend: setup done");
}

void module_friend_activate() {
  // reset index so first read forces an update
  lastIdx = -1;
  leds[0] = CRGB::Black;
  FastLED.show();
  Serial.println("module_friend: activated");
}

void module_friend_deactivate() {
  // tidy up hardware state when switching away
  leds[0] = CRGB::Black;
  FastLED.show();
  // optionally publish a "stopped" message or disconnect MQTT if needed
  Serial.println("module_friend: deactivated");
}

void module_friend_loop() {
  // ensure mqtt alive — we expect mqttClient defined in shared.h
  if (!mqttClient.connected()) {
    // don't block here — main.ino should reconnect, but we'll print for debug
    if (DEBUG_RAW) Serial.println("module_friend: mqtt disconnected");
  }
  mqttClient.loop();

  // 1) read raw angle from shared as5600
  uint16_t raw = as5600.readAngle();

  // 2) apply calibration offset & wrap
  int32_t shifted = int32_t(raw) - RAW_OFFSET;
  if (shifted < 0) shifted += 4096;
  else if (shifted >= 4096) shifted -= 4096;

  // 3) compute slice 0…SLICE_COUNT-1
  uint8_t slice = (shifted * SLICE_COUNT) / 4096;

  // 4) map slice → friend index
  uint8_t idx = (slice + SLICE_COUNT - HOME_SLICE) % SLICE_COUNT;

  if (DEBUG_RAW) {
    Serial.printf("raw=%u shifted=%ld slice=%u idx=%u\n",
                  raw, shifted, slice, idx);
  }

  // 5) update LED & display & MQTT on change
  if (idx != lastIdx) {
    lastIdx = idx;
    // LED
    leds[0] = friendColors[idx];
    FastLED.show();
    // display
    updateDisplay(idx);
    // publish JSON payload
    char payload[64];
    snprintf(payload, sizeof(payload), "{\"name\":\"%s\"}", friends[idx]);
    if (mqttClient.connected()) {
      mqttClient.publish(pubTopic, payload);
      Serial.print("MQTT ▶ ");
      Serial.println(payload);
    } else {
      Serial.println("MQTT publish skipped — not connected");
    }
  }

  delay(20); // tiny sleep so we don't hammer CPU; acceptable for this loop
}