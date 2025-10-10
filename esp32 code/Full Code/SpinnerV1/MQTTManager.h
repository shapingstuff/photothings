#pragma once
#include <WiFi.h>
#include <PubSubClient.h>

class MQTTManager {
public:
  static MQTTManager& instance() {
    static MQTTManager inst;
    return inst;
  }

  // Call with the WiFiClient instance you're using in main (e.g. wifiClient)
  void begin(WiFiClient& client, const char* server, uint16_t port = 1883) {
    this->client = &client;

    // (re)create PubSubClient now that we have a real Client reference
    if (mqtt) { delete mqtt; mqtt = nullptr; }
    mqtt = new PubSubClient(*this->client);

    mqttServer = server;
    mqttPort = port;
    mqtt->setServer(mqttServer, mqttPort);
    mqtt->setCallback(mqttCallbackStatic);
  }

  void loop() {
    if (!mqtt) return;
    if (!mqtt->connected()) reconnect();
    mqtt->loop();
  }

  bool publish(const char* topic, const char* payload) {
    if (!mqtt) return false;
    return mqtt->publish(topic, payload);
  }

  bool subscribe(const char* topic) {
    if (!mqtt) return false;
    return mqtt->subscribe(topic);
  }

  void setForwarder(void (*fptr)(const char*, const char*)) {
    forwarder = fptr;
  }

private:
  PubSubClient* mqtt = nullptr;    // created in begin()
  WiFiClient* client = nullptr;
  const char* mqttServer = nullptr;
  uint16_t mqttPort = 1883;
  void (*forwarder)(const char*, const char*) = nullptr;

  MQTTManager() {}
  ~MQTTManager() { if (mqtt) delete mqtt; }

  void reconnect() {
    if (!mqtt || !mqttServer) return;
    while (!mqtt->connected()) {
      // client id — make unique if you have multiple devices
      mqtt->connect("xiao-s3-client");
      if (!mqtt->connected()) delay(500);
    }
  }

  static void mqttCallbackStatic(char* topic, byte* payload, unsigned int length) {
    static char buf[512];
    if (length >= sizeof(buf)) length = sizeof(buf) - 1;
    memcpy(buf, payload, length);
    buf[length] = 0;
    if (MQTTManager::instance().forwarder) {
      MQTTManager::instance().forwarder(topic, buf);
    }
  }
};