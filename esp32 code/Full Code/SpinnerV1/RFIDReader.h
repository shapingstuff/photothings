#pragma once
#include <SPI.h>
#include <MFRC522.h>

class RFIDReader {
public:
  // Constructor: ssPin = RC522 SS, rstPin = RC522 RST.
  // Optionally pass sckPin, misoPin, mosiPin to use custom SPI pins.
  RFIDReader(uint8_t ssPin, uint8_t rstPin,
             int sckPin = -1, int misoPin = -1, int mosiPin = -1)
    : mfrc522(ssPin, rstPin), ss(ssPin), rst(rstPin),
      sck(sckPin), miso(misoPin), mosi(mosiPin) {}

  void begin() {
    // If the user provided custom SPI pins, initialise SPI with them.
    // ESP32 SPI.begin(SCK, MISO, MOSI)
    if (sck >= 0 && miso >= 0 && mosi >= 0) {
      SPI.begin(sck, miso, mosi);
      Serial.print("SPI.begin(custom pins) SCK=");
      Serial.print(sck);
      Serial.print(" MISO=");
      Serial.print(miso);
      Serial.print(" MOSI=");
      Serial.println(mosi);
    } else {
      // default hardware SPI pins
      SPI.begin();
      Serial.println("SPI.begin(default pins)");
    }

    // tiny delay to let SPI lines settle
    delay(10);

    // Init the MFRC522 (this uses the SPI bus we've just configured)
    mfrc522.PCD_Init();
    Serial.print("MFRC522 initialised on SS=");
    Serial.print(ss);
    Serial.print(" RST=");
    Serial.println(rst);
  }

  // if a card is present and read, returns true and uidStr contains uppercase hex w/o spaces.
  bool poll(String &uidStr) {
    if (!mfrc522.PICC_IsNewCardPresent()) return false;
    if (!mfrc522.PICC_ReadCardSerial()) return false;
    uidStr = uidToString(mfrc522.uid.uidByte, mfrc522.uid.size);
    mfrc522.PICC_HaltA(); // stop further reading until new card
    lastSeen = millis();
    return true;
  }

  // returns milliseconds since last seen card (or large value if never seen)
  unsigned long msSinceLastSeen() {
    if (lastSeen == 0) return ULONG_MAX;
    return millis() - lastSeen;
  }

  static String uidToString(byte *uid, byte len) {
    String s = "";
    for (byte i = 0; i < len; i++) {
      if (uid[i] < 0x10) s += "0";
      s += String(uid[i], HEX);
    }
    s.toUpperCase();
    return s;
  }

private:
  MFRC522 mfrc522;
  uint8_t ss;
  uint8_t rst;
  int sck, miso, mosi;
  unsigned long lastSeen = 0;
};