// GrowNetAuto - add ONE line to any Arduino sketch and GrowNet watches it,
// learns how it normally behaves, and tells you what it finds.
//
//   #include <GrowNetAuto.h>     // put it after your other #include lines
//
// Every analogRead, digitalRead, digitalWrite and analogWrite in your sketch is
// seen by GrowNet. Your code works exactly as before. Open the Serial Monitor
// and type commands that start with # (other text is left for your sketch):
//
//   #train   learn how the system normally behaves (inputs -> on/off outputs)
//   #yes #no answer GrowNet's questions after training
//   #csv     print what was learned as CSV (copy into Excel / Google Sheets)
//   #links   print links between signals and likely drivers of each output
//   #ok      "the current situation is normal" (teach it)
//   #status  short status        #reset   forget the trained model
//   #help    list commands
//
// After training GrowNet keeps watching and reports situations it has never seen
// and outputs that behave differently from what it learned. The trained model is
// saved to EEPROM (AVR, ESP32, ESP8266, RP2040) and reused after a restart or a
// code / sensor change; new sensors are added without losing what was learned.
//
// Options (define BEFORE the include):
//   #define GROWNET_AUTO_NO_SAVE           don't use EEPROM (if your sketch uses it)
//   #define GROWNET_AUTO_EEPROM_ADDR 512   where in EEPROM to save (default 0)
//   #define GROWNET_AUTO_NO_WRAP           don't wrap the Arduino functions
// On AVR boards GrowNet starts Serial at 115200 if your sketch hasn't; on other
// boards call Serial.begin(115200) yourself.
// License: MIT
#ifndef GROWNET_AUTO_H
#define GROWNET_AUTO_H

#include <Arduino.h>
#include "GrowNet.h"
#include "GrowNetInsight.h"
#ifndef GROWNET_AUTO_NO_SAVE
  #include "GrowNetEEPROM.h"
#endif

#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega328PB__) || defined(__AVR_ATmega168__) || defined(__AVR_ATmega32U4__)
  #define GA_MAX_IN 4
  #define GA_MAX_OUTBITS 2
  #define GA_POOL_BYTES 520
#elif defined(__AVR__)
  #define GA_MAX_IN 8
  #define GA_MAX_OUTBITS 3
  #define GA_POOL_BYTES 2500
#else
  #define GA_MAX_IN 8
  #define GA_MAX_OUTBITS 3
  #define GA_POOL_BYTES 3600      // sized so the whole model fits the 4 KB save area
#endif
#ifndef GROWNET_AUTO_EEPROM_ADDR
  #define GROWNET_AUTO_EEPROM_ADDR 0
#endif

namespace GrowNetAuto {

  // ------------------------------------------------------------------ state
  enum State : uint8_t { WATCHING, TRAINING, ASK_CSV, ASK_LINKS, TRAINED };

  struct Session {
    State    state;
    bool     netReady, greeted, inCmd, missingReported, newOutputReported, quietWatching;
    uint8_t  unusualRun, unexpectedRun;
    uint16_t liveOk, liveTotal;   // accuracy on fresh readings during training
    uint8_t  nMapped, nOutputs, cmdLen;
    int16_t  inIds[GA_MAX_IN];
    int16_t  outIds[GA_MAX_OUTBITS];
    bool     seen[GA_MAX_IN];
    uint32_t trainStart, lastProgress, lastGrow, lastSample, lastCheck, lastUnusual, lastUnexpected, firstSeen;
    float    lastAcc;
    char     cmd[12];
  };

  inline Session& ses()            { static Session s; return s; }
  inline GrowNetInsight& insight() { static GrowNetInsight g; return g; }
  inline GrowNet& net()            { static GrowNet n; return n; }
  inline float* pool()             { static float p[GA_POOL_BYTES / sizeof(float)]; return p; }
  inline Print*& out()             { static Print* p = &Serial; return p; }
  inline Stream*& in()             { static Stream* s = &Serial; return s; }

  // settings you may change in setup()
  inline uint32_t& minTrainMs() { static uint32_t v = 30000; return v; }   // train at least this long
  inline uint32_t& maxTrainMs() { static uint32_t v = 180000; return v; }  // and at most this long

