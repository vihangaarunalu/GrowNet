// GrowNetInsight - watches the signals of a running system and discovers,
// on the chip, what the user may want to know:
//   * which signals move together (links)
//   * what turns each on/off output on (drivers)
//   * sensors that never change (stuck) or relate to nothing (no link)
//   * values far outside the usual range (unusual)
//   * outputs that switch too often (flicker)
//
// It sizes itself to the board: few signals on an Arduino Nano, many on
// ESP32 / RP2040 / Nano 33 BLE. Plain C++ (no Arduino dependency).
// Results are delivered as events; GrowNetAuto.h turns them into text.
// License: MIT
#ifndef GROWNET_INSIGHT_H
#define GROWNET_INSIGHT_H

#include <stdint.h>
#include <string.h>
#include <math.h>

#ifndef GROWNET_INSIGHT_MAX_SIGNALS
  #if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega328PB__) || defined(__AVR_ATmega168__) || defined(__AVR_ATmega32U4__)
    #define GROWNET_INSIGHT_MAX_SIGNALS 6
    #define GROWNET_INSIGHT_MAX_OUTPUTS 2
  #elif defined(__AVR__)
    #define GROWNET_INSIGHT_MAX_SIGNALS 10
    #define GROWNET_INSIGHT_MAX_OUTPUTS 4
  #else
    #define GROWNET_INSIGHT_MAX_SIGNALS 32
    #define GROWNET_INSIGHT_MAX_OUTPUTS 8
  #endif
#endif
#ifndef GROWNET_INSIGHT_MAX_OUTPUTS
  #define GROWNET_INSIGHT_MAX_OUTPUTS 4
#endif

class GrowNetInsight {
public:
  enum Kind : uint8_t { INPUT_SIGNAL = 0, OUTPUT_SIGNAL = 1 };

  enum EventType : uint8_t {
    WATCHING,   // a: signal now being watched
    LINK,       // a, b: move together (value > 0) or opposite (value < 0); value = link strength
    DRIVER,     // a: on/off output, b: input that best explains it; value = avg of b when ON, value2 = when OFF
    STUCK,      // a: input never changes; value = its constant value
    NO_LINK,    // a: input with no clear link to anything
    UNUSUAL,    // a: value far outside normal; value = reading, value2..value3 = normal range
    FLICKER     // a: output switching often; value = number of changes, value2 = seconds
  };

  struct Event { EventType type; uint8_t a, b; float value, value2, value3; };

  struct Signal {
    int16_t id; Kind kind; bool binary; const char* name;
    uint32_t n; float last, mean, var, mn, mx;
    uint16_t changes; uint32_t lastUnusual, lastFlicker;
    int8_t outSlot, driver; bool reportedStuck, reportedNoLink, announced;
  };

  typedef void (*EventFn)(const GrowNetInsight& insight, const Event& e);

  // ---------------- Settings ----------------
  EventFn  onEvent      = 0;
  uint16_t sampleMs     = 100;    // how often all signals are sampled together
  uint32_t reportMs     = 10000;  // how often new findings are reported
  uint16_t warmup       = 100;    // samples before reporting links/drivers/stuck
  float    linkStrong   = 0.7f;   // report a link above this strength
  float    unusualLimit = 5.0f;   // "unusual" = this many standard deviations away
  uint16_t window       = 2000;   // statistics follow roughly the last N samples
  uint16_t flickerLimit = 20;     // output changes per flickerMs that count as flicker
  uint32_t flickerMs    = 10000;

  // ---------------- Feeding it ----------------
  // Record the latest value of a signal (registers it on first use).
  // Returns false if there is no room for another signal on this board.
  bool record(int16_t id, Kind kind, float value, uint32_t now) {
    int8_t i = find(id, kind);
    if (i < 0) i = add(id, kind, now);
    if (i < 0) return false;
    SignalX& s = _s[i];
    if (kind == OUTPUT_SIGNAL) {
      if (s.fresh && s.last != value && s.changes < 65535) s.changes++;
      if (value != 0.0f && value != 1.0f) s.binary = false;
    }
    s.last = value;
    s.fresh = true;
    return true;
  }

