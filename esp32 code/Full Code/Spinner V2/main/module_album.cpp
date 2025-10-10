// module_album.cpp - Fixed increment version with rainbow LED
#include "module_album.h"
#include "shared.h"

#include <Arduino.h>
#include <AS5600.h>
#include <FastLED.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>

// Fonts for display
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSans9pt7b.h>

extern String currentActiveUid;
extern PubSubClient mqttClient;
extern AS5600 as5600;
extern CRGB* leds;
extern Adafruit_SSD1306 display;

namespace {

static const bool DEBUG = true;
const char* DEFAULT_ALBUM = "at3k2ggmwen1awna";

struct TagToAlbum { const char* tagUid; const char* albumId; };

TagToAlbum tagToAlbumMap[] = {
  { "C1A18949", "at3k2ggmwen1awna" },
  { "41AF8949", "at3k2guo8gcj8w5m" },
  { "F16B8949", "otheralbum" },
};
const int TAG_TO_ALBUM_COUNT = sizeof(tagToAlbumMap)/sizeof(tagToAlbumMap[0]);

uint16_t RAW_OFFSET = 0;
const unsigned long PUBLISH_DEBOUNCE_MS = 200;

// Fixed rotation approach: every N encoder positions = 1 photo change
const int POSITIONS_PER_PHOTO = 600;

// Rainbow cycle for LED
uint8_t rainbowHue = 0;  // 0-255, cycles through full spectrum

bool active = false;
int lastRawPosition = -1;
int accumulatedDelta = 0;
int totalPhotos = 0;
String activeAlbumId;
String navTopic;
String photoTopic;
unsigned long lastPublishMs = 0;

const char* albumForTag(const String &tagUid) {
  if (!tagUid.length()) return DEFAULT_ALBUM;
  for (int i = 0; i < TAG_TO_ALBUM_COUNT; ++i) {
    if (tagUid.equalsIgnoreCase(String(tagToAlbumMap[i].tagUid))) {
      return tagToAlbumMap[i].albumId;
    }
  }
  return DEFAULT_ALBUM;
}

static void buildTopicsForAlbum(const char* albumId) {
  navTopic = String("spinner/album/") + String(albumId) + "/nav";
  photoTopic = String("spinner/album/") + String(albumId) + "/photo";
  
  if (DEBUG) {
    Serial.print("module_album: navTopic -> "); Serial.println(navTopic);
    Serial.print("module_album: photoTopic -> "); Serial.println(photoTopic);
  }
}

static void publishGet() {
  if (!mqttClient.connected()) {
    if (DEBUG) Serial.println("module_album: mqtt not connected");
    return;
  }
  const char* payload = "{\"cmd\":\"get\"}";
  mqttClient.publish(navTopic.c_str(), payload);
  if (DEBUG) Serial.println("module_album: published GET");
}

static void publishNavDelta(int delta) {
  if (!mqttClient.connected()) {
    if (DEBUG) Serial.println("module_album: mqtt not connected");
    return;
  }
  const char* cmd = delta > 0 ? "next" : "prev";
  int steps = abs(delta);
  char payload[128];
  snprintf(payload, sizeof(payload), "{\"cmd\":\"%s\",\"steps\":%d}", cmd, steps);
  mqttClient.publish(navTopic.c_str(), payload);
  if (DEBUG) {
    Serial.print("module_album: published ");
    Serial.print(cmd);
    Serial.print(" steps=");
    Serial.println(steps);
  }
}

static void updateDisplay(const char* age, const char* date) {
  display.clearDisplay();

  // Age (top - large and bold)
  display.setFont(&FreeSansBold12pt7b);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  const char* ageText = (strlen(age) > 0) ? age : "—";
  int16_t x0, y0;
  uint16_t w0, h0;
  display.getTextBounds(ageText, 0, 0, &x0, &y0, &w0, &h0);
  int16_t ax = (SCREEN_W - w0) / 2 - x0;
  int16_t ay = ((SCREEN_H / 2) - h0) / 2 - y0;
  display.setCursor(ax, ay);
  display.print(ageText);

  // Date (bottom - smaller)
  display.setFont(&FreeSans9pt7b);
  if (strlen(date) > 0) {
    display.getTextBounds(date, 0, 0, &x0, &y0, &w0, &h0);
    int16_t dx = (SCREEN_W - w0) / 2 - x0;
    int16_t dy = SCREEN_H / 2 + ((SCREEN_H / 2 - h0) / 2) - y0;
    display.setCursor(dx, dy);
    display.print(date);
  }

  display.display();
  
  if (DEBUG) {
    Serial.print("Display updated: age=");
    Serial.print(ageText);
    Serial.print(" date=");
    Serial.println(date);
  }
}

} // namespace

void module_album_setup() {
  lastRawPosition = -1;
  accumulatedDelta = 0;
  active = false;
  totalPhotos = 0;
  rainbowHue = 0;
  activeAlbumId = String(DEFAULT_ALBUM);
  buildTopicsForAlbum(activeAlbumId.c_str());
  if (leds && NUM_PIXELS > 0) { leds[0] = CRGB::Black; FastLED.show(); }
  display.clearDisplay(); display.display();
  if (DEBUG) Serial.println("module_album: setup complete");
}

