#pragma once
#include "ModuleBase.h"
#include "MQTTManager.h"

class TestModule : public ModuleBase {
public:
  TestModule(int ledPin = LED_BUILTIN) : pin(ledPin) {}

  void begin() override {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH); // indicate active
    Serial.println("[TestModule] begin");
    // Example: subscribe to a topic if you want
    // MQTTManager::instance().subscribe("example/topic");
  }

  void stop() override {
    digitalWrite(pin, LOW);
    Serial.println("[TestModule] stop");
  }

  void loop() override {
    // simple demonstration: nothing heavy here.
    // keep non-blocking for responsiveness.
  }

  void onMQTT(const char* topic, const char* payload) override {
    Serial.print("[TestModule] MQTT ");
    Serial.print(topic);
    Serial.print(" -> ");
    Serial.println(payload);
  }

private:
  int pin;
};