  // ------------------------------------------------------------------ text helpers
  inline void tag() { out()->print(F("[GrowNet] ")); }

  inline void printSignal(const GrowNetInsight& g, uint8_t i) {
    Print& p = *out();
    const GrowNetInsight::Signal& s = g.signal(i);
    if (s.name) { p.print(s.name); return; }
  #if defined(__AVR__) && defined(PIN_A0)
    if (s.kind == GrowNetInsight::INPUT_SIGNAL && s.id >= PIN_A0 && s.id < PIN_A0 + 8) { p.print('A'); p.print(s.id - PIN_A0); return; }
  #endif
    p.print(F("pin ")); p.print(s.id);
  }

  inline void printId(int16_t id, GrowNetInsight::Kind kind) {
    int8_t i = insight().indexOf(id, kind);
    if (i >= 0) { printSignal(insight(), (uint8_t)i); return; }
    out()->print(F("pin ")); out()->print(id);
  }

  inline void printValue(float v) {
    float a = v < 0 ? -v : v;
    out()->print(v, a >= 100 ? 0 : (a >= 1 ? 1 : 2));
  }

  // ------------------------------------------------------------------ Insight messages
  inline void onEvent(const GrowNetInsight& g, const GrowNetInsight::Event& e) {
    Print& p = *out();
    if ((ses().state == TRAINING || ses().quietWatching) && e.type == GrowNetInsight::WATCHING) return;   // keep output tidy
    tag();
    switch (e.type) {
      case GrowNetInsight::WATCHING:
        p.print(F("Watching ")); printSignal(g, e.a);
        p.println(g.signal(e.a).kind == GrowNetInsight::OUTPUT_SIGNAL ? F(" (output)") : F(" (input)"));
        break;
      case GrowNetInsight::LINK:
        printSignal(g, e.a); p.print(F(" and ")); printSignal(g, e.b);
        p.print(e.value > 0 ? F(" move together (link ") : F(" move in opposite directions (link "));
        p.print(e.value, 2); p.println(')');
        break;
      case GrowNetInsight::DRIVER:
        printSignal(g, e.a); p.print(F(" is ON mostly when ")); printSignal(g, e.b);
        p.print(e.value > e.value2 ? F(" is high (avg ") : F(" is low (avg "));
        printValue(e.value); p.print(F(" when ON, ")); printValue(e.value2); p.println(F(" when OFF) - likely driver"));
        break;
      case GrowNetInsight::STUCK:
        printSignal(g, e.a); p.print(F(" never changes (always ")); printValue(e.value);
        p.println(F(") - check the sensor or wiring"));
        break;
      case GrowNetInsight::NO_LINK:
        printSignal(g, e.a); p.println(F(" has no clear link to anything - is it needed?"));
        break;
      case GrowNetInsight::UNUSUAL:
        p.print(F("Unusual: ")); printSignal(g, e.a); p.print(F(" = ")); printValue(e.value);
        p.print(F(" (normal ")); printValue(e.value2); p.print(F(" to ")); printValue(e.value3); p.println(')');
        break;
      case GrowNetInsight::FLICKER:
        printSignal(g, e.a); p.print(F(" is switching very often (")); p.print((int)e.value);
        p.print(F(" changes in ")); p.print((int)e.value2); p.println(F(" s) - possible flicker"));
        break;
    }
  }

