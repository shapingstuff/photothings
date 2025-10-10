#pragma once
#include <Arduino.h>

class ModuleBase {
public:
  virtual ~ModuleBase() {}
  // called when module becomes active (after RFID change)
  virtual void begin() = 0;
  // called when module is deactivated (another tag selected)
  virtual void stop() = 0;
  // called every loop
  virtual void loop() = 0;
  // optional: react to a tag scan of the same module (not used here)
  virtual void onTag(const String &uid) {}
  // optional: forwarded MQTT messages
  virtual void onMQTT(const char* topic, const char* payload) {}
};