  // Give a signal a readable name (the string must stay valid).
  void setName(int16_t id, Kind kind, const char* name) {
    int8_t i = find(id, kind);
    if (i < 0) i = add(id, kind, 0);
    if (i >= 0) _s[i].name = name;
  }

  // Call often (GrowNetAuto does this for you).
  void tick(uint32_t now) {
    if (!_started) { _started = true; _lastSample = now; _lastReport = now; _flickerStart = now; }
    if (now - _lastSample >= sampleMs) { _lastSample = now; sample(now); }
    if (now - _flickerStart >= flickerMs) { checkFlicker(now); _flickerStart = now; }
    if (now - _lastReport >= reportMs) { _lastReport = now; analyze(false); }
  }

  // Report every current finding again, even ones already reported.
  void summary() { announceAll(true); analyze(true); }

  // ---------------- Reading results ----------------
  uint8_t signalCount() const { return _count; }
  uint8_t capacity() const    { return GROWNET_INSIGHT_MAX_SIGNALS; }
  const Signal& signal(uint8_t i) const { return _s[i]; }
  float link(uint8_t a, uint8_t b) const { return corr(a, b); }
  int8_t indexOf(int16_t id, Kind kind) const { return find(id, kind); }
  float spread(uint8_t i) const { return sd(i); }
  // average of input j while on/off output o is ON / OFF (0 if unknown)
  float onAverage(uint8_t o, uint8_t j) const  { return _s[o].outSlot >= 0 ? _on[(uint8_t)_s[o].outSlot][j] : 0; }
  float offAverage(uint8_t o, uint8_t j) const { return _s[o].outSlot >= 0 ? _off[(uint8_t)_s[o].outSlot][j] : 0; }

private:
  struct SignalX : Signal { bool fresh; };
  SignalX  _s[GROWNET_INSIGHT_MAX_SIGNALS];
  float    _cov[GROWNET_INSIGHT_MAX_SIGNALS * (GROWNET_INSIGHT_MAX_SIGNALS - 1) / 2 + 1];
  int8_t   _linkState[GROWNET_INSIGHT_MAX_SIGNALS * (GROWNET_INSIGHT_MAX_SIGNALS - 1) / 2 + 1];
  float    _on[GROWNET_INSIGHT_MAX_OUTPUTS][GROWNET_INSIGHT_MAX_SIGNALS];
  float    _off[GROWNET_INSIGHT_MAX_OUTPUTS][GROWNET_INSIGHT_MAX_SIGNALS];
  uint16_t _nOn[GROWNET_INSIGHT_MAX_OUTPUTS], _nOff[GROWNET_INSIGHT_MAX_OUTPUTS];
  uint8_t  _count = 0, _outputs = 0;
  bool     _started = false;
  uint32_t _lastSample = 0, _lastReport = 0, _flickerStart = 0;

  static uint16_t pairIndex(uint8_t a, uint8_t b) {
    if (a > b) { uint8_t t = a; a = b; b = t; }
    return (uint16_t)b * (b - 1) / 2 + a;
  }

  int8_t find(int16_t id, Kind kind) const {
    for (uint8_t i = 0; i < _count; i++) if (_s[i].id == id && _s[i].kind == kind) return (int8_t)i;
    return -1;
  }

  int8_t add(int16_t id, Kind kind, uint32_t) {
    if (_count >= GROWNET_INSIGHT_MAX_SIGNALS) return -1;
    uint8_t i = _count++;
    SignalX& s = _s[i];
    memset(&s, 0, sizeof(s));
    s.id = id; s.kind = kind; s.binary = true; s.outSlot = -1; s.driver = -1;
    if (kind == OUTPUT_SIGNAL && _outputs < GROWNET_INSIGHT_MAX_OUTPUTS) {
      s.outSlot = (int8_t)_outputs;
      _nOn[_outputs] = 0; _nOff[_outputs] = 0;
      for (uint8_t j = 0; j < GROWNET_INSIGHT_MAX_SIGNALS; j++) { _on[_outputs][j] = 0; _off[_outputs][j] = 0; }
      _outputs++;
    }
    for (uint8_t j = 0; j < i; j++) { _cov[pairIndex(i, j)] = 0; _linkState[pairIndex(i, j)] = 0; }
    return (int8_t)i;
  }

