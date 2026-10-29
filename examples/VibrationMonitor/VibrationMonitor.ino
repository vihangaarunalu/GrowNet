// VibrationMonitor - learn a machine's vibration and spot faults.
//
// Hardware: MPU6050 accelerometer module (no extra library needed)
//   VCC -> 5V (or 3.3V)   GND -> GND   SDA -> A4 (Nano/Uno)   SCL -> A5
//   (ESP32: SDA 21, SCL 22. Other boards: their default I2C pins)
// Fix the sensor firmly to the machine (a fan works great for testing).
//
// Serial Monitor at 115200 baud:
//   0 -> machine is running NORMALLY     1 -> a FAULT is happening now
//   s -> status    r -> forget everything
// Try it: teach normal, then tape a coin to one fan blade and teach fault.

#include <Wire.h>
#include <GrowNet.h>
#include <GrowNetEEPROM.h>

const uint8_t MPU_ADDR = 0x68;
const char* NAMES[] = {"Normal", "Fault"};
const uint8_t N_IN = 3, N_OUT = 2;
const int WINDOW = 64;                      // samples per measurement

float pool[GROWNET_POOL_BYTES / sizeof(float)];
GrowNet net;

void mpuBegin() {
  Wire.begin();
  Wire.beginTransmission(MPU_ADDR); Wire.write((uint8_t)0x6B); Wire.write((uint8_t)0); Wire.endTransmission();   // wake up
  Wire.beginTransmission(MPU_ADDR); Wire.write((uint8_t)0x1C); Wire.write((uint8_t)0); Wire.endTransmission();   // +-2 g
}

bool mpuRead(int16_t* a) {
  Wire.beginTransmission(MPU_ADDR); Wire.write((uint8_t)0x3B);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)MPU_ADDR, (int)6) != 6) return false;
  for (int i = 0; i < 3; i++) { int hi = Wire.read(); int lo = Wire.read(); a[i] = (int16_t)((hi << 8) | lo); }
  return true;
}

// Features = vibration strength (RMS around the average) on X, Y and Z
bool measure(float* f) {
  float sum[3] = {0, 0, 0}, sq[3] = {0, 0, 0};
  int16_t a[3];
  for (int n = 0; n < WINDOW; n++) {
    if (!mpuRead(a)) return false;
    for (int i = 0; i < 3; i++) { sum[i] += a[i]; sq[i] += (float)a[i] * a[i]; }
    delayMicroseconds(1000);
  }
  for (int i = 0; i < 3; i++) {
    float m = sum[i] / WINDOW, v = sq[i] / WINDOW - m * m;
    f[i] = v > 0 ? sqrt(v) : 0;
  }
  return true;
}

void printStatus() {
  Serial.print(F("Neurons ")); Serial.print(net.neurons()); Serial.print('/'); Serial.print(net.maxNeurons());
  Serial.print(F(" | examples ")); Serial.print(net.examples());
  Serial.print(F(" | accuracy ")); Serial.print(net.accuracy() * 100, 0); Serial.println('%');
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  mpuBegin();
  GrowNetEEPROM::begin();
  net.begin(N_IN, N_OUT, pool, sizeof(pool), micros());
  Serial.println(GrowNetEEPROM::load(net) ? F("Brain loaded.") : F("New brain. Teach me: 0 = normal, 1 = fault"));
}

void loop() {
  float f[N_IN];
  if (!measure(f)) { Serial.println(F("MPU6050 not found - check wiring")); delay(1000); return; }

  if (Serial.available()) {
    char c = Serial.read();
    if (c == '0' || c == '1') {
      // teach several measurements in a row for a stable picture
      for (int k = 0; k < 5; k++) { if (measure(f)) net.teach(f, c - '0'); }
      GrowNetEEPROM::save(net);
      Serial.print(F("Learned: ")); Serial.println(NAMES[c - '0']);
      printStatus();
    } else if (c == 's') printStatus();
    else if (c == 'r') { net.reset(); GrowNetEEPROM::erase(); Serial.println(F("Forgot everything.")); }
    return;
  }

  Serial.print(F("vibration x/y/z: "));
  for (int i = 0; i < N_IN; i++) { Serial.print(f[i], 0); Serial.print(' '); }
  Serial.print(F(" -> "));
  uint8_t r = net.predict(f);
  if (net.examples() == 0) Serial.println(F("teach me first"));
  else if (r == GrowNet::UNKNOWN) Serial.println(F("UNUSUAL - not seen before, please inspect"));
  else { Serial.print(NAMES[r]); Serial.print(F(" (")); Serial.print(net.confidence() * 100, 0); Serial.println(F("%)")); }
  delay(200);
}