  // ------------------------------------------------------------------ saving
  inline void save() {
  #if !defined(GROWNET_AUTO_NO_SAVE) && defined(GROWNET_HAS_EEPROM)
    Session& s = ses();
    const uint32_t A = GROWNET_AUTO_EEPROM_ADDR, NET = A + 8 + 2 * (GA_MAX_IN + GA_MAX_OUTBITS);
    if (!GrowNetEEPROM::available()) return;
    if (NET + net().saveSize() > GrowNetEEPROM::SIZE) { tag(); out()->println(F("(not saved: model is bigger than the EEPROM space)")); return; }
    uint32_t a = A;
    GrowNetEEPROM::writeByte(a++, 'G'); GrowNetEEPROM::writeByte(a++, 'A'); GrowNetEEPROM::writeByte(a++, 1);
    GrowNetEEPROM::writeByte(a++, s.nMapped); GrowNetEEPROM::writeByte(a++, s.nOutputs);
    a = A + 8;
    for (uint8_t i = 0; i < GA_MAX_IN; i++)      { uint16_t v = (uint16_t)s.inIds[i];  GrowNetEEPROM::writeByte(a++, v & 0xFF); GrowNetEEPROM::writeByte(a++, v >> 8); }
    for (uint8_t i = 0; i < GA_MAX_OUTBITS; i++) { uint16_t v = (uint16_t)s.outIds[i]; GrowNetEEPROM::writeByte(a++, v & 0xFF); GrowNetEEPROM::writeByte(a++, v >> 8); }
    struct W { static void w(uint32_t ad, uint8_t v) { GrowNetEEPROM::writeByte(ad + GROWNET_AUTO_EEPROM_ADDR + 8 + 2 * (GA_MAX_IN + GA_MAX_OUTBITS), v); } };
    net().save(W::w);
    GrowNetEEPROM::commit();
  #endif
  }

  inline bool load() {
  #if !defined(GROWNET_AUTO_NO_SAVE) && defined(GROWNET_HAS_EEPROM)
    Session& s = ses();
    const uint32_t A = GROWNET_AUTO_EEPROM_ADDR;
    if (!GrowNetEEPROM::available()) return false;
    if (GrowNetEEPROM::readByte(A) != 'G' || GrowNetEEPROM::readByte(A + 1) != 'A' || GrowNetEEPROM::readByte(A + 2) != 1) return false;
    uint8_t nm = GrowNetEEPROM::readByte(A + 3), no = GrowNetEEPROM::readByte(A + 4);
    if (nm < 1 || nm > GA_MAX_IN || no > GA_MAX_OUTBITS) return false;
    if (!net().begin(nm, (uint8_t)(1 << GA_MAX_OUTBITS), pool(), GA_POOL_BYTES, 1, GA_MAX_IN)) return false;
    struct R { static uint8_t r(uint32_t ad) { return GrowNetEEPROM::readByte(ad + GROWNET_AUTO_EEPROM_ADDR + 8 + 2 * (GA_MAX_IN + GA_MAX_OUTBITS)); } };
    if (!net().load(R::r)) return false;
    uint32_t a = A + 8;
    for (uint8_t i = 0; i < GA_MAX_IN; i++)      { uint16_t v = GrowNetEEPROM::readByte(a++); v |= (uint16_t)GrowNetEEPROM::readByte(a++) << 8; s.inIds[i] = (int16_t)v; }
    for (uint8_t i = 0; i < GA_MAX_OUTBITS; i++) { uint16_t v = GrowNetEEPROM::readByte(a++); v |= (uint16_t)GrowNetEEPROM::readByte(a++) << 8; s.outIds[i] = (int16_t)v; }
    s.nMapped = net().inputs(); s.nOutputs = no; s.netReady = true; net().keepDiverse = true;
    return true;
  #else
    return false;
  #endif
  }

  inline void erase() {
  #if !defined(GROWNET_AUTO_NO_SAVE) && defined(GROWNET_HAS_EEPROM)
    if (GrowNetEEPROM::available()) { GrowNetEEPROM::writeByte(GROWNET_AUTO_EEPROM_ADDR, 0xFF); GrowNetEEPROM::commit(); }
  #endif
  }

  // ------------------------------------------------------------------ model helpers
  inline bool isMapped(int16_t id) {
    for (uint8_t k = 0; k < ses().nMapped; k++) if (ses().inIds[k] == id) return true;
    return false;
  }

  // Current inputs in model order. Returns false until every input has been seen.
  inline bool currentInputs(float* x) {
    Session& s = ses(); bool all = true;
    for (uint8_t k = 0; k < s.nMapped; k++) {
      int8_t i = insight().indexOf(s.inIds[k], GrowNetInsight::INPUT_SIGNAL);
      if (i < 0) { x[k] = 0; all = false; s.seen[k] = false; continue; }
      x[k] = insight().signal((uint8_t)i).last; s.seen[k] = true;
    }
    return all;
  }

