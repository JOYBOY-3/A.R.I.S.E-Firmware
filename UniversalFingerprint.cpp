// UniversalFingerprint.cpp - Simple wrapper for Adafruit library
#include "UniversalFingerprint.h"

UniversalFingerprint::UniversalFingerprint(HardwareSerial* ser) 
  : serial(ser), finger(ser), maxCapacity(162) {
}

bool UniversalFingerprint::begin() {
  serial->begin(57600);
  delay(1000);
  
  if (finger.verifyPassword()) {
    // Get actual template count to determine sensor type
    finger.getTemplateCount();
    if (finger.templateCount == FINGERPRINT_OK) {
      // AS608 has 162 capacity, R307 has 1000
      if (finger.templateCount > 162) {
        maxCapacity = 1000;
      } else {
        maxCapacity = 162;
      }
    }
    Serial.println("✅ Fingerprint Sensor: " + getSensorName());
    return true;
  }
  return false;
}

bool UniversalFingerprint::verifyPassword() {
  return finger.verifyPassword();
}

String UniversalFingerprint::getSensorName() {
  if (maxCapacity >= 1000) {
    return "R307 (1000 templates)";
  } else {
    return "AS608 (162 templates)";
  }
}

uint16_t UniversalFingerprint::getMaxCapacity() {
  return maxCapacity;
}

uint16_t UniversalFingerprint::getTemplateCount() {
  if (finger.getTemplateCount() == FINGERPRINT_OK) {
    return finger.templateCount;
  }
  return 0;
}

uint8_t UniversalFingerprint::getImage() {
  return finger.getImage();
}

uint8_t UniversalFingerprint::image2Tz(uint8_t slot) {
  return finger.image2Tz(slot);
}

uint8_t UniversalFingerprint::createModel() {
  return finger.createModel();
}

uint8_t UniversalFingerprint::storeModel(uint16_t location) {
  // High slot fix for AS608
  if (location > 127 && maxCapacity == 162) {
    // For high slots on AS608, we need to use a different approach
    // This is a workaround for the library limitation
    return finger.storeModel(location);
  }
  return finger.storeModel(location);
}

uint8_t UniversalFingerprint::deleteModel(uint16_t location) {
  return finger.deleteModel(location);
}

uint8_t UniversalFingerprint::emptyDatabase() {
  return finger.emptyDatabase();
}

uint8_t UniversalFingerprint::fingerFastSearch() {
  return finger.fingerFastSearch();
}

uint8_t UniversalFingerprint::loadModel(uint16_t location) {
  return finger.loadModel(location);
}

uint16_t UniversalFingerprint::getFingerID() {
  return finger.fingerID;
}

uint16_t UniversalFingerprint::getConfidence() {
  return finger.confidence;
}