  float sd(uint8_t i) const { return _s[i].var > 0 ? (float)sqrt(_s[i].var) : 0; }

  float corr(uint8_t a, uint8_t b) const {
    if (a == b) return 1;
    float va = _s[a].var, vb = _s[b].var;
    if (va <= 1e-9f || vb <= 1e-9f) return 0;
    float r = _cov[pairIndex(a, b)] / (float)sqrt(va * vb);
    return r > 1 ? 1 : (r < -1 ? -1 : r);
  }

  void emit(EventType t, uint8_t a, uint8_t b, float v, float v2 = 0, float v3 = 0) {
    if (!onEvent) return;
    Event e; e.type = t; e.a = a; e.b = b; e.value = v; e.value2 = v2; e.value3 = v3;
    onEvent(*this, e);
  }

  float alphaOf(uint32_t n) const {
    float a = 1.0f / (float)n, w = 1.0f / (float)window;
    return a > w ? a : w;
  }

  // Sample every signal's latest value together and update all statistics.
  void sample(uint32_t now) {
    float dx[GROWNET_INSIGHT_MAX_SIGNALS];
    bool  ok[GROWNET_INSIGHT_MAX_SIGNALS];
    for (uint8_t i = 0; i < _count; i++) {
      SignalX& s = _s[i];
      ok[i] = s.fresh || s.n > 0;
      if (!ok[i]) continue;
      float x = s.last;
      if (s.n == 0) { s.mean = x; s.mn = x; s.mx = x; s.var = 0; }
      // unusual value check (before the value is absorbed)
      float d = sd(i);
      if (s.kind == INPUT_SIGNAL && s.n >= warmup && d > 0) {
        float z = (x - s.mean) / d; if (z < 0) z = -z;
        float span = s.mx - s.mn;
        if (z > unusualLimit && (x > s.mx + 0.05f * span || x < s.mn - 0.05f * span) &&
            (s.lastUnusual == 0 || now - s.lastUnusual > 10000)) {
          s.lastUnusual = now;
          emit(UNUSUAL, i, i, x, s.mean - 2 * d, s.mean + 2 * d);
        }
      }
      dx[i] = x - s.mean;
    }
    // pairwise co-movement (exponentially weighted covariance)
    for (uint8_t i = 0; i < _count; i++) {
      if (!ok[i]) continue;
      for (uint8_t j = 0; j < i; j++) {
        if (!ok[j]) continue;
        uint32_t n = _s[i].n < _s[j].n ? _s[i].n : _s[j].n;
        float a = alphaOf(n + 1);
        float& c = _cov[pairIndex(i, j)];
        c = (1 - a) * (c + a * dx[i] * dx[j]);
      }
    }
    // per-signal mean, variance, range
    for (uint8_t i = 0; i < _count; i++) {
      if (!ok[i]) continue;
      SignalX& s = _s[i];
      s.n++;
      float a = alphaOf(s.n);
      s.mean += a * dx[i];
      s.var = (1 - a) * (s.var + a * dx[i] * dx[i]);
      if (s.last < s.mn) s.mn = s.last;
      if (s.last > s.mx) s.mx = s.last;
    }
    // what each on/off output looks like from the inputs
    for (uint8_t o = 0; o < _count; o++) {
      SignalX& out = _s[o];
      if (out.kind != OUTPUT_SIGNAL || out.outSlot < 0 || !out.binary || !ok[o]) continue;
      uint8_t slot = (uint8_t)out.outSlot;
      bool isOn = out.last != 0;
      uint16_t& cnt = isOn ? _nOn[slot] : _nOff[slot];
      if (cnt < 65535) cnt++;
      float a = alphaOf(cnt);
      float* m = isOn ? _on[slot] : _off[slot];
      for (uint8_t j = 0; j < _count; j++) {
        if (j == o || !ok[j] || _s[j].kind != INPUT_SIGNAL) continue;
        if (cnt == 1) m[j] = _s[j].last; else m[j] += a * (_s[j].last - m[j]);
      }
    }
  }

  void announceAll(bool force) {
    for (uint8_t i = 0; i < _count; i++) {
      if (_s[i].n == 0) continue;
      if (force || !_s[i].announced) { _s[i].announced = true; emit(WATCHING, i, i, 0); }
    }
  }

