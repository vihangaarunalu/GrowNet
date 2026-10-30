// InsightAuto - an ordinary Arduino sketch plus ONE line: #include <GrowNetAuto.h>
//
// The sketch below is normal code: it reads a sensor and a button and switches
// an LED. Because of the include, GrowNet watches every read and write and
// talks to you on the Serial Monitor (115200 baud, "Newline" line ending):
//
//   [GrowNet] Ready. Watching your sketch. Type #train to learn how it normally behaves.
//   #train      -> it learns for about a minute, showing progress
//   #yes / #no  -> it offers the learned data as CSV, then the links it found
//   afterwards  -> it warns about unusual situations and unexpected outputs
//   #ok         -> "this situation is normal" (it learns it)
//   #csv #links #status #reset #help
//
// The trained model is saved and reused after a restart or a code change.
// Your own Serial input still reaches your sketch: GrowNet only takes lines starting with #.
//
// Wiring: any analog sensor (potentiometer, light, temperature...) on A0,
//         an optional button from pin 2 to GND. LED on pin 13 (built in on most boards).
// Turn the sensor up and down for a minute and watch the messages.

#include <GrowNetAuto.h>          // <- the only GrowNet line needed

const int SENSOR = A0;
const int BUTTON = 2;
const int LED = 13;

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON, INPUT_PULLUP);
  pinMode(LED, OUTPUT);
  GrowNetAuto::name(SENSOR, "sensor");          // optional: readable names
  GrowNetAuto::name(BUTTON, "button");
  GrowNetAuto::nameOutput(LED, "LED");
}

void loop() {
  int level = analogRead(SENSOR);
  int pressed = digitalRead(BUTTON) == LOW;

  if (level > 500 || pressed) digitalWrite(LED, HIGH);   // your own rule, unchanged
  else                        digitalWrite(LED, LOW);

  delay(50);
}