  // On/off pattern of the trained outputs as one number (bit k = output k ON).
  inline uint8_t currentLabel() {
    Session& s = ses(); uint8_t lab = 0;
    for (uint8_t k = 0; k < s.nOutputs; k++) {
      int8_t i = insight().indexOf(s.outIds[k], GrowNetInsight::OUTPUT_SIGNAL);
      if (i >= 0 && insight().signal((uint8_t)i).last != 0) lab |= (uint8_t)(1 << k);
    }
    return lab;
  }

  // Map any input seen for the first time (after training, a code change or a new sensor).
  inline void mapNewInputs(bool announce) {
    Session& s = ses(); GrowNetInsight& g = insight();
    for (uint8_t i = 0; i < g.signalCount(); i++) {
      const GrowNetInsight::Signal& sig = g.signal(i);
      if (sig.kind != GrowNetInsight::INPUT_SIGNAL || isMapped(sig.id)) continue;
      if (s.nMapped >= GA_MAX_IN) return;
      if (s.netReady) { if (!net().addInput()) return; }
      s.inIds[s.nMapped++] = sig.id;
      if (announce) {
        tag(); out()->print(F("New input ")); printSignal(g, i);
        out()->println(F(" added - previous learning kept. Type #train to include it."));
      }
    }
  }

  // Add on/off outputs not yet in the model (existing ones keep their place).
  inline void mapOutputs() {
    Session& s = ses(); GrowNetInsight& g = insight();
    for (uint8_t i = 0; i < g.signalCount() && s.nOutputs < GA_MAX_OUTBITS; i++) {
      const GrowNetInsight::Signal& sig = g.signal(i);
      if (sig.kind != GrowNetInsight::OUTPUT_SIGNAL || !sig.binary) continue;
      bool known = false;
      for (uint8_t k = 0; k < s.nOutputs; k++) if (s.outIds[k] == sig.id) known = true;
      if (!known) s.outIds[s.nOutputs++] = sig.id;
    }
  }

  // ------------------------------------------------------------------ commands
  inline void help() {
    tag(); out()->println(F("Commands: #train #csv #links #ok #status #reset #help  (answers: #yes / #no)"));
  }

  inline void status() {
    Session& s = ses(); Print& p = *out();
    tag(); p.print(F("Signals watched: ")); p.print(insight().signalCount()); p.print('/'); p.print(insight().capacity());
    p.print(F(" | model: "));
    if (!s.netReady || net().examples() == 0) { p.println(F("not trained (type #train)")); return; }
    p.print(s.nMapped); p.print(F(" inputs, ")); p.print(s.nOutputs); p.print(F(" outputs, "));
    p.print(net().neurons()); p.print(F(" neurons, ")); p.print(net().params()); p.print(F(" parameters, "));
    p.print(net().examples()); p.print(F(" examples, accuracy ")); p.print(net().accuracy() * 100, 0);
    p.println(s.state == TRAINING ? F("% (training)") : F("%"));
  }

