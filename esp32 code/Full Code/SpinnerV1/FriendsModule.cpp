#include "FriendsModule.h"

// define colors & fonts (must match header)
const CRGB FriendsModule::FRIENDS_COLORS[6] = {
  CRGB::Red, CRGB::Green, CRGB::Blue, CRGB::Yellow, CRGB::Cyan, CRGB::Magenta
};
const GFXfont* FriendsModule::FRIENDS_FONTS[6] = {
  &Rabito_font34pt7b, &Rabito_font34pt7b, &Rabito_font34pt7b,
  &Rabito_font34pt7b, &Rabito_font28pt7b, &Rabito_font28pt7b
};

FriendsModule::FriendsModule(uint8_t sda, uint8_t scl, const char* pubTopic)
  : _sda(sda), _scl(scl), _pubTopic(pubTopic), _leds(nullptr), _display(nullptr), _lastIdx(-1), _active(false), _lastLoopMs(0)
{
}

FriendsModule::~FriendsModule() {
  if (_display) { delete _display; _display = nullptr; }
  if (_leds)    { delete[] _leds; _leds = nullptr; }
}

void FriendsModule::initI2C() {
  Serial.printf("[FriendsModule] Wire.begin(sda=%u,scl=%u)\n", _sda, _scl);
  Wire.end();
  delay(5);
  Wire.begin(_sda, _scl);
  Wire.setClock(100000UL);
  delay(5);
}

void FriendsModule::initLeds() {
  if (_leds) return;
  Serial.printf("[FriendsModule] Allocating LED buffer count=%u\n", FRIENDS_NUM_PIXELS);
  _leds = new CRGB[FRIENDS_NUM_PIXELS];
  FastLED.addLeds<FRIENDS_LED_TYPE, FRIENDS_PIXEL_PIN, FRIENDS_COLOR_ORDER>(_leds, FRIENDS_NUM_PIXELS);
  FastLED.setBrightness(200);
  FastLED.clear();
  FastLED.show();
}

void FriendsModule::deinitLeds() {
  if (_leds) {
    FastLED.clear();
    FastLED.show();
    delete[] _leds;
    _leds = nullptr;
    delay(5);
  }
}

void FriendsModule::initDisplay() {
  if (_display) return;
  Serial.println("[FriendsModule] Allocating OLED (0x3D)");
  _display = new Adafruit_SSD1306(128, 64, &Wire, FRIENDS_OLED_RST);
  if (!_display) {
    Serial.println("[FriendsModule] ERROR: display allocation failed");
    return;
  }
  if (!_display->begin(SSD1306_SWITCHCAPVCC, FRIENDS_OLED_ADDR)) {
    Serial.println("[FriendsModule] SSD1306 init failed!");
    // We'll continue but drawing will be no-op
  }
  _display->clearDisplay();
  _display->display();
}

void FriendsModule::begin() {
  if (_active) {
    Serial.println("[FriendsModule] begin() already active");
    return;
  }

  Serial.println("[FriendsModule] begin()");
  Serial.print("[FriendsModule] free heap before init: ");
  Serial.println(ESP.getFreeHeap());

  // init i2c + sensor
  initI2C();
  if (!_as5600.begin()) {
    Serial.println("AS5600 not found!");
    // Unlike original blocking sketch, don't hang forever — just warn and continue
  }

  // leds & display
  initLeds();
  initDisplay();

  Serial.print("[FriendsModule] free heap after init: ");
  Serial.println(ESP.getFreeHeap());

  _lastIdx = -1;
  _active = true;
  _lastLoopMs = millis();
}

void FriendsModule::stop() {
  if (!_active) {
    Serial.println("[FriendsModule] stop() called but not active");
    return;
  }
  Serial.println("[FriendsModule] stop() - clearing visuals");

  if (_leds) {
    _leds[0] = CRGB::Black;
    FastLED.show();
  }
  if (_display) {
    _display->clearDisplay();
    _display->display();
    delete _display;
    _display = nullptr;
  }

  deinitLeds();

  _active = false;
}

void FriendsModule::updateDisplay(int idx) {
  if (!_display) return;

  _display->clearDisplay();
  const char* name = FRIENDS_LIST[idx];
  const GFXfont* f = FRIENDS_FONTS[idx];

  _display->setFont(f);
  _display->setTextSize(1);
  _display->setTextColor(SSD1306_WHITE);

  int16_t x1, y1;
  uint16_t w, h;
  _display->getTextBounds(name, 0, 0, &x1, &y1, &w, &h);

  int16_t cx = (128 - w) / 2 - x1;
  int16_t cy = (64 - h) / 2 - y1;
  _display->setCursor(cx, cy);
  _display->print(name);
  _display->display();
}

void FriendsModule::publishFriend(int idx) {
  if (idx < 0 || idx >= _numFriends) return;
  char payload[64];
  snprintf(payload, sizeof(payload), "{\"name\":\"%s\"}", FRIENDS_LIST[idx]);
  MQTTManager::instance().publish(_pubTopic.c_str(), payload);
  Serial.print("MQTT ▶ ");
  Serial.println(payload);
}

void FriendsModule::loop() {
  if (!_active) return;

  unsigned long now = millis();
  if (now - _lastLoopMs < 20) return; // same 20ms delay as original
  _lastLoopMs = now;

  uint16_t raw = _as5600.readAngle();

  int32_t shifted = int32_t(raw) - RAW_OFFSET;
  if (shifted < 0) shifted += 4096;
  else if (shifted >= 4096) shifted -= 4096;

  const int SLICE_COUNT = _numFriends;
  uint8_t slice = (shifted * SLICE_COUNT) / 4096;
  uint8_t idx = (slice + SLICE_COUNT - 0) % SLICE_COUNT; // HOME_SLICE = 0

  if (DEBUG_RAW) {
    Serial.printf("raw=%u shifted=%ld slice=%u idx=%u\n", raw, shifted, slice, idx);
  }

  if ((int)idx != _lastIdx) {
    _lastIdx = idx;

    if (_leds) {
      _leds[0] = FRIENDS_COLORS[idx];
      FastLED.show();
    }

    updateDisplay(idx);
    publishFriend(idx);
  }
}

void FriendsModule::onTag(const String &uid) {
  Serial.print("[FriendsModule] onTag(): ");
  Serial.println(uid);
  if (_lastIdx >= 0) publishFriend(_lastIdx);
}

void FriendsModule::onMQTT(const char* topic, const char* payload) {
  // not used in original; leave for future extensibility
  Serial.print("[FriendsModule] onMQTT(): ");
  Serial.print(topic);
  Serial.print(" -> ");
  Serial.println(payload);
}