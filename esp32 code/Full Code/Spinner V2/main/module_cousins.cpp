// module_cousins.cpp
#include "module_cousins.h"
#include "shared.h"

// Fonts used by the display — match your friend module's choices
#include <Fonts/Rabito_font30pt7b.h>  // large
#include <Fonts/Rabito_font34pt7b.h>  // medium
#include <Fonts/Rabito_font28pt7b.h>  // small
#include <Fonts/Rabito_font26pt7b.h>  // smaller

// Module-local constants and state
namespace {
  const bool DEBUG_RAW = false;

  // Calibrate these to fit your wheel's physical alignment.
  // RAW_OFFSET is the AS5600 raw value that corresponds to your "home" position.
  // HOME_SLICE is the slice index that should map to cousin index 0.
  const uint16_t RAW_OFFSET = 2019;  // tweak for your wheel
  const uint8_t HOME_SLICE = 0;

  // Cousin list (4 cousins). Change names as needed.
  const char* cousins[] = {
    "Max",
    "Xander",
    "Lincoln",
    "Lucas"
  };
  const uint8_t numCousins = sizeof(cousins) / sizeof(cousins[0]);
  const uint8_t SLICE_COUNT = numCousins;

  // LED colours (one per cousin)
  const CRGB cousinColors[] = {
    CRGB::Magenta, CRGB::Orange, CRGB::Blue, CRGB::Green
  };

  // Per-name fonts (choose sizes that look good for each name)
  const GFXfont* nameFonts[] = {
    &Rabito_font34pt7b,
    &Rabito_font26pt7b,
    &Rabito_font26pt7b,
    &Rabito_font28pt7b
  };

  static_assert(sizeof(cousinColors)/sizeof(cousinColors[0]) == sizeof(cousins)/sizeof(cousins[0]),
                "Array lengths must match for cousinColors vs cousins");
  static_assert(sizeof(nameFonts)/sizeof(nameFonts[0]) == sizeof(cousins)/sizeof(cousins[0]),
                "Array lengths must match for nameFonts vs cousins");

  const char* pubTopic = "spinner/cousin";

  // module-local state
  int lastIdx = -1;
}

// ----- helper: render centered name for given cousin index -----
static void updateDisplayForCousin(int idx) {
  display.clearDisplay();
  display.setFont(nameFonts[idx]);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  const char* name = cousins[idx];
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(name, 0, 0, &x1, &y1, &w, &h);

  int16_t cx = (SCREEN_W - w) / 2 - x1;
  int16_t cy = (SCREEN_H - h) / 2 - y1;
  display.setCursor(cx, cy);
  display.print(name);
  display.display();
}

// ----- Module API -----
void module_cousins_setup() {
  lastIdx = -1;

  // set LED safe state
  if (leds && NUM_PIXELS > 0) {
    leds[0] = CRGB::Black;
    FastLED.show();
  }

  // clear display
  display.clearDisplay();
  display.display();

  Serial.println("module_cousins: setup done");
}

void module_cousins_activate() {
  // force a redraw on first loop
  lastIdx = -1;
  if (leds && NUM_PIXELS > 0) {
    leds[0] = CRGB::Black;
    FastLED.show();
  }
  Serial.println("module_cousins: activated");
}

void module_cousins_deactivate() {
  // tidy up hardware state when switching away
  if (leds && NUM_PIXELS > 0) {
    leds[0] = CRGB::Black;
    FastLED.show();
  }
  Serial.println("module_cousins: deactivated");
}

void module_cousins_loop() {
  // keep mqtt alive (main loop handles reconnects)
  mqttClient.loop();

  // 1) read raw angle from shared AS5600
  uint16_t raw = as5600.readAngle();

  // 2) apply calibration offset & wrap
  int32_t shifted = int32_t(raw) - RAW_OFFSET;
  if (shifted < 0) shifted += 4096;
  else if (shifted >= 4096) shifted -= 4096;

  // 3) compute slice 0…(SLICE_COUNT-1)
  uint8_t slice = (shifted * SLICE_COUNT) / 4096;

  // 4) map slice → cousin index (0..numCousins-1)
  uint8_t idx = (slice + SLICE_COUNT - HOME_SLICE) % SLICE_COUNT;

  if (DEBUG_RAW) {
    Serial.printf("module_cousins: raw=%u shifted=%ld slice=%u idx=%u\n",
                  raw, shifted, slice, idx);
  }

  // 5) update LED & display & MQTT only on change
  if (idx != lastIdx) {
    lastIdx = idx;

    // LED
    if (leds && NUM_PIXELS > 0) {
      // safe-bound check
      if (idx < (int)(sizeof(cousinColors)/sizeof(cousinColors[0])))
        leds[0] = cousinColors[idx];
      else
        leds[0] = CRGB::White;
      FastLED.show();
    }

    // display
    updateDisplayForCousin(idx);

    // publish JSON payload
    char payload[64];
    snprintf(payload, sizeof(payload), "{\"name\":\"%s\",\"relation\":\"cousin\"}", cousins[idx]);
    if (mqttClient.connected()) {
      mqttClient.publish(pubTopic, payload);
      Serial.print("MQTT ▶ ");
      Serial.println(payload);
    } else {
      if (DEBUG_RAW) Serial.println("module_cousins: mqtt not connected; publish skipped");
    }
  }

  delay(20); // small delay to avoid busy-loop
}