  inline void csv() {
    Session& s = ses(); GrowNetInsight& g = insight(); Print& p = *out();
    p.println(F("--- GrowNet CSV begin ---"));
    p.println(F("signal,type,min,max,average,spread,samples"));
    for (uint8_t i = 0; i < g.signalCount(); i++) {
      const GrowNetInsight::Signal& sig = g.signal(i);
      printSignal(g, i); p.print(sig.kind == GrowNetInsight::OUTPUT_SIGNAL ? F(",output,") : F(",input,"));
      printValue(sig.mn); p.print(','); printValue(sig.mx); p.print(','); printValue(sig.mean); p.print(',');
      printValue(g.spread(i)); p.print(','); p.println((unsigned long)sig.n);
    }
    p.println();
    p.println(F("signal_a,signal_b,link"));
    for (uint8_t i = 0; i < g.signalCount(); i++)
      for (uint8_t j = 0; j < i; j++) {
        if (g.signal(i).n < 2 || g.signal(j).n < 2) continue;
        printSignal(g, j); p.print(','); printSignal(g, i); p.print(','); p.println(g.link(j, i), 2);
      }
    p.println();
    p.println(F("output,likely_driver,avg_when_on,avg_when_off"));
    for (uint8_t o = 0; o < g.signalCount(); o++) {
      const GrowNetInsight::Signal& sig = g.signal(o);
      if (sig.kind != GrowNetInsight::OUTPUT_SIGNAL || sig.driver < 0) continue;
      printSignal(g, o); p.print(','); printSignal(g, (uint8_t)sig.driver); p.print(',');
      printValue(g.onAverage(o, (uint8_t)sig.driver)); p.print(','); printValue(g.offAverage(o, (uint8_t)sig.driver)); p.println();
    }
    p.println();
    p.println(F("model,value"));
    p.print(F("trained,")); p.println(s.netReady && net().examples() > 0 ? F("yes") : F("no"));
    if (s.netReady) {
      p.print(F("inputs,")); for (uint8_t k = 0; k < s.nMapped; k++) { if (k) p.print(' '); printId(s.inIds[k], GrowNetInsight::INPUT_SIGNAL); } p.println();
      p.print(F("outputs,")); for (uint8_t k = 0; k < s.nOutputs; k++) { if (k) p.print(' '); printId(s.outIds[k], GrowNetInsight::OUTPUT_SIGNAL); } p.println();
      p.print(F("neurons,")); p.println(net().neurons());
      p.print(F("parameters,")); p.println((unsigned long)net().params());
      p.print(F("examples,")); p.println(net().examples());
      p.print(F("accuracy_percent,")); p.println(net().accuracy() * 100, 0);
    }
    p.println(F("--- GrowNet CSV end ---"));
  }

  inline void links() {
    tag(); out()->println(F("Links and likely drivers (links show what changes together, not proven causes):"));
    ses().quietWatching = true;
    insight().summary();
    ses().quietWatching = false;
  }

  inline void startTraining() {
    Session& s = ses();
    mapNewInputs(false);
    if (s.nMapped == 0) { tag(); out()->println(F("No inputs seen yet - let your sketch run a few seconds, then #train.")); return; }
    mapOutputs();
    s.newOutputReported = false;
    if (!s.netReady) {
      if (!net().begin(s.nMapped, (uint8_t)(1 << GA_MAX_OUTBITS), pool(), GA_POOL_BYTES, millis() + 1, GA_MAX_IN)) {
        tag(); out()->println(F("Not enough memory to train on this board.")); return;
      }
      net().keepDiverse = true;
      s.netReady = true;
    }
    s.state = TRAINING; s.trainStart = s.lastProgress = s.lastGrow = millis(); s.lastAcc = 0;
    s.liveOk = 0; s.liveTotal = 0;
    tag(); out()->print(F("Training... using ")); out()->print(s.nMapped); out()->print(F(" inputs and "));
    out()->print(s.nOutputs); out()->println(F(" on/off outputs. Keep the system running normally (try all its usual situations)."));
  }

  inline void finishTraining() {
    Session& s = ses();
    net().refresh();
    save();
    s.state = ASK_CSV;
    tag(); out()->print(F("Training done in ")); out()->print((millis() - s.trainStart) / 1000);
    out()->print(F(" s: ")); out()->print(net().examples()); out()->print(F(" examples, accuracy on new readings "));
    out()->print(s.lastAcc * 100, 0); out()->println('%');
    if (s.lastAcc < 0.9f) { tag(); out()->println(F("Accuracy is low: the inputs may not explain the outputs well. Results will be less reliable.")); }
    tag(); out()->println(F("Send the trained data as CSV? Type #yes or #no"));
  }

