#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <AS5600.h>
#include <FastLED.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/FreeSerif12pt7b.h>

#include "ModuleBase.h"
#include "MQTTManager.h"

// Defaults from your previous sketch
#define DEFAULT_SDA_PIN    5
#define DEFAULT_SCL_PIN    6
#define DEFAULT_PIXEL_PIN  2
#define DEFAULT_NUM_PIXELS 1
#define DEFAULT_OLED_RST   3

static const char* DEFAULT_PUB_TOPIC = "spinner/date/count";
static const char* DEFAULT_MAX_TOPIC = "spinner/date/count/max";

class RelativeModule : public ModuleBase {
public:
  RelativeModule(uint8_t sda = DEFAULT_SDA_PIN,
                 uint8_t scl = DEFAULT_SCL_PIN,
                 uint8_t pixelPin = DEFAULT_PIXEL_PIN,
                 uint16_t numPixels = DEFAULT_NUM_PIXELS,
                 uint8_t oledReset = DEFAULT_OLED_RST,
                 const char* pubTopic = DEFAULT_PUB_TOPIC,
                 const char* maxTopic = DEFAULT_MAX_TOPIC);

  ~RelativeModule();

  // Module interface
  void begin() override;
  void stop() override;
  void loop() override;
  void onTag(const String &uid) override;
  void onMQTT(const char* topic, const char* payload) override;

  // read raw AS5600 angle (0..4095)
  uint16_t readAS5600Raw();

private:
  // compile-time slice count
  enum { SLICE_COUNT = 12 };

  // pins / hw config
  uint8_t _sdaPin;
  uint8_t _sclPin;
  uint8_t _pixelPin;
  uint16_t _numPixels;
  uint8_t _oledReset;
  String _pubTopic;
  String _maxTopic;

  // hardware objects (created lazily in begin())
  AS5600 _as5600;
  CRGB* _leds = nullptr;
  Adafruit_SSD1306* _display = nullptr;

  // state (copied from your original sketch)
  uint16_t lastRaw = 0;
  int lastSlice = -1;
  long counter = 0;
  long maxCount = 0;
  bool dirty = false;
  uint32_t lastMovementTime = 0;
  long lastSentCounter = -1;
  unsigned long _lastLoopMs = 0;
  bool _active = false;

  // constants from your sketch
  const bool DEBUG_RAW = true;
  const uint16_t RAW_OFFSET = 4040;
  const uint16_t SEND_DELAY_MS = 20;

  // colour table (will be initialised in ctor)
  CRGB sliceColors[SLICE_COUNT];

  // helpers
  void initI2C();
  void initDisplay();
  void initLeds();
  void deinitLeds();
  void publishCounter();
  void drawSlice(int slice, int sliceRaw);
};