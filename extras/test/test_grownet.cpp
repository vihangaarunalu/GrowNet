// PC test for GrowNet. Build and run:
//   g++ -std=c++11 -Wall -Wextra -O2 -I../../src test_grownet.cpp -o test && ./test
// Uses the same memory budget as an Arduino Nano (1000-byte pool).
#include <stdio.h>
#include <math.h>
#include "GrowNet.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("  PASS  %s\n", msg); \
                              else { printf("  FAIL  %s\n", msg); failures++; } } while (0)

static uint32_t s = 1;
static float rr() { s = s * 1103515245u + 12345u; return ((s >> 8) % 10000) / 10000.0f; }
static float noise(float a) { return (rr() - 0.5f) * 2 * a; }

// Three "machine states" in raw, unscaled units:
// input 0 = vibration (0..1023 ADC), input 1 = temperature (deg C)
static void sample(uint8_t c, float* x) {
  if (c == 0) { x[0] = 200 + noise(30); x[1] = 35 + noise(2); }   // normal
  if (c == 1) { x[0] = 700 + noise(40); x[1] = 38 + noise(2); }   // imbalance
  if (c == 2) { x[0] = 250 + noise(30); x[1] = 70 + noise(3); }   // overheating
}

static uint8_t ee[2048];
static void eeW(uint32_t a, uint8_t v) { ee[a] = v; }
static uint8_t eeR(uint32_t a) { return ee[a]; }