  inline void command(const char* c) {
    Session& s = ses();
    if (!strcmp(c, "#train"))       startTraining();
    else if (!strcmp(c, "#yes") || !strcmp(c, "#no")) {
      bool yes = c[1] == 'y';
      if (s.state == ASK_CSV) {
        if (yes) csv();
        s.state = ASK_LINKS;
        tag(); out()->println(F("Show links between signals and likely drivers? Type #yes or #no"));
      } else if (s.state == ASK_LINKS) {
        if (yes) links();
        s.state = TRAINED;
        tag(); out()->println(F("Trained. Watching for unusual situations and unexpected outputs. #help for commands."));
      } else { tag(); out()->println(F("Nothing to answer right now.")); }
    }
    else if (!strcmp(c, "#csv"))    csv();
    else if (!strcmp(c, "#links"))  links();
    else if (!strcmp(c, "#status")) status();
    else if (!strcmp(c, "#help"))   help();
    else if (!strcmp(c, "#ok")) {
      float x[GA_MAX_IN];
      if (!s.netReady || !currentInputs(x)) { tag(); out()->println(F("Train first (#train).")); return; }
      uint8_t lab = currentLabel();
      for (uint8_t r = 0; r < 3; r++) net().remember(x, lab);
      net().improveSome(60); net().refresh(); save();
      tag(); out()->println(F("Learned: the current situation is normal."));
    }
    else if (!strcmp(c, "#reset")) {
      s.netReady = false; s.nMapped = 0; s.nOutputs = 0; s.state = WATCHING; erase();
      tag(); out()->println(F("Trained model forgotten. Type #train to learn again."));
    }
    else { tag(); out()->print(F("Unknown command ")); out()->println(c); help(); }
  }

  // Read only lines that start with '#'; everything else stays for the sketch.
  inline void pollCommands() {
    Stream* st = in(); if (!st) return;
    Session& s = ses();
    while (st->available() > 0) {
      if (!s.inCmd) {
        if (st->peek() != '#') return;
        s.inCmd = true; s.cmdLen = 0;
      }
      int c = st->read();
      if (c < 0) return;
      if (c == '\n' || c == '\r') {
        s.cmd[s.cmdLen] = 0; s.inCmd = false;
        if (s.cmdLen > 1) {
          for (uint8_t k = 0; k < s.cmdLen; k++) if (s.cmd[k] >= 'A' && s.cmd[k] <= 'Z') s.cmd[k] += 32;
          command(s.cmd);
        }
        return;
      }
      if (s.cmdLen < sizeof(s.cmd) - 1) s.cmd[s.cmdLen++] = (char)c;
    }
  }

  // ------------------------------------------------------------------ the engine
  inline void start() {
    static bool started = false;
    if (started) return;
    started = true;
    insight().onEvent = onEvent;
  #if defined(UCSR0B) && defined(TXEN0)
    if (out() == &Serial && !(UCSR0B & (1 << TXEN0))) Serial.begin(115200);
  #endif
  #ifndef GROWNET_AUTO_NO_SAVE
    GrowNetEEPROM::begin();
  #endif
    Session& s = ses();
    s.firstSeen = millis();
    if (load()) s.state = TRAINED;
  }

  inline void greet() {
    Session& s = ses();
    if (s.greeted || millis() - s.firstSeen < 1500) return;
    s.greeted = true;
    tag();
    if (s.state == TRAINED) {
      out()->print(F("Trained model loaded (")); out()->print(net().examples());
      out()->println(F(" examples). Watching your sketch; it keeps improving. #help for commands."));
    } else {
      out()->println(F("Ready. Watching your sketch. Type #train to learn how it normally behaves. #help for commands."));
    }
  }

