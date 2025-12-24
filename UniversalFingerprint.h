// UniversalFingerprint.h - Simple wrapper for Adafruit library
#ifndef UNIVERSAL_FINGERPRINT_H
#define UNIVERSAL_FINGERPRINT_H

#include <Adafruit_Fingerprint.h>

class UniversalFingerprint {
private:
  HardwareSerial* serial;
  Adafruit_Fingerprint finger;
  uint16_t maxCapacity;

public:
  UniversalFingerprint(HardwareSerial* ser);

  bool begin();
  bool verifyPassword();
  String getSensorName();
  uint16_t getMaxCapacity();
  uint16_t getTemplateCount();

  // Core functions
  uint8_t getImage();
  uint8_t image2Tz(uint8_t slot = 1);
  uint8_t createModel();
  uint8_t storeModel(uint16_t location);
  uint8_t deleteModel(uint16_t location);
  uint8_t emptyDatabase();
  uint8_t fingerFastSearch();
  uint8_t loadModel(uint16_t location);

  // Results
  uint16_t getFingerID();
  uint16_t getConfidence();
};

#endif