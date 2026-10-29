// GrowNetEEPROM - save and load a GrowNet brain in the board's EEPROM.
// Works on AVR (Nano, Uno, Mega...), ESP32, ESP8266 and RP2040 (Earle Philhower core).
// On boards without EEPROM support the functions do nothing and return false.
#ifndef GROWNET_EEPROM_H
#define GROWNET_EEPROM_H

#include <Arduino.h>
#include "GrowNet.h"

#if defined(__AVR__) || defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266) || \
    (defined(ARDUINO_ARCH_RP2040) && !defined(ARDUINO_ARCH_MBED))
  #define GROWNET_HAS_EEPROM 1
  #include <EEPROM.h>
#endif

namespace GrowNetEEPROM {
#ifdef GROWNET_HAS_EEPROM
  #if defined(__AVR__)
    static const uint32_t SIZE = (uint32_t)E2END + 1;
  #else
    static const uint32_t SIZE = 4096;
  #endif

  inline void writeByte(uint32_t a, uint8_t v) {
  #if defined(__AVR__)
    EEPROM.update(a, v);          // only writes bytes that changed
  #else
    EEPROM.write(a, v);
  #endif
  }
  inline uint8_t readByte(uint32_t a) { return EEPROM.read(a); }

  inline void commit() {
  #if !defined(__AVR__)
    EEPROM.commit();
  #endif
  }

  // Call once in setup() before load()/save()
  inline void begin() {
  #if !defined(__AVR__)
    EEPROM.begin(SIZE);
  #endif
  }

  // false if the brain is bigger than the EEPROM
  inline bool save(const GrowNet& net) {
    if (net.saveSize() > SIZE) return false;
    net.save(writeByte);
    commit();
    return true;
  }

  // false if there is no saved brain, or it has a different shape
  inline bool load(GrowNet& net) { return net.load(readByte); }

  // Makes the saved brain invalid (next load() fails)
  inline void erase() { writeByte(0, 0xFF); commit(); }

  inline bool available() { return true; }
#else
  inline void begin() {}
  inline bool save(const GrowNet&) { return false; }
  inline bool load(GrowNet&) { return false; }
  inline void erase() {}
  inline bool available() { return false; }
#endif
}

#endif