  inline void trainingStep(uint32_t now) {
    Session& s = ses();
    float x[GA_MAX_IN];
    mapNewInputs(false);
    if (currentInputs(x)) {
      uint8_t lab = currentLabel();
      // score the model on this fresh reading BEFORE learning from it (honest accuracy)
      if (net().examples() >= 4 && (now / 50) % 4 == 0) {
        bool saved = net().detectUnknown; net().detectUnknown = false;
        if (net().predict(x) == lab) s.liveOk++;
        net().detectUnknown = saved;
        s.liveTotal++;
      }
      // keep new or different situations, and refresh memory now and then
      if (net().examples() < 4 || net().distance(x) > 0.04f || (now / 250) % 12 == 0) net().remember(x, lab);
      net().improveSome(3);
    }
    if (now - s.lastProgress >= 10000) {
      s.lastProgress = now;
      // accuracy on fresh readings over the last 10 s (falls back to memory accuracy early on)
      float acc = s.liveTotal >= 20 ? (float)s.liveOk / s.liveTotal : net().accuracy();
      s.liveOk = 0; s.liveTotal = 0;
      if (acc < net().targetAccuracy && now - s.lastGrow >= 10000 && acc <= s.lastAcc + 0.02f) { net().grow(); s.lastGrow = now; }
      uint32_t el = now - s.trainStart;
      tag(); out()->print(F("Training... ")); out()->print(el / 1000); out()->print(F(" s, "));
      out()->print(net().examples()); out()->print(F(" examples, ")); out()->print(net().neurons());
      out()->print(F(" neurons, accuracy on new readings ")); out()->print(acc * 100, 0); out()->println('%');
      bool enough = el >= minTrainMs() && net().examples() >= 8 && acc >= 0.95f && s.lastAcc >= 0.95f;
      s.lastAcc = acc;
      if (enough || el >= maxTrainMs()) { s.lastAcc = acc; finishTraining(); }
    }
  }

  inline void monitorStep(uint32_t now) {
    Session& s = ses();
    mapNewInputs(true);
    float x[GA_MAX_IN];
    if (!currentInputs(x)) {
      if (!s.missingReported && now - s.firstSeen > 30000) {
        s.missingReported = true;
        for (uint8_t k = 0; k < s.nMapped; k++) if (!s.seen[k]) {
          tag(); out()->print(F("Input ")); printId(s.inIds[k], GrowNetInsight::INPUT_SIGNAL);
          out()->println(F(" used in training is not seen any more. Type #train to update or #reset to start over."));
        }
      }
      return;
    }
    if (!s.newOutputReported) {
      GrowNetInsight& g = insight(); uint8_t known = 0;
      for (uint8_t i = 0; i < g.signalCount(); i++) if (g.signal(i).kind == GrowNetInsight::OUTPUT_SIGNAL && g.signal(i).binary) known++;
      if (known > s.nOutputs && s.nOutputs < GA_MAX_OUTBITS) {
        s.newOutputReported = true;
        tag(); out()->println(F("A new output appeared since training. Type #train to include it."));
      }
    }
    uint8_t lab = currentLabel();
    uint8_t p = net().predict(x);
    // only warn when it lasts: 6 checks in a row (about 3 seconds)
    bool unusual = net().rangeExcess(x) > 0.2f ||
                   (p == GrowNet::UNKNOWN && net().novelty() > 1.5f && net().distance(x) > 0.25f);
    bool unexpected = !unusual && net().bestGuess() != lab && net().confidence() > 0.95f;
    s.unusualRun = unusual ? (uint8_t)(s.unusualRun < 255 ? s.unusualRun + 1 : 255) : 0;
    s.unexpectedRun = unexpected ? (uint8_t)(s.unexpectedRun < 255 ? s.unexpectedRun + 1 : 255) : 0;
    if (s.unusualRun == 6 && (s.lastUnusual == 0 || now - s.lastUnusual >= 30000)) {
      s.lastUnusual = now;
      tag(); out()->print(F("Unusual situation, not seen during training: "));
      for (uint8_t k = 0; k < s.nMapped; k++) { if (k) out()->print(F(", ")); printId(s.inIds[k], GrowNetInsight::INPUT_SIGNAL); out()->print('='); printValue(x[k]); }
      out()->println(F(". Type #ok if this is normal."));
    }
    if (s.unexpectedRun == 6 && (s.lastUnexpected == 0 || now - s.lastUnexpected >= 30000)) {
      s.lastUnexpected = now;
      uint8_t usualLab = net().bestGuess();
      for (uint8_t k = 0; k < s.nOutputs; k++) {
        bool isOn = (lab >> k) & 1, usual = (usualLab >> k) & 1;
        if (isOn == usual) continue;
        tag(); out()->print(F("Unexpected: ")); printId(s.outIds[k], GrowNetInsight::OUTPUT_SIGNAL);
        out()->print(isOn ? F(" is ON") : F(" is OFF"));
        out()->print(F(", but in this situation it was usually ")); out()->print(usual ? F("ON") : F("OFF"));
        out()->println(F(". Type #ok if this is normal now."));
      }
    }
  }

