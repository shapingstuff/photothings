// shared.h
#pragma once

#include <Wire.h>
#include <AS5600.h>
#include <FastLED.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <PubSubClient.h>

// -- configuration constants (declared here so main.ino + modules can use them)
extern const uint8_t SDA_PIN;
extern const uint8_t SCL_PIN;
extern const uint8_t PIXEL_PIN;
extern const uint16_t NUM_PIXELS;
extern const uint16_t SCREEN_W;
extern const uint16_t SCREEN_H;
extern const uint8_t OLED_RESET;

// -- shared objects (defined/constructed in main.ino)
extern AS5600 as5600;               // magnetic encoder
extern CRGB *leds;                  // pointer to LED array created in main
extern Adafruit_SSD1306 display;    // OLED display (constructed in main)
extern WiFiClient wifiClient;
extern PubSubClient mqttClient;

// helper: publish wrapper (optional)
inline bool publishJson(const char* topic, const char* payload) {
  if (!mqttClient.connected()) return false;
  return mqttClient.publish(topic, payload);
}