#include "RelativeModule.h"
#include <Arduino.h>

RelativeModule::RelativeModule(uint8_t sda, uint8_t scl, uint8_t pixelPin, uint16_t numPixels, uint8_t oledReset, const char* pubTopic, const char* maxTopic)
  : _sdaPin(sda), _sclPin(scl), _pixelPin(pixelPin), _numPixels(numPixels),
    _oledReset(oledReset), _pubTopic(pubTopic), _maxTopic(maxTopic),
    _as5600(), _leds(nullptr), _display(nullptr)
{
  // initialise colour table (same pattern you used earlier)
  sliceColors[0]  = CRGB::Red;
  sliceColors[1]  = CRGB::Yellow;
  sliceColors[2]  = CRGB::Red;
  sliceColors[3]  = CRGB::Yellow;
  sliceColors[4]  = CRGB::Red;
  sliceColors[5]  = CRGB::Yellow;
  sliceColors[6]  = CRGB::Red;
  sliceColors[7]  = CRGB::Yellow;
  sliceColors[8]  = CRGB::Red;
  sliceColors[9]  = CRGB::Yellow;
  sliceColors[10] = CRGB::Red;
  sliceColors[11] = CRGB::Yellow;
}

RelativeModule::~RelativeModule() {
  if (_display) {
    delete _display;
    _display = nullptr;
  }
  if (_leds) {
    delete[] _leds;
    _leds = nullptr;
  }
}

void RelativeModule::initI2C() {
  Serial.printf("[RelativeModule] Wire.begin(sda=%u,scl=%u)\n", _sdaPin, _sclPin);
  Wire.begin(_sdaPin, _sclPin);
  Wire.setClock(100000UL);
  delay(5);
}

void RelativeModule::initDisplay() {
  if (!_display) {
    Serial.println("[RelativeModule] Allocating display object");
    _display = new Adafruit_SSD1306(128, 64, &Wire, _oledReset);
    if (!_display) {
      Serial.println("[RelativeModule] ERROR: display allocation failed");
      return;
    }
    if (!_display->begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
      Serial.println("[RelativeModule] ERROR: display->begin() failed");
    } else {
      _display->clearDisplay();
      _display->setTextColor(SSD1306_WHITE);
      _display->setFont(&FreeSerif12pt7b);
      _display->display();
    }
  }
}

void RelativeModule::initLeds() {
  if (!_leds) {
    Serial.printf("[RelativeModule] Allocating LED buffer count=%u\n", _numPixels);
    _leds = new CRGB[_numPixels];

    // Use the default pixel pin compile-time value for FastLED (as before).
    FastLED.addLeds<NEOPIXEL, DEFAULT_PIXEL_PIN>(_leds, _numPixels);
    FastLED.setBrightness(40); // start reasonably low
    FastLED.clear();
    FastLED.show();
  }
}

void RelativeModule::deinitLeds() {
  if (_leds) {
    FastLED.clear();
    FastLED.show();
    delete[] _leds;
    _leds = nullptr;
    delay(5);
  }
}

uint16_t RelativeModule::readAS5600Raw() {
  return _as5600.readAngle();
}

void RelativeModule::begin() {
  if (_active) {
    Serial.println("[RelativeModule] begin() called but already active");
    return;
  }

  Serial.println("[RelativeModule] begin() - starting module");
  Serial.print("[RelativeModule] free heap before init: ");
  Serial.println(ESP.getFreeHeap());

  // Setup I2C & sensor
  initI2C();
  if (!_as5600.begin()) {
    Serial.println("[RelativeModule] ERROR: AS5600 begin failed");
  }
  lastRaw = _as5600.readAngle();

  Serial.print("[RelativeModule] free heap after AS5600 init: ");
  Serial.println(ESP.getFreeHeap());

  // Display
  initDisplay();
  Serial.print("[RelativeModule] free heap after display init: ");
  Serial.println(ESP.getFreeHeap());

  // LEDs (FastLED allocation)
  initLeds();
  Serial.print("[RelativeModule] free heap after LED init: ");
  Serial.println(ESP.getFreeHeap());

  // Subscribe to maxTopic so server can tell us album max index
  MQTTManager::instance().subscribe(_maxTopic.c_str());

  // initialise local counters from defaults
  lastSlice = -1;
  counter = 0;
  maxCount = 0;
  dirty = false;
  lastMovementTime = 0;
  lastSentCounter = -1;

  _active = true;
  _lastLoopMs = millis();

  Serial.print("[RelativeModule] begin done, free heap: ");
  Serial.println(ESP.getFreeHeap());
}