  inline void tick() {
    start();
    uint32_t now = millis();
    insight().tick(now);
    greet();
    pollCommands();
    Session& s = ses();
    if (now - s.lastSample < 50) return;
    s.lastSample = now;
    if (s.state == TRAINING) trainingStep(now);
    else if (s.netReady && net().examples() > 0 && (s.state == TRAINED || s.state == ASK_CSV || s.state == ASK_LINKS)) {
      if (now - s.lastCheck >= 500) { s.lastCheck = now; monitorStep(now); }
    }
  }

  inline void recordInput(int16_t id, float v)  { start(); insight().record(id, GrowNetInsight::INPUT_SIGNAL, v, millis()); tick(); }
  inline void recordOutput(int16_t id, float v) { start(); insight().record(id, GrowNetInsight::OUTPUT_SIGNAL, v, millis()); tick(); }

  // ---- the wrapped Arduino functions (your sketch calls these automatically) ----
  template <typename P> inline int aRead(P pin) {
    int v = analogRead(pin); recordInput((int16_t)pin, (float)v); return v;
  }
  template <typename P> inline int dRead(P pin) {
    int v = (int)digitalRead(pin); recordInput((int16_t)pin, (float)v); return v;
  }
  template <typename P, typename V> inline void dWrite(P pin, V val) {
    digitalWrite(pin, val); recordOutput((int16_t)pin, (int)val ? 1.0f : 0.0f);
  }
  template <typename P, typename V> inline void aWrite(P pin, V val) {
    analogWrite(pin, val); recordOutput((int16_t)pin, (float)val);
  }

  // ---- optional extras ----
  inline int16_t idForName(const char* n) {
    static const char* names[GROWNET_INSIGHT_MAX_SIGNALS];
    static uint8_t count = 0;
    for (uint8_t i = 0; i < count; i++) if (names[i] == n || strcmp(names[i], n) == 0) return (int16_t)(-2 - i);
    if (count >= GROWNET_INSIGHT_MAX_SIGNALS) return -1;
    names[count] = n;
    return (int16_t)(-2 - count++);
  }
  inline void watch(const char* name, float value) {          // a value from a sensor library
    int16_t id = idForName(name); if (id == -1) return;
    insight().setName(id, GrowNetInsight::INPUT_SIGNAL, name); recordInput(id, value);
  }
  inline void control(const char* name, float value) {        // an output driven through a library
    int16_t id = idForName(name); if (id == -1) return;
    insight().setName(id, GrowNetInsight::OUTPUT_SIGNAL, name); recordOutput(id, value);
  }
  template <typename P> inline void name(P pin, const char* n)       { insight().setName((int16_t)pin, GrowNetInsight::INPUT_SIGNAL, n); }
  template <typename P> inline void nameOutput(P pin, const char* n) { insight().setName((int16_t)pin, GrowNetInsight::OUTPUT_SIGNAL, n); }
  inline void summary()           { start(); insight().summary(); }
  inline void output(Print& p)    { out() = &p; }
  inline void input(Stream& s)    { in() = &s; }
  inline void noCommands()        { in() = 0; }
  inline void update()            { tick(); }   // call in loop() if your sketch rarely reads pins
  inline void train()             { start(); startTraining(); }
  inline bool isTrained()         { return ses().netReady && net().examples() > 0 && ses().state != TRAINING; }
}

// ---- make your sketch's calls go through GrowNet (only in this file and after it) ----
#ifndef GROWNET_AUTO_NO_WRAP
  #ifndef analogRead
    #define analogRead(p) GrowNetAuto::aRead(p)
  #endif
  #ifndef digitalRead
    #define digitalRead(p) GrowNetAuto::dRead(p)
  #endif
  #ifndef digitalWrite
    #define digitalWrite(p, v) GrowNetAuto::dWrite(p, v)
  #endif
  #ifndef analogWrite
    #define analogWrite(p, v) GrowNetAuto::aWrite(p, v)
  #endif
#endif

#endif