void module_album_activate() {
  extern void mqttDispatch(char* topic, byte* payload, unsigned int length);
  mqttClient.setCallback(mqttDispatch);
  
  const char* chosen = albumForTag(currentActiveUid);
  activeAlbumId = String(chosen);
  buildTopicsForAlbum(chosen);

  lastRawPosition = -1;
  accumulatedDelta = 0;
  active = true;
  lastPublishMs = 0;
  totalPhotos = 0;
  rainbowHue = 0;  // Start rainbow from red

  if (mqttClient.connected()) {
    bool ok = mqttClient.subscribe(photoTopic.c_str(), 0);
    if (DEBUG) {
      Serial.print("module_album: subscribe photo ");
      Serial.println(ok ? "OK" : "FAIL");
    }
  }

  if (DEBUG) Serial.println("module_album: sending GET...");
  publishGet();

  // Set LED to initial rainbow color (red)
  if (leds && NUM_PIXELS > 0) { 
    leds[0] = CHSV(rainbowHue, 255, 150);
    FastLED.show(); 
  }
  
  if (DEBUG) {
    Serial.print("module_album: activated album=");
    Serial.println(activeAlbumId);
  }
}

void module_album_deactivate() {
  if (mqttClient.connected()) {
    mqttClient.unsubscribe(photoTopic.c_str());
    if (DEBUG) Serial.println("module_album: unsubscribed");
  }
  active = false;
  totalPhotos = 0;
  if (leds && NUM_PIXELS > 0) { leds[0] = CRGB::Black; FastLED.show(); }
  display.clearDisplay();
  display.display();
  if (DEBUG) Serial.println("module_album: deactivated");
}

void module_album_onMqtt(const char* topic, const char* payload) {
  if (!active) return;

  if (DEBUG) {
    Serial.print("module_album: MQTT -> ");
    Serial.println(topic);
  }

  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    if (DEBUG) {
      Serial.print("module_album: JSON error: ");
      Serial.println(err.f_str());
    }
    return;
  }

  if (doc.containsKey("index")) {
    int idx = doc["index"] | 0;
    int photosCount = doc["photosCount"] | 0;
    const char* date = doc["date"] | "";
    const char* age = doc["age"] | "";
    
    totalPhotos = photosCount;
    
    if (DEBUG && totalPhotos > 0) {
      Serial.print("module_album: album has ");
      Serial.print(totalPhotos);
      Serial.println(" photos");
    }
    
    updateDisplay(age, date);
    
    // Advance to next rainbow color and show with subtle dim effect
    if (leds && NUM_PIXELS > 0) {
      rainbowHue += 8;  // Move 8 steps through color wheel per photo
      
      leds[0] = CHSV(rainbowHue, 255, 80);  // Dim briefly
      FastLED.show();
      delay(100);
      
      leds[0] = CHSV(rainbowHue, 255, 150);  // Return to steady brightness
      FastLED.show();
    }
  }
}

void module_album_loop() {
  if (!active) return;

  // Read raw encoder position (0-4095)
  uint16_t raw = as5600.readAngle();
  int32_t shifted = int32_t(raw) - int32_t(RAW_OFFSET);
  if (shifted < 0) shifted += 4096;
  else if (shifted >= 4096) shifted -= 4096;

  // First read - establish baseline
  if (lastRawPosition == -1) {
    lastRawPosition = shifted;
    if (DEBUG) {
      Serial.print("module_album: encoder baseline: ");
      Serial.println(shifted);
    }
    return;
  }

  // Calculate how much the encoder has moved
  int rawDelta = shifted - lastRawPosition;
  
  // Handle wraparound (going from 4095 to 0 or vice versa)
  if (rawDelta > 2048) rawDelta -= 4096;
  else if (rawDelta < -2048) rawDelta += 4096;

  // If encoder hasn't moved, nothing to do
  if (rawDelta == 0) {
    delay(10);
    return;
  }

  // Accumulate the movement
  accumulatedDelta += rawDelta;
  lastRawPosition = shifted;

  // Check if we've accumulated enough movement for a photo change
  int photosToMove = 0;
  
  if (accumulatedDelta >= POSITIONS_PER_PHOTO) {
    photosToMove = accumulatedDelta / POSITIONS_PER_PHOTO;
    accumulatedDelta = accumulatedDelta % POSITIONS_PER_PHOTO;
  } else if (accumulatedDelta <= -POSITIONS_PER_PHOTO) {
    photosToMove = accumulatedDelta / POSITIONS_PER_PHOTO;
    accumulatedDelta = accumulatedDelta % POSITIONS_PER_PHOTO;
  }

  // If we've crossed the threshold, publish the navigation command
  if (photosToMove != 0) {
    unsigned long now = millis();
    if (now - lastPublishMs >= PUBLISH_DEBOUNCE_MS) {
      if (photosToMove > 0) {
        publishNavDelta(photosToMove);
      } else {
        publishNavDelta(photosToMove);
      }
      lastPublishMs = now;
      
      if (DEBUG) {
        Serial.print("module_album: moved ");
        Serial.print(photosToMove);
        Serial.print(" photo(s), accumulated remainder: ");
        Serial.println(accumulatedDelta);
      }
    }
  }

  delay(10);
}