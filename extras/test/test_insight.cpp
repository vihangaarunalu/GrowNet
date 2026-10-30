// PC test for GrowNetInsight, using the Arduino Nano's limits (6 signals).
//   g++ -std=c++11 -Wall -Wextra -O2 -I../../src -D__AVR_ATmega328P__ test_insight.cpp -o t && ./t
// (-D__AVR_ATmega328P__ only selects the Nano-sized limits; it still runs on the PC.)
#include <stdio.h>
#include "GrowNetInsight.h"

static int failures = 0;
#define CHECK(c, m) do { if (c) printf("  PASS  %s\n", m); else { printf("  FAIL  %s\n", m); failures++; } } while (0)

static const char* NAMES[] = {"A (temperature)", "B (follows A)", "C (stuck)", "D (random)", "E (opposite A)", "LED pin 9"};
static int seen[8][8];   // seen[type][signal a]
static int linkAB = 0, linkAE = 0, driverLedA = 0, unusualA = 0, flickerLed = 0;

static void onEvent(const GrowNetInsight& ins, const GrowNetInsight::Event& e) {
  const char* a = ins.signal(e.a).name; const char* b = ins.signal(e.b).name;
  switch (e.type) {
    case GrowNetInsight::WATCHING: printf("  msg   watching %s\n", a); break;
    case GrowNetInsight::LINK:
      printf("  msg   %s and %s move %s (link %.2f)\n", a, b, e.value > 0 ? "together" : "opposite", e.value);
      if ((e.a == 0 && e.b == 1) || (e.a == 1 && e.b == 0)) linkAB++;
      if ((e.a == 0 && e.b == 4) || (e.a == 4 && e.b == 0)) linkAE = e.value < 0 ? 1 : -1;
      break;
    case GrowNetInsight::DRIVER:
      printf("  msg   %s is ON mostly when %s is %s (avg %.0f when ON, %.0f when OFF)\n", a, b,
             e.value > e.value2 ? "high" : "low", e.value, e.value2);
      if (e.a == 5 && e.b == 0) driverLedA++;
      break;
    case GrowNetInsight::STUCK:   printf("  msg   %s never changes (always %.0f)\n", a, e.value); break;
    case GrowNetInsight::NO_LINK: printf("  msg   %s has no clear link to anything\n", a); break;
    case GrowNetInsight::UNUSUAL:
      printf("  msg   unusual: %s = %.0f (normal %.0f-%.0f)\n", a, e.value, e.value2, e.value3);
      if (e.a == 0) unusualA++;
      break;
    case GrowNetInsight::FLICKER:
      printf("  msg   %s is flickering (%.0f changes in %.0f s)\n", a, e.value, e.value2);
      if (e.a == 5) flickerLed++;
      break;
  }
  seen[e.type][e.a]++;
}

static uint32_t rs = 7;
static float rnd() { rs = rs * 1103515245u + 12345u; return ((rs >> 8) % 10000) / 10000.0f; }

int main() {
  GrowNetInsight ins;
  ins.onEvent = onEvent;
  CHECK(ins.capacity() == 6, "Nano limits: watches up to 6 signals");
  for (int i = 0; i < 5; i++) ins.setName(i, GrowNetInsight::INPUT_SIGNAL, NAMES[i]);
  ins.setName(9, GrowNetInsight::OUTPUT_SIGNAL, NAMES[5]);

  float a = 500;
  uint32_t t = 0;
  for (int step = 0; step < 1200; step++, t += 100) {          // 2 minutes, one reading per 100 ms
    a += (rnd() - 0.5f) * 40; if (a < 200) a = 200; if (a > 800) a = 800;
    if (step == 900) a = 1500;                                  // one sudden spike
    float b = 0.8f * a + 50 + (rnd() - 0.5f) * 20;
    float e = 1000 - a + (rnd() - 0.5f) * 20;
    ins.record(0, GrowNetInsight::INPUT_SIGNAL, a, t);
    ins.record(1, GrowNetInsight::INPUT_SIGNAL, b, t);
    ins.record(2, GrowNetInsight::INPUT_SIGNAL, 512, t);
    ins.record(3, GrowNetInsight::INPUT_SIGNAL, rnd() * 1023, t);
    ins.record(4, GrowNetInsight::INPUT_SIGNAL, e, t);
    float led = a > 520 ? 1 : 0;
    if (step > 1050) led = step % 2;                            // starts flickering near the end
    ins.record(9, GrowNetInsight::OUTPUT_SIGNAL, led, t);
    if (step == 900) a = 500;
    ins.tick(t);
  }
  CHECK(ins.signalCount() == 6, "registered all 6 signals");
  CHECK(!ins.record(42, GrowNetInsight::INPUT_SIGNAL, 1, t), "refuses a 7th signal on a Nano");
  CHECK(seen[GrowNetInsight::WATCHING][0] == 1, "announces each signal once");
  CHECK(linkAB >= 1, "finds A and B move together");
  CHECK(linkAE == 1, "finds A and E move opposite");
  CHECK(seen[GrowNetInsight::STUCK][2] == 1, "reports stuck sensor C once");
  CHECK(seen[GrowNetInsight::NO_LINK][3] >= 1, "reports random D has no link");
  CHECK(seen[GrowNetInsight::NO_LINK][0] == 0, "does not call A unlinked");
  CHECK(driverLedA >= 1, "finds the LED is driven by A");
  CHECK(unusualA >= 1, "reports the spike on A as unusual");
  CHECK(seen[GrowNetInsight::UNUSUAL][3] == 0, "no false unusual alarm on random D");
  CHECK(flickerLed >= 1, "reports the flickering LED");

  printf("\n  summary() repeats current findings:\n");
  int before = seen[GrowNetInsight::STUCK][2];
  ins.summary();
  CHECK(seen[GrowNetInsight::STUCK][2] == before + 1, "summary() repeats findings on request");

  printf(failures ? "\n%d FAILED\n" : "\nALL PASSED\n", failures);
  return failures ? 1 : 0;
}
