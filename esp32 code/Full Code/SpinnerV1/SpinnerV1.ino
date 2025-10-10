#include <WiFi.h>           // needed only if you plan to use MQTT
#include "RFIDReader.h"
#include "ModuleBase.h"
#include "TestModule.h"
#include "MQTTManager.h"
#include "RelativeModule.h" // <- MUST be included before instantiating relativeModule
#include "FriendsModule.h"

// ===== CONFIG =====
// Change these for your board and network:
const uint8_t RC522_SS_PIN  = 44;   // SS pin
const uint8_t RC522_RST_PIN = 1;    // RST pin

const char* WIFI_SSID = "DylanWiFi";
const char* WIFI_PASS = "82339494";
const char* MQTT_SERVER = "192.168.68.90"; // optional

// I2C pins (your working pins)
#define SDA_PIN 5
#define SCL_PIN 6

// NeoPixel / display pins / counts (adjust if your wiring differs)
const uint8_t PIXEL_PIN = 13;
const uint16_t NUM_PIXELS = 1;
const uint8_t OLED_RST = 3;

// (left for reference if you want a different behaviour later)
const unsigned long TAG_CLEAR_MS = 1200UL;

// custom SPI pins: SCK=7, MISO=9, MOSI=8
RFIDReader rfid(RC522_SS_PIN, RC522_RST_PIN, 7, 9, 8);

// static module instances (avoid heap churn)
// Test module (your existing simple module)
TestModule testModule;

// RelativeModule: pass (sda, scl, pixelPin, numPixels, oledReset)
RelativeModule relativeModule(SDA_PIN, SCL_PIN, PIXEL_PIN, NUM_PIXELS, OLED_RST);
FriendsModule friendsModule(SDA_PIN, SCL_PIN, PIXEL_PIN, NUM_PIXELS, OLED_RST, "friends/pub", "friends/cmd");

// mapping table (uid string -> module pointer)
// Keep UIDs uppercase, no spaces. e.g. "04A2B3C4"
struct TagMapEntry {
  const char* uid;
  ModuleBase* module;
};

TagMapEntry tagMap[] = {
  // Put a valid UID for the testModule if needed, e.g. {"04A2B3C4", &testModule},
  // The UID below should be replaced with the tag that selects the relative module
  {"1D2D26BB8A0000", &relativeModule},
  {"1D6F1EBB8A0000", &friendsModule},
  {nullptr, nullptr} // terminator
};

ModuleBase* activeModule = nullptr;
// lastSeenUID used only to debounce repeated reads of same tag while it's passing the antenna
String lastSeenUID = "";

WiFiClient wifiClient;

void mqttForwarder(const char* topic, const char* payload) {
  if (activeModule) activeModule->onMQTT(topic, payload);
}

ModuleBase* findModuleForUID(const String &uid) {
  for (int i = 0; tagMap[i].uid != nullptr; ++i) {
    if (uid.equalsIgnoreCase(String(tagMap[i].uid))) return tagMap[i].module;
  }
  return nullptr;
}

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("Starting RFID modules example (persistent active module)");

  // initialise RFID (this will call SPI.begin(customPins) inside RFIDReader.begin())
  rfid.begin();
  Serial.println("RFID initialised");

  // OPTIONAL: bring up WiFi + MQTT if you want to use MQTT in modules
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting WiFi ");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
    Serial.print(".");
    delay(300);
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected");
    MQTTManager::instance().begin(wifiClient, MQTT_SERVER, 1883);
    MQTTManager::instance().setForwarder(mqttForwarder);
  } else {
    Serial.println("\nWiFi NOT connected (continuing without MQTT)");
  }

  // no active module by default (set to a default if you want)
  activeModule = nullptr;
  lastSeenUID = "";
}

void loop() {
  // OPTIONAL: MQTT loop
  MQTTManager::instance().loop();

  // Poll RFID - this returns each time a card is seen.
  String uid;
  bool sawCard = rfid.poll(uid);

  if (sawCard) {
    // If this read is the same UID we last saw, ignore (prevents repeated re-begins)
    if (lastSeenUID.equalsIgnoreCase(uid)) {
      // same tag being read repeatedly while passing the antenna — ignore
      // If you want the module to react to repeated scans, call activeModule->onTag(uid) here
    } else {
      Serial.print("New RFID seen: ");
      Serial.println(uid);

      ModuleBase* target = findModuleForUID(uid);
      if (target) {
        // Only switch modules if the scanned tag maps to a different module
        if (target != activeModule) {
          Serial.println("Found registered module for UID -> switching");
          if (activeModule) {
            activeModule->stop();
          }
          activeModule = target;
          activeModule->begin();
        } else {
          Serial.println("Scanned tag belongs to currently active module -> no switch");
        }
      } else {
        // Unregistered tag: ignore (do not change active module).
        Serial.println("Unregistered tag seen -> ignoring for module switching");
      }

      // record the UID we last saw to debounce repeated reads while the tag is passing
      lastSeenUID = uid;
    }
  } else {
    // No card present: DO NOT clear activeModule. We intentionally keep the active module running
    // until another registered tag appears.
    //
    // We DO NOT clear lastSeenUID here either (so a transient gap does not immediately allow
    // re-trigger). If you'd like the same tag to be able to re-trigger after it leaves and
    // returns, we can add a short clearing timeout here — tell me and I'll add it.
  }

  // call active module loop if present
  if (activeModule) activeModule->loop();

  // tiny delay to keep CPU happy, still non-blocking for most uses
  delay(10);
}