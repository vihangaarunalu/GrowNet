// SerialTeach - teach GrowNet with ANY analog sensors, from the Serial Monitor.
//
// Wiring: any analog sensors on A0 and A1 (light, temperature, potentiometer,
//         soil moisture, gas, current...). GrowNet learns their ranges itself.
//
// Serial Monitor at 115200 baud, then type:
//   0, 1, 2  -> "what is happening right now is situation 0 / 1 / 2"
//   s        -> status        r -> forget everything
//
// The brain is saved to EEPROM, so it remembers after power-off.

#include <GrowNet.h>
#include <GrowNetEEPROM.h>

const uint8_t N_IN = 2;                         // number of inputs
const uint8_t PINS[N_IN] = {A0, A1};            // where they are connected
const char* NAMES[] = {"Situation A", "Situation B", "Situation C"};   // rename freely
const uint8_t N_OUT = sizeof(NAMES) / sizeof(NAMES[0]);

float pool[GROWNET_POOL_BYTES / sizeof(float)];
GrowNet net;

void readInputs(float* x) {
  for (uint8_t i = 0; i < N_IN; i++) x[i] = analogRead(PINS[i]);
}

void printStatus() {
  Serial.print(F("Neurons ")); Serial.print(net.neurons()); Serial.print('/'); Serial.print(net.maxNeurons());
  Serial.print(F(" | params ")); Serial.print(net.params()); Serial.print('/'); Serial.print(net.maxParams());
  Serial.print(F(" | examples ")); Serial.print(net.examples());
  Serial.print(F(" | accuracy ")); Serial.print(net.accuracy() * 100, 0); Serial.println('%');
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  GrowNetEEPROM::begin();
  if (!net.begin(N_IN, N_OUT, pool, sizeof(pool), micros())) { Serial.println(F("Not enough memory")); while (true) {} }
  Serial.println(GrowNetEEPROM::load(net) ? F("Brain loaded from memory.") : F("New brain."));
  printStatus();
  Serial.println(F("Type a number to teach the current situation, s = status, r = reset."));
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c >= '0' && c < '0' + N_OUT) {
      float x[N_IN]; readInputs(x);
      uint8_t label = c - '0';
      Serial.print(F("Learning: ")); Serial.println(NAMES[label]);
      net.teach(x, label);
      if (!GrowNetEEPROM::save(net)) Serial.println(F("(not saved: no EEPROM or brain too big)"));
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
    float x[N_IN]; readInputs(x);
    uint8_t r = net.predict(x);
    for (uint8_t i = 0; i < N_IN; i++) { Serial.print(x[i], 0); Serial.print(' '); }
    Serial.print(F("-> "));
    if (net.examples() == 0) Serial.println(F("teach me first"));
    else if (r == GrowNet::UNKNOWN) Serial.println(F("I don't know this situation"));
    else { Serial.print(NAMES[r]); Serial.print(F(" (")); Serial.print(net.confidence() * 100, 0); Serial.println(F("%)")); }
  }
}