  bool skipLink(uint8_t i) const {
    return _s[i].kind == OUTPUT_SIGNAL && _s[i].binary;   // explained by DRIVER instead
  }

  bool isStuck(uint8_t i) const { return _s[i].n >= warmup && _s[i].mx == _s[i].mn; }

  void analyze(bool force) {
    announceAll(false);
    for (uint8_t i = 0; i < _count; i++) {
      SignalX& s = _s[i];
      if (s.kind != INPUT_SIGNAL || s.n < warmup) continue;
      bool stuck = isStuck(i);
      if (stuck && (force || !s.reportedStuck)) emit(STUCK, i, i, s.last);
      s.reportedStuck = stuck;
    }
    // links between signals
    for (uint8_t i = 0; i < _count; i++) {
      for (uint8_t j = 0; j < i; j++) {
        if (_s[i].n < warmup || _s[j].n < warmup || isStuck(i) || isStuck(j)) continue;
        if (skipLink(i) || skipLink(j)) continue;
        float r = corr(i, j);
        int8_t& st = _linkState[pairIndex(i, j)];
        int8_t now = st;
        if (r >= linkStrong) now = 1;
        else if (r <= -linkStrong) now = -1;
        else if (r < linkStrong - 0.2f && r > -linkStrong + 0.2f) now = 0;
        if (now != 0 && (now != st || force)) emit(LINK, j, i, r);
        st = now;
      }
    }
    // what drives each on/off output
    for (uint8_t o = 0; o < _count; o++) {
      SignalX& out = _s[o];
      if (out.kind != OUTPUT_SIGNAL || out.outSlot < 0 || !out.binary) continue;
      uint8_t slot = (uint8_t)out.outSlot;
      if (_nOn[slot] < 20 || _nOff[slot] < 20) continue;
      int8_t best = -1; float bestSep = 1.0f;
      for (uint8_t j = 0; j < _count; j++) {
        if (_s[j].kind != INPUT_SIGNAL || isStuck(j) || _s[j].n < warmup) continue;
        float d = sd(j); if (d <= 0) continue;
        float sep = (_on[slot][j] - _off[slot][j]) / d; if (sep < 0) sep = -sep;
        if (sep > bestSep) { bestSep = sep; best = (int8_t)j; }
      }
      if (best >= 0 && (best != out.driver || force))
        emit(DRIVER, o, (uint8_t)best, _on[slot][best], _off[slot][best]);
      out.driver = best;
    }
    // inputs that relate to nothing
    for (uint8_t i = 0; i < _count; i++) {
      SignalX& s = _s[i];
      if (s.kind != INPUT_SIGNAL || s.n < 2 * warmup || isStuck(i) || _count < 2) continue;
      float best = 0;
      for (uint8_t j = 0; j < _count; j++) {
        if (j == i || _s[j].n < warmup || isStuck(j)) continue;
        float r = corr(i, j); if (r < 0) r = -r;
        if (r > best) best = r;
        if (_s[j].kind == OUTPUT_SIGNAL && _s[j].outSlot >= 0 && _s[j].binary) {
          uint8_t slot = (uint8_t)_s[j].outSlot; float d = sd(i);
          if (d > 0 && _nOn[slot] >= 20 && _nOff[slot] >= 20) {
            float sep = (_on[slot][i] - _off[slot][i]) / d; if (sep < 0) sep = -sep;
            if (sep / 2 > best) best = sep / 2;
          }
        }
      }
      bool none = best < 0.3f;
      if (none && (force || !s.reportedNoLink)) emit(NO_LINK, i, i, best);
      s.reportedNoLink = none;
    }
  }

  void checkFlicker(uint32_t now) {
    for (uint8_t i = 0; i < _count; i++) {
      SignalX& s = _s[i];
      if (s.kind != OUTPUT_SIGNAL) continue;
      if (s.changes > flickerLimit && (s.lastFlicker == 0 || now - s.lastFlicker > 60000)) {
        s.lastFlicker = now;
        emit(FLICKER, i, i, s.changes, flickerMs / 1000.0f);
      }
      s.changes = 0;
    }
  }
};

#endif
