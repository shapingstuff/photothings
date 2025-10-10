// main.ino
#include <Arduino.h>

#include <SPI.h>
#include <MFRC522.h>

#include "shared.h"
#include "module_friend.h"
#include "module_family.h"
#include "module_date.h"
#include "module_days.h"
#include "module_distance.h"
#include "module_timeline.h"
#include "module_cousins.h"
#include "module_afamily.h"
#include "module_themes.h"
#include "module_album.h"

// ---- shared configuration values (definitions) ----
const uint8_t SDA_PIN = 5;
const uint8_t SCL_PIN = 6;
const uint8_t PIXEL_PIN = 2;
const uint16_t NUM_PIXELS = 1;

const uint16_t SCREEN_W = 128;
const uint16_t SCREEN_H = 64;
const uint8_t OLED_RESET = 3;

// WiFi / MQTT config - change to your network
const char* WIFI_SSID = "DylanWiFi";
const char* WIFI_PWD = "82339494";
const char* MQTT_SERVER = "192.168.68.80";
const uint16_t MQTT_PORT = 1883;

// RFID pins (from your working code)
#define RST_PIN 1  // Use your selected RST pin
#define SS_PIN 44  // Chip select / SDA pin

// Example UID mapping (replace after first serial run)
// Note: UIDs must match the format returned by tryReadRfidUid() (uppercase hex concatenated bytes)
const char* FRIEND_UID = "F16B8949";          // replace with real scanned UID
const char* FAMILY_UID = "91798949";          // example placeholder — replace with your family tag UID
const char* DATE_UID = "A1778949";            // example placeholder — replace with your family tag UID
const char* DAYS_UID = "C19D8949";            // <-- replace with the tag you want for module_days
const char* DISTANCE_UID = "1D0B1CBB8A0000";  // <-- REPLACE with your scanned tag UID (uppercase hex)
const char* TIMELINE_UID = "";                // <-- REPLACE with your scanned tag UID (uppercase hex)
const char* COUSINS_UID = "D19B8949";         // <-- REPLACE with your scanned tag UID (uppercase hex)
const char* AFAMILY_UID = "E1998949";         // replace with the real tag UID
const char* THEMES_UID = "81AD8949";          // replace with scanned tag
const char* ALBUM_UID_TAG_1 = "C1A18949"; // baby
const char* ALBUM_UID_TAG_2 = "41AF8949"; // toddler

// ---- shared object definitions (actual instances) ----
AS5600 as5600;  // uses Wire
CRGB* leds = nullptr;
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, OLED_RESET);

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// MFRC522 instance
MFRC522 mfrc522(SS_PIN, RST_PIN);  // SS, RST

// Module function typedef
typedef void (*module_fn_t)();

// ModuleEntry structure - map UID -> module functions
struct ModuleEntry {
  const char* uid;   // UID string to match (uppercase hex)
  const char* name;  // friendly name (for logging)
  module_fn_t setup;
  module_fn_t activate;
  module_fn_t deactivate;
  module_fn_t loop;
};

// Forward declarations (module headers supply these; extern not strictly necessary but explicit)
extern void module_friend_setup();
extern void module_friend_activate();
extern void module_friend_deactivate();
extern void module_friend_loop();

extern void module_family_setup();
extern void module_family_activate();
extern void module_family_deactivate();
extern void module_family_loop();

extern void module_date_setup();
extern void module_date_activate();
extern void module_date_deactivate();
extern void module_date_loop();

extern void module_days_setup();
extern void module_days_activate();
extern void module_days_deactivate();
extern void module_days_loop();

extern void module_distance_setup();
extern void module_distance_activate();
extern void module_distance_deactivate();
extern void module_distance_loop();

extern void module_cousins_setup();
extern void module_cousins_activate();
extern void module_cousins_deactivate();
extern void module_cousins_loop();

extern void module_afamily_setup();
extern void module_afamily_activate();
extern void module_afamily_deactivate();
extern void module_afamily_loop();

extern void module_themes_setup();
extern void module_themes_activate();
extern void module_themes_deactivate();
extern void module_themes_loop();

extern void module_album_setup();
extern void module_album_activate();
extern void module_album_deactivate();
extern void module_album_loop();

// The module table: add more entries here when you add modules.
// If you want a module to be present but not mapped to a tag, set uid = nullptr.
// modules[] table: add the days entry
ModuleEntry modules[] = {
  { FRIEND_UID, "friend", module_friend_setup, module_friend_activate, module_friend_deactivate, module_friend_loop },
  { FAMILY_UID, "family", module_family_setup, module_family_activate, module_family_deactivate, module_family_loop },
  { DATE_UID, "date", module_date_setup, module_date_activate, module_date_deactivate, module_date_loop },
  { DAYS_UID, "days", module_days_setup, module_days_activate, module_days_deactivate, module_days_loop },
  { DISTANCE_UID, "distance", module_distance_setup, module_distance_activate, module_distance_deactivate, module_distance_loop },
  { TIMELINE_UID, "timeline", module_timeline_setup, module_timeline_activate, module_timeline_deactivate, module_timeline_loop },
  { COUSINS_UID, "cousins", module_cousins_setup, module_cousins_activate, module_cousins_deactivate, module_cousins_loop },
  { AFAMILY_UID, "afamily", module_afamily_setup, module_afamily_activate, module_afamily_deactivate, module_afamily_loop },
  { THEMES_UID, "themes", module_themes_setup, module_themes_activate, module_themes_deactivate, module_themes_loop },
  { ALBUM_UID_TAG_1, "album", module_album_setup, module_album_activate, module_album_deactivate, module_album_loop },
  { ALBUM_UID_TAG_2, "album", module_album_setup, module_album_activate, module_album_deactivate, module_album_loop },
  { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr }  // sentinel
};

