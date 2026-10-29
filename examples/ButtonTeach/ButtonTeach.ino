// ButtonTeach - a teachable device with no computer needed.
//
// Wiring:
//   A0, A1  -> any two analog sensors
//   Pin 2   -> button "this is NORMAL"  (other leg to GND)
//   Pin 3   -> button "this is a PROBLEM" (other leg to GND)
//   Pin 5   -> green LED  (+ 220 ohm resistor to GND)  normal
//   Pin 6   -> red LED                                  problem
//   Pin 7   -> yellow LED                               "I don't know"
//
// Use: press NORMAL a few times while things are fine, press PROBLEM while
// the problem is happening. LEDs then show what the AI recognizes.
// Hold both buttons while powering on to forget everything.

#include <GrowNet.h>
#include <GrowNetEEPROM.h>

const uint8_t BTN_NORMAL = 2, BTN_PROBLEM = 3;
const uint8_t PIN_OK = 5, PIN_PROBLEM = 6, PIN_UNSURE = 7;      // green, red, yellow LEDs

float pool[GROWNET_POOL_BYTES / sizeof(float)];
GrowNet net;

void readInputs(float* x) { x[0] = analogRead(A0); x[1] = analogRead(A1); }

void leds(bool g, bool r, bool y) {
  digitalWrite(PIN_OK, g ? HIGH : LOW); digitalWrite(PIN_PROBLEM, r ? HIGH : LOW); digitalWrite(PIN_UNSURE, y ? HIGH : LOW);
}

void teach(uint8_t label, uint8_t pin) {
  leds(true, true, true);                 // all on = learning
  float x[2]; readInputs(x);
  net.teach(x, label);
  GrowNetEEPROM::save(net);
  delay(30);
  while (digitalRead(pin) == LOW) {}      // wait for release
  delay(30);
}

void setup() {
  pinMode(BTN_NORMAL, INPUT_PULLUP);
  pinMode(BTN_PROBLEM, INPUT_PULLUP);
  pinMode(PIN_OK, OUTPUT); pinMode(PIN_PROBLEM, OUTPUT); pinMode(PIN_UNSURE, OUTPUT);
  GrowNetEEPROM::begin();
  net.begin(2, 2, pool, sizeof(pool), analogRead(A0) + micros());

  bool wipe = digitalRead(BTN_NORMAL) == LOW && digitalRead(BTN_PROBLEM) == LOW;
  if (wipe) {
    GrowNetEEPROM::erase();
    for (int i = 0; i < 3; i++) { leds(true, true, true); delay(200); leds(false, false, false); delay(200); }
    while (digitalRead(BTN_NORMAL) == LOW || digitalRead(BTN_PROBLEM) == LOW) {}
  } else {
    GrowNetEEPROM::load(net);
  }
}

void loop() {
  if (digitalRead(BTN_NORMAL) == LOW)  teach(0, BTN_NORMAL);
  if (digitalRead(BTN_PROBLEM) == LOW) teach(1, BTN_PROBLEM);

  if (net.examples() == 0) {              // not taught yet: blink yellow
    leds(false, false, ((millis() / 500) % 2) == 1);
    return;
  }
  float x[2]; readInputs(x);
  uint8_t r = net.predict(x);
  if (r == GrowNet::UNKNOWN) leds(false, false, true);
  else if (r == 0)           leds(true, false, false);
  else                       leds(false, true, false);
  delay(50);
}
