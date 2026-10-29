// SerialTeach - teach GrowNet with ANY analog sensors, from the Serial Monitor,
// and add a sensor later without losing what it learned.
//
// Wiring: any analog sensors on A0 and A1 (light, temperature, potentiometer,
//         soil moisture, gas, current...). Optionally a third one on A2 later.
//         GrowNet learns their ranges itself.
//
// Serial Monitor at 115200 baud, then type:
//   0, 1, 2  -> "what is happening right now is situation 0 / 1 / 2"
//   a        -> add the next sensor (A2) - everything learned is kept
//   s        -> status        r -> forget everything
//
// The brain is saved to EEPROM, so it remembers after power-off.

#include <GrowNet.h>
#include <GrowNetEEPROM.h>

const uint8_t START_INPUTS = 2;                 // sensors used from the start
#if defined(ARDUINO_ARCH_ESP32)
const uint8_t PINS[] = {36, 39, 34};             // ESP32 analog pins (change to yours)
#else
const uint8_t PINS[] = {A0, A1, A2};             // all sensors that may be used
#endif
const uint8_t MAX_INPUTS = sizeof(PINS) / sizeof(PINS[0]);
const char* NAMES[] = {"Situation A", "Situation B", "Situation C"};   // rename freely
const uint8_t N_OUT = sizeof(NAMES) / sizeof(NAMES[0]);

float pool[GROWNET_POOL_BYTES / sizeof(float)];
GrowNet net;

void readInputs(float* x) {
  for (uint8_t i = 0; i < net.inputs(); i++) x[i] = analogRead(PINS[i]);
}

void printStatus() {
  Serial.print(F("Sensors ")); Serial.print(net.inputs()); Serial.print('/'); Serial.print(net.maxInputs());
  Serial.print(F(" | neurons ")); Serial.print(net.neurons()); Serial.print('/'); Serial.print(net.maxNeurons());
  Serial.print(F(" | params ")); Serial.print(net.params());
  Serial.print(F(" | examples ")); Serial.print(net.examples());
  Serial.print(F(" | accuracy ")); Serial.print(net.accuracy() * 100, 0); Serial.println('%');
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  GrowNetEEPROM::begin();
  if (!net.begin(START_INPUTS, N_OUT, pool, sizeof(pool), micros(), MAX_INPUTS)) {
    Serial.println(F("Not enough memory")); while (true) {}
  }
  Serial.println(GrowNetEEPROM::load(net) ? F("Brain loaded from memory.") : F("New brain."));
  printStatus();
  Serial.println(F("Type 0/1/2 to teach, a = add sensor, s = status, r = reset."));
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c >= '0' && c < '0' + N_OUT) {
      float x[MAX_INPUTS]; readInputs(x);
      uint8_t label = c - '0';
      Serial.print(F("Learning: ")); Serial.println(NAMES[label]);
      net.teach(x, label);
      if (!GrowNetEEPROM::save(net)) Serial.println(F("(not saved: no EEPROM or brain too big)"));
      printStatus();
    } else if (c == 'a') {
      if (net.addInput()) {
        GrowNetEEPROM::save(net);
        Serial.println(F("Sensor added. Old knowledge kept - teach a few examples so it learns the new sensor."));
      } else Serial.println(F("No room for more sensors."));
      printStatus();
    } else if (c == 's') {
      printStatus();
    } else if (c == 'r') {
      net.reset(); GrowNetEEPROM::erase();
      Serial.println(F("Forgot everything."));
    }
  }

  static unsigned long last = 0;
  if (millis() - last >= 1000) {
    last = millis();
    float x[MAX_INPUTS]; readInputs(x);
    uint8_t r = net.predict(x);
    for (uint8_t i = 0; i < net.inputs(); i++) { Serial.print(x[i], 0); Serial.print(' '); }
    Serial.print(F("-> "));
    if (net.examples() == 0) Serial.println(F("teach me first"));
    else if (r == GrowNet::UNKNOWN) Serial.println(F("I don't know this situation"));
    else { Serial.print(NAMES[r]); Serial.print(F(" (")); Serial.print(net.confidence() * 100, 0); Serial.println(F("%)")); }
  }
}
