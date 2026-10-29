// HelloGrowNet - try GrowNet with NO extra hardware.
// Just upload to any board (Nano, Uno, ESP32, RP2040...) and open the
// Serial Monitor at 115200 baud.
//
// It pretends to read two sensors (vibration and temperature) of a machine,
// teaches the AI three situations, then tests it on new readings, including
// one situation it was never taught -> it answers "I don't know".

#include <GrowNet.h>

const char* NAMES[] = {"Normal", "Imbalance", "Overheating"};

float pool[GROWNET_POOL_BYTES / sizeof(float)];   // the AI's memory, sized for this board
GrowNet net;

float noise(float a) { return random(-1000, 1001) / 1000.0f * a; }

// Fake sensor readings, in raw units (ADC counts and degrees C)
void fakeSensors(int situation, float* x) {
  switch (situation) {
    case 0:  x[0] = 200 + noise(30); x[1] = 35 + noise(2); break;  // Normal
    case 1:  x[0] = 700 + noise(40); x[1] = 38 + noise(2); break;  // Imbalance
    case 2:  x[0] = 250 + noise(30); x[1] = 70 + noise(3); break;  // Overheating
    default: x[0] = 1000 + noise(10); x[1] = 95 + noise(2); break; // never taught!
  }
}

void show(const float* x, int truth) {
  unsigned long t = micros();
  uint8_t r = net.predict(x);
  t = micros() - t;
  Serial.print(F("  vib=")); Serial.print(x[0], 0);
  Serial.print(F(" temp=")); Serial.print(x[1], 1);
  Serial.print(F("  ->  "));
  if (r == GrowNet::UNKNOWN) Serial.print(F("I don't know"));
  else { Serial.print(NAMES[r]); Serial.print(F(" (")); Serial.print(net.confidence() * 100, 0); Serial.print(F("% sure)")); }
  Serial.print(F("   [truth: "));
  Serial.print(truth < 3 ? NAMES[truth] : "never taught");
  Serial.print(F(", ")); Serial.print(t); Serial.println(F(" us]"));
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  delay(500);
  randomSeed(42);

  if (!net.begin(2, 3, pool, sizeof(pool))) { Serial.println(F("Not enough memory")); while (true) {} }
  Serial.println(F("\n=== GrowNet: AI that learns on this chip ==="));
  Serial.print(F("This board can grow up to ")); Serial.print(net.maxParams());
  Serial.print(F(" parameters (")); Serial.print(net.maxNeurons()); Serial.println(F(" neurons)."));

  Serial.println(F("\nTeaching 5 examples of each situation..."));
  unsigned long t = millis();
  for (int r = 0; r < 5; r++)
    for (int s = 0; s < 3; s++) { float x[2]; fakeSensors(s, x); net.teach(x, s); }
  t = millis() - t;
  Serial.print(F("Learned in ")); Serial.print(t); Serial.print(F(" ms. Neurons: "));
  Serial.print(net.neurons()); Serial.print(F(", accuracy on examples: "));
  Serial.print(net.accuracy() * 100, 0); Serial.println(F("%"));

  Serial.println(F("\nTesting on NEW readings it has never seen:"));
  for (int s = 0; s < 3; s++) { float x[2]; fakeSensors(s, x); show(x, s); }
  Serial.println(F("\nA situation it was never taught:"));
  float x[2]; fakeSensors(3, x); show(x, 3);
  Serial.println(F("\nDone. Next: try the SerialTeach example with your own sensors."));
}

void loop() {}
