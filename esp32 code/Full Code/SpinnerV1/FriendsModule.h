#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <AS5600.h>
#include <FastLED.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/Rabito_font30pt7b.h>  // large
#include <Fonts/Rabito_font34pt7b.h>  // medium
#include <Fonts/Rabito_font28pt7b.h>  // small

#include "ModuleBase.h"
#include "MQTTManager.h"

// Hardware defaults (match your original sketch)
#define FRIENDS_SDA_PIN    5
#define FRIENDS_SCL_PIN    6
#define FRIENDS_PIXEL_PIN  2   // compile-time pin used by FastLED template
#define FRIENDS_NUM_PIXELS 1
#define FRIENDS_OLED_RST   3
#define FRIENDS_OLED_ADDR  0x3D

#define FRIENDS_COLOR_ORDER GRB
#define FRIENDS_LED_TYPE WS2812B

// Default MQTT topic (same as your sketch)
static const char* FRIENDS_PUB_TOPIC = "spinner/friend";

class FriendsModule : public ModuleBase {
public:
  // constructor: (sda, scl, pubTopic) - you can override pixel / oled by editing macros above
  FriendsModule(uint8_t sda = FRIENDS_SDA_PIN,
                uint8_t scl = FRIENDS_SCL_PIN,
                const char* pubTopic = FRIENDS_PUB_TOPIC);

  ~FriendsModule();

  // Module interface
  void begin() override;
  void stop() override;
  void loop() override;
  void onTag(const String &uid) override;
  void onMQTT(const char* topic, const char* payload) override;

private:
  // pins/config
  uint8_t _sda;
  uint8_t _scl;
  String _pubTopic;

  // hardware (created in begin())
  AS5600 _as5600;
  CRGB* _leds = nullptr;                 // FastLED buffer allocated in begin()
  Adafruit_SSD1306* _display = nullptr;  // allocated in begin()

  // state & constants (matching your original)
  static constexpr uint16_t RAW_OFFSET = 2019;
  static constexpr bool DEBUG_RAW = true;

  static constexpr const char* FRIENDS_LIST[6] = { "Asha", "Esta", "Seth", "Bo", "Bronn", "" };
  static const CRGB FRIENDS_COLORS[6];
  static const GFXfont* FRIENDS_FONTS[6];

  const int _numFriends = 6;
  int _lastIdx = -1;

  // runtime
  bool _active = false;
  unsigned long _lastLoopMs = 0;

  // helpers (copied from your sketch)
  void initI2C();
  void initLeds();
  void initDisplay();
  void deinitLeds();
  void updateDisplay(int idx);
  void publishFriend(int idx);
};