// ---- MQTT reconnect helper ----
void reconnectMQTT() {
  if (mqttClient.connected()) return;
  while (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "esp32-";
    clientId += String((uint32_t)ESP.getEfuseMac(), HEX);
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 2s");
      delay(2000);
    }
  }
}

// ---- RFID & module selection state ----
int activeModuleIndex = -1;    // index into modules[], -1 = none
String currentActiveUid = "";  // active UID string (empty = none)

unsigned long lastTagProcessedMs = 0;
const unsigned long TAG_DEBOUNCE_MS = 600;  // ignore re-reads within this window

// ---- helpers: lookup, activate, deactivate ----
int findModuleIndexByUid(const String& uid) {
  for (int i = 0; modules[i].uid != nullptr; ++i) {
    if (uid == String(modules[i].uid)) return i;
  }
  return -1;
}

void activateModuleByIndex(int idx, const String& uid) {
  if (idx < 0) return;
  // deactivate previous
  if (activeModuleIndex >= 0 && activeModuleIndex != idx) {
    if (modules[activeModuleIndex].deactivate) modules[activeModuleIndex].deactivate();
    Serial.print("Deactivated module: ");
    Serial.println(modules[activeModuleIndex].name);
  }
  // activate new
  if (modules[idx].activate) modules[idx].activate();
  Serial.print("Activated module: ");
  Serial.println(modules[idx].name);
  activeModuleIndex = idx;
  currentActiveUid = uid;
}

void deactivateActiveModule() {
  if (activeModuleIndex >= 0) {
    if (modules[activeModuleIndex].deactivate) modules[activeModuleIndex].deactivate();
    Serial.print("Deactivated module: ");
    Serial.println(modules[activeModuleIndex].name);
    activeModuleIndex = -1;
    currentActiveUid = "";
  }
}

// ---- setup() ----
void setup() {
  Serial.begin(115200);
  while (!Serial)
    ;
  Serial.println("Booting — central init (with MFRC522)");

  // create leds buffer
  leds = (CRGB*)malloc(sizeof(CRGB) * NUM_PIXELS);
  if (!leds) {
    Serial.println("Failed to allocate leds array");
    while (1) delay(1000);
  }

  // --- I2C + sensor init (centralised) ---
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000UL);
  if (!as5600.begin()) {
    Serial.println("AS5600 not found! (check wiring)");
  } else {
    Serial.println("AS5600 OK");
  }

  // --- NeoPixel init ---
  FastLED.addLeds<WS2812B, PIXEL_PIN, GRB>(leds, NUM_PIXELS);
  FastLED.setBrightness(200);
  leds[0] = CRGB::Black;
  FastLED.show();

  // --- OLED init ---
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
    Serial.println("SSD1306 init failed!");
  } else {
    display.clearDisplay();
    display.display();
  }

  // --- Wi-Fi & MQTT init ---
  Serial.print("Connecting to WiFi ");
  Serial.print(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PWD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" connected");
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);

  // --- MFRC522 init (your working config) ---
  SPI.begin(7, 9, 8);  // SCK, MISO, MOSI — keep your proven wiring
  mfrc522.PCD_Init();
  byte version = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
  Serial.print("RC522 Version: 0x");
  Serial.println(version, HEX);
  if (version == 0x00 || version == 0xFF) {
    Serial.println("RC522 not detected. Check wiring.");
  } else {
    Serial.println("MFRC522 ready. Scan a tag to activate a module.");
  }

  // --- initialise all modules (optional: modules can defer heavy init to activate) ---
  for (int i = 0; modules[i].uid != nullptr; ++i) {
    if (modules[i].setup) modules[i].setup();
    Serial.print("Module initialised: ");
    Serial.println(modules[i].name);
  }

  Serial.println("Setup complete");
}

// ---- read RFID helper (non-blocking, returns empty if no new tag) ----
String tryReadRfidUid() {
  if (!mfrc522.PICC_IsNewCardPresent()) return String("");
  if (!mfrc522.PICC_ReadCardSerial()) return String("");

  String uidStr = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) uidStr += "0";
    uidStr += String(mfrc522.uid.uidByte[i], HEX);
  }
  uidStr.toUpperCase();

  mfrc522.PICC_HaltA();
  return uidStr;
}

// ---- main loop ----
void loop() {
  // keep MQTT alive
  if (!mqttClient.connected()) reconnectMQTT();
  mqttClient.loop();

  // Poll RFID (fast non-blocking)
  String uid = tryReadRfidUid();
  if (uid.length()) {
    unsigned long now = millis();
    if (now - lastTagProcessedMs < TAG_DEBOUNCE_MS) {
      Serial.println("RFID read ignored (debounce)");
    } else {
      lastTagProcessedMs = now;
      Serial.print("Card UID: ");
      Serial.println(uid);

      // If same as currently active UID, ignore (per your request)
      if (currentActiveUid.length() && uid == currentActiveUid) {
        Serial.println("Same tag as current active module — no change.");
      } else {
        int idx = findModuleIndexByUid(uid);
        if (idx >= 0) {
          // found a module for this tag
          activateModuleByIndex(idx, uid);
        } else {
          // unknown tag — deactivate current module
          Serial.println("Unknown UID — no module mapped.");
          deactivateActiveModule();
        }
      }
    }
  }

  // run active module loop if any
  if (activeModuleIndex >= 0) {
    if (modules[activeModuleIndex].loop) modules[activeModuleIndex].loop();
  }

  delay(1);
}