void RelativeModule::stop() {
  if (!_active) {
    Serial.println("[RelativeModule] stop() called but not active");
    return;
  }

  Serial.println("[RelativeModule] stop() - clearing resources");
  // clear LEDs
  if (_leds) {
    for (uint16_t i = 0; i < _numPixels; ++i) _leds[i] = CRGB::Black;
    FastLED.show();
  }

  // free LED buffer to give other modules heap if needed
  deinitLeds();

  // free display
  if (_display) {
    _display->clearDisplay();
    _display->display();
    delete _display;
    _display = nullptr;
    delay(2);
  }

  _active = false;
}

void RelativeModule::publishCounter() {
  if (counter < 0) counter = 0;
  if (counter > maxCount) counter = maxCount;

  if (counter != lastSentCounter) {
    char payload[32];
    snprintf(payload, sizeof(payload), "%ld", counter);
    MQTTManager::instance().publish(_pubTopic.c_str(), payload);
    lastSentCounter = counter;
    if (DEBUG_RAW) {
      Serial.print("[RelativeModule] Published counter: ");
      Serial.println(payload);
    }
  }
}

void RelativeModule::drawSlice(int slice, int sliceRaw) {
  if (!_display) return;
  _display->clearDisplay();

  _display->setCursor(0, 12);
  _display->setTextSize(1);
  _display->print("Slice: ");
  _display->println(slice);

  _display->setCursor(0, 32);
  _display->setTextSize(1);
  _display->print("Counter: ");
  _display->println(counter);

  _display->display();
}

void RelativeModule::loop() {
  if (!_active) return;

  uint16_t raw = readAS5600Raw();
  int32_t shifted = int32_t(raw) - RAW_OFFSET;
  if (shifted < 0) shifted += 4096;
  else if (shifted >= 4096) shifted -= 4096;

  int slice = (shifted * SLICE_COUNT) / 4096;

  if (lastSlice >= 0 && slice != lastSlice) {
    int delta = 0;
    int32_t diff = int32_t(raw) - int32_t(lastRaw);
    if (diff > 2048) diff -= 4096;
    else if (diff < -2048) diff += 4096;
    if (diff > 0) delta = 1;
    else if (diff < 0) delta = -1;

    counter += delta;
    dirty = true;
    lastMovementTime = millis();

    if (DEBUG_RAW) {
      Serial.printf("[RelativeModule] slice changed %d -> %d, delta=%d, counter=%ld\n", lastSlice, slice, delta, counter);
    }
  }
  lastRaw = raw;
  lastSlice = slice;

  if (dirty && (millis() - lastMovementTime) > SEND_DELAY_MS) {
    publishCounter();
    dirty = false;
  }

  if (_leds) {
    uint16_t idx = (uint32_t)slice % _numPixels;
    for (uint16_t i = 0; i < _numPixels; ++i) _leds[i] = CRGB::Black;
    _leds[idx] = sliceColors[slice];
    FastLED.show();
  }

  drawSlice(slice, raw);

  delay(10);
}

void RelativeModule::onTag(const String &uid) {
  Serial.print("[RelativeModule] onTag(): ");
  Serial.println(uid);
  static bool toggle = false;
  toggle = !toggle;
  if (!toggle && _leds) {
    for (uint16_t i = 0; i < _numPixels; ++i) _leds[i] = CRGB::Black;
    FastLED.show();
  }
}

void RelativeModule::onMQTT(const char* topic, const char* payload) {
  if (strcmp(topic, _maxTopic.c_str()) == 0) {
    long m = atol(payload);
    maxCount = m < 0 ? 0 : m;
    if (DEBUG_RAW) Serial.printf("[RelativeModule] set maxCount = %ld\n", maxCount);
  } else {
    if (DEBUG_RAW) {
      Serial.print("[RelativeModule] MQTT ");
      Serial.print(topic);
      Serial.print(" -> ");
      Serial.println(payload);
    }
  }
}