int main() {
  static float pool[1000 / 4];
  GrowNet net;
  CHECK(net.begin(2, 3, pool, sizeof(pool), 777), "begin() with Nano-sized pool");
  printf("  info  Nano budget: max %u neurons, %lu params, %u memory slots\n",
         net.maxNeurons(), (unsigned long)net.maxParams(), net.memorySlots());

  float normal0[2] = {200, 35};
  CHECK(net.predict(normal0) == GrowNet::UNKNOWN, "untrained brain answers UNKNOWN");

  // Teach 8 examples of each state, interleaved, in raw units
  for (int r = 0; r < 8; r++)
    for (uint8_t c = 0; c < 3; c++) { float x[2]; sample(c, x); net.teach(x, c); }
  printf("  info  after teaching: %u neurons, %lu params, accuracy %.0f%%\n",
         net.neurons(), (unsigned long)net.params(), net.accuracy() * 100);
  CHECK(net.accuracy() >= 0.95f, "learns the taught examples (>=95%)");

  // Fresh test data
  int ok = 0, unk = 0, n = 300;
  for (int t = 0; t < n; t++) {
    uint8_t c = t % 3; float x[2]; sample(c, x);
    uint8_t p = net.predict(x);
    if (p == c) ok++;
    if (p == GrowNet::UNKNOWN) unk++;
  }
  printf("  info  new data: %d/%d correct, %d answered unknown\n", ok, n, unk);
  CHECK(ok >= n * 0.9, "classifies new data (>=90% correct)");

  // Something never taught: very high vibration AND very hot
  float weird[2] = {1000, 95};
  CHECK(net.predict(weird) == GrowNet::UNKNOWN, "says UNKNOWN for a situation never taught");
  printf("  info  novelty of unfamiliar input: %.1fx the usual distance\n", net.novelty());

  // Save and load into a bigger "chip"
  CHECK(net.saveSize() <= 1024, "whole brain fits in Nano EEPROM (1 KB)");
  printf("  info  brain size: %lu bytes\n", (unsigned long)net.saveSize());
  net.save(eeW);
  static float bigPool[60000 / 4];
  GrowNet big;
  big.begin(2, 3, bigPool, sizeof(bigPool), 5);
  CHECK(big.load(eeR), "brain loads on an ESP32-sized chip");
  int same = 0;
  for (int t = 0; t < 200; t++) {
    float x[2] = {rr() * 1100, rr() * 100};
    same += net.predict(x) == big.predict(x);
  }
  CHECK(same == 200, "loaded brain gives identical answers");

  // Learning continues after loading: add a 4th-class-free check -> keep teaching
  float x[2]; sample(1, x);
  big.teach(x, 1);
  CHECK(big.accuracy() >= 0.95f, "loaded brain keeps learning");

  // Wrong shape must be refused
  GrowNet other; static float p3[1000 / 4];
  other.begin(3, 3, p3, sizeof(p3));
  CHECK(!other.load(eeR), "refuses a brain with a different shape");

  // Range widening must not change answers
  static float p4[1000 / 4];
  GrowNet r; r.begin(1, 2, p4, sizeof(p4), 9);
  for (int i = 0; i < 10; i++) { float a[1] = {10.0f + i}; r.teach(a, 0); float b[1] = {30.0f + i}; r.teach(b, 1); }
  float probe[1] = {25}; r.predict(probe); float before = r.probability(1);
  r.learningRate = 0; float far[1] = {500}; r.teach(far, 1);   // widen range, no learning
  r.predict(probe); float after = r.probability(1);
  printf("  info  P(class1) before %.4f, after range widening %.4f\n", before, after);
  CHECK(fabs(before - after) < 1e-3, "widening an input range keeps what was learned");

  // A harder pattern (inside vs outside a ring) makes the brain grow
  static float p5[1000 / 4];
  GrowNet g; g.begin(2, 2, p5, sizeof(p5), 3);
  for (int i = 0; i < 48; i++) {
    float x[2] = {rr(), rr()};
    float d = (x[0] - .5f) * (x[0] - .5f) + (x[1] - .5f) * (x[1] - .5f);
    g.teach(x, d < 0.06f ? 0 : 1);
  }
  g.improve(100);
  int gok = 0;
  for (int t = 0; t < 400; t++) {
    float x[2] = {rr(), rr()};
    float d = (x[0] - .5f) * (x[0] - .5f) + (x[1] - .5f) * (x[1] - .5f);
    g.predict(x); gok += g.bestGuess() == (d < 0.06f ? 0 : 1);
  }
  printf("  info  ring task: grew from 4 to %u neurons, %d/400 correct on new points\n", g.neurons(), gok);
  CHECK(g.neurons() > 4, "grows extra neurons for a harder task");
  CHECK(gok >= 320, "ring task >=80% on new points");

  // Adding a sensor later keeps what was learned
  static float p6[1000 / 4];
  GrowNet a;
  CHECK(a.begin(1, 3, p6, sizeof(p6), 11, 2), "begin() with room for one more sensor");
  for (int i = 0; i < 8; i++) {
    float lo[2] = {100 + noise(10), 0}, hi[2] = {800 + noise(10), 0};
    a.teach(lo, 0); a.teach(hi, 1);
  }
  float q[2] = {120, 0}, q2[2] = {780, 0};
  uint8_t b1 = a.predict(q), b2 = a.predict(q2);
  CHECK(a.addInput(), "addInput() adds the second sensor");
  CHECK(!a.addInput(), "addInput() refuses beyond the reserved room");
  float q3[2] = {120, 55}, q4[2] = {780, 12};   // any value of the new sensor
  CHECK(a.predict(q3) == b1 && a.predict(q4) == b2, "old answers unchanged right after adding a sensor");
  // Situation 2 = same as situation 0 on sensor 1, but the new sensor is high
  for (int i = 0; i < 8; i++) {
    float n0[2] = {100 + noise(10), 10 + noise(2)}, n2[2] = {100 + noise(10), 90 + noise(2)};
    float n1[2] = {800 + noise(10), 10 + noise(2)};
    a.teach(n0, 0); a.teach(n2, 2); a.teach(n1, 1);
  }
  int aok = 0;
  for (int t = 0; t < 90; t++) {
    uint8_t c = t % 3; float x[2];
    x[0] = (c == 1 ? 800 : 100) + noise(10); x[1] = (c == 2 ? 90 : 10) + noise(2);
    a.predict(x); aok += a.bestGuess() == c;
  }
  printf("  info  after adding a sensor: %d/90 correct, %lu params in use\n", aok, (unsigned long)a.params());
  CHECK(aok >= 85, "learns a new situation that needs the new sensor");

  printf(failures ? "\n%d FAILED\n" : "\nALL PASSED\n", failures);
  return failures ? 1 : 0;
}
