// GrowNet - a self-growing neural network that learns ON the microcontroller
// ----------------------------------------------------------------------------
//  * Learns from examples directly on the chip (backpropagation, no PC needed)
//  * Sizes itself to the memory of the chip it runs on, from an Arduino Nano
//    (2 KB RAM) up to ESP32, RP2040, Nano 33 BLE, SAMD, STM32 ...
//  * Starts small and adds neurons by itself when it cannot learn well enough
//  * Works with ANY sensor: it learns each input's range by itself
//  * Says "I don't know" (GrowNet::UNKNOWN) for inputs unlike anything it was taught
//  * Saves its whole brain (weights + memory) through two tiny callbacks, in a
//    format that is identical on every chip, so a brain can move between MCUs
//
//  Plain C++ (no Arduino dependency), so it also compiles and runs on a PC.
//  License: MIT
// ----------------------------------------------------------------------------
#ifndef GROWNET_H
#define GROWNET_H

#include <stdint.h>
#include <string.h>
#include <math.h>

#define GROWNET_VERSION "0.1.0"

// ---------- Memory budget per board (define GROWNET_POOL_BYTES yourself to override) ----------
#ifndef GROWNET_POOL_BYTES
  #if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega328PB__) || defined(__AVR_ATmega168__)
    #define GROWNET_POOL_BYTES 1000      // Nano / Uno / Pro Mini (2 KB RAM)
  #elif defined(__AVR_ATmega32U4__)
    #define GROWNET_POOL_BYTES 1300      // Leonardo / Micro (2.5 KB RAM)
  #elif defined(__AVR_ATmega2560__)
    #define GROWNET_POOL_BYTES 5000      // Mega (8 KB RAM)
  #elif defined(__AVR__)
    #define GROWNET_POOL_BYTES 600       // other small AVRs
  #elif defined(ARDUINO_ARCH_ESP32)
    #define GROWNET_POOL_BYTES 60000
  #elif defined(ARDUINO_ARCH_ESP8266)
    #define GROWNET_POOL_BYTES 20000
  #elif defined(ARDUINO_ARCH_RP2040)
    #define GROWNET_POOL_BYTES 100000
  #elif defined(ARDUINO_ARCH_MBED)
    #define GROWNET_POOL_BYTES 100000    // Nano 33 BLE, Portenta, ...
  #elif defined(ARDUINO_ARCH_SAMD)
    #define GROWNET_POOL_BYTES 16000
  #elif defined(ARDUINO_ARCH_STM32)
    #define GROWNET_POOL_BYTES 6000
  #else
    #define GROWNET_POOL_BYTES 4096
  #endif
#endif

#if defined(__AVR__)
  #define GROWNET_DEFAULT_EPOCHS 20
#else
  #define GROWNET_DEFAULT_EPOCHS 100
#endif

class GrowNet {
public:
  static const uint8_t UNKNOWN = 255;   // returned by predict() for unfamiliar input

  typedef void    (*WriteFn)(uint32_t addr, uint8_t value);
  typedef uint8_t (*ReadFn)(uint32_t addr);

  // ---------------- Settings (change any time) ----------------
  float    learningRate   = 0.1f;
  float    targetAccuracy = 0.97f;    // train until this accuracy on remembered examples...
  uint16_t maxEpochs      = GROWNET_DEFAULT_EPOCHS;  // ...or until this many passes
  uint8_t  maxGrowPerTeach = 2;       // max neurons added per teach() call
  float    minConfidence  = 0.6f;     // below this, predict() answers UNKNOWN
  float    noveltyFactor  = 1.5f;     // larger = says "unknown" less often
  bool     detectUnknown  = true;     // set false to always return the best class

  // pool: a buffer you provide (declare it as a float array for alignment).
  // nIn  : number of inputs (raw values, any units; GrowNet scales them itself)
  // nOut : number of situations / classes (at least 2)
  bool begin(uint8_t nIn, uint8_t nOut, void* pool, uint32_t poolBytes, uint32_t seed = 12345) {
    if (nIn == 0 || nOut < 2 || nOut == UNKNOWN || pool == 0) return false;
    _nIn = nIn; _nOut = nOut; _stride = (uint16_t)nIn + 1 + nOut;

    const uint32_t fixedBytes = ((uint32_t)2 * nOut + 3u * nIn) * 4;  // outB, prob, xt, lo, hi
    const uint32_t exBytes    = (uint32_t)nIn * 2 + 1;               // one remembered example
    const uint32_t perNeuron  = ((uint32_t)_stride + 1) * 4;          // weights + activation
    if (poolBytes <= fixedBytes + 4 * exBytes) return false;

    uint32_t slots = (poolBytes / 4) / exBytes;   // about 25% of the pool for memory
    if (slots > 255) slots = 255;
    if (slots < 4) slots = 4;
    uint32_t maxH = 0;
    for (;;) {
      int32_t avail = (int32_t)poolBytes - (int32_t)fixedBytes - (int32_t)(slots * exBytes);
      maxH = avail > 0 ? (uint32_t)avail / perNeuron : 0;
      if (maxH >= 2 || slots <= 4) break;
      slots--;
    }
    if (maxH < 2) return false;
    if (maxH > 65535) maxH = 65535;
    _slots = (uint8_t)slots;
    _maxH  = (uint16_t)maxH;

    float* f = (float*)pool;
    _outB = f; f += nOut;
    _prob = f; f += nOut;
    _xt   = f; f += nIn;
    _lo   = f; f += nIn;
    _hi   = f; f += nIn;
    _act  = f; f += _maxH;
    _w    = f; f += (uint32_t)_maxH * _stride;
    _bx   = (int16_t*)f;
    _by   = (uint8_t*)(_bx + (uint32_t)_slots * nIn);

    _rng = seed ? seed : 12345;
    reset();
    return true;
  }

  // Forget everything: small fresh brain, empty memory, unknown input ranges.
  void reset() {
    _H = _maxH < 4 ? _maxH : 4;
    _count = 0; _head = 0; _ranged = false; _thr = -1;
    for (uint8_t i = 0; i < _nIn; i++) { _lo[i] = 0; _hi[i] = 0; }
    for (uint8_t k = 0; k < _nOut; k++) { _outB[k] = 0; _prob[k] = 1.0f / _nOut; }
    for (uint16_t j = 0; j < _H; j++) initNeuron(j);
    _best = 0; _unknown = true; _dist = 0;
  }

  // ---------------- Using it ----------------

  // Returns the situation (0 .. nOut-1) for these raw inputs, or GrowNet::UNKNOWN.
  uint8_t predict(const float* raw) {
    if (_count == 0) { _unknown = true; _best = 0; return UNKNOWN; }
    normalize(raw, _xt);
    forward(_xt);
    _best = argmax();
    _dist = nearestDistance(_xt, 255);
    _unknown = false;
    if (detectUnknown) {
      if (_prob[_best] < minConfidence) _unknown = true;
      if (_thr >= 0 && _dist > _thr)    _unknown = true;
    }
    return _unknown ? UNKNOWN : _best;
  }

  // Teach one example: these raw inputs belong to situation `label`.
  // Trains until targetAccuracy (or maxEpochs) and grows if it needs to.
  void teach(const float* raw, uint8_t label) {
    if (label >= _nOut) return;
    updateRanges(raw);
    normalize(raw, _xt);
    int16_t* dst = _bx + (uint32_t)_head * _nIn;
    for (uint8_t i = 0; i < _nIn; i++) dst[i] = pack(_xt[i]);
    _by[_head] = label;
    _head = (uint8_t)((_head + 1) % _slots);
    if (_count < _slots) _count++;

    trainToTarget();
    for (uint8_t g = 0; g < maxGrowPerTeach && _H < _maxH && accuracy() < targetAccuracy; g++) {
      initNeuron(_H);
      _H++;
      trainToTarget();
    }
    updateNovelty();
  }

  // Extra training passes, e.g. from loop() while the device is idle.
  void improve(uint16_t passes = 1) {
    for (uint16_t e = 0; e < passes; e++) trainPass();
  }

  // Fraction of remembered examples it classifies correctly (0..1).
  float accuracy() {
    if (_count == 0) return 0;
    uint16_t ok = 0;
    for (uint8_t k = 0; k < _count; k++) {
      unpack(k, _xt);
      forward(_xt);
      if (argmax() == _by[k]) ok++;
    }
    return (float)ok / _count;
  }

  // ---------------- Details of the last predict() ----------------
  float   confidence() const     { return _prob[_best]; }        // 0..1
  float   probability(uint8_t k) const { return k < _nOut ? _prob[k] : 0; }
  uint8_t bestGuess() const      { return _best; }               // even when UNKNOWN
  bool    isUnknown() const      { return _unknown; }
  float   novelty() const        { return _thr > 0 ? _dist / _thr : 0; }  // >1 = unfamiliar

  // ---------------- Size info ----------------
  uint8_t  inputs() const       { return _nIn; }
  uint8_t  outputs() const      { return _nOut; }
  uint16_t neurons() const      { return _H; }
  uint16_t maxNeurons() const   { return _maxH; }
  uint32_t params() const       { return (uint32_t)_H * _stride + _nOut; }
  uint32_t maxParams() const    { return (uint32_t)_maxH * _stride + _nOut; }
  uint8_t  examples() const     { return _count; }
  uint8_t  memorySlots() const  { return _slots; }

  // ---------------- Saving / loading the brain ----------------
  uint32_t saveSize() const {
    return 10 + 4 * ((uint32_t)_nOut + 2u * _nIn + 1 + (uint32_t)_H * _stride)
              + (uint32_t)_count * ((uint32_t)_nIn * 2 + 1);
  }

  bool save(WriteFn w, uint32_t base = 0) const {
    if (!w) return false;
    uint32_t a = base;
    w(a++, 'G'); w(a++, 'N'); w(a++, 2); w(a++, _nIn); w(a++, _nOut);
    w(a++, (uint8_t)(_H & 0xFF)); w(a++, (uint8_t)(_H >> 8));
    w(a++, _count); w(a++, _head); w(a++, _ranged ? 1 : 0);
    a = putFloats(w, a, _outB, _nOut);
    a = putFloats(w, a, _lo, _nIn);
    a = putFloats(w, a, _hi, _nIn);
    a = putFloats(w, a, &_thr, 1);
    a = putFloats(w, a, _w, (uint32_t)_H * _stride);
    for (uint32_t i = 0; i < (uint32_t)_count * _nIn; i++) {
      uint16_t v = (uint16_t)_bx[i];
      w(a++, (uint8_t)(v & 0xFF)); w(a++, (uint8_t)(v >> 8));
    }
    for (uint8_t k = 0; k < _count; k++) w(a++, _by[k]);
    return true;
  }

  // Fails (and changes nothing) if there is no brain, the shape differs,
  // or the brain is too big for this chip.
  bool load(ReadFn r, uint32_t base = 0) {
    if (!r) return false;
    uint32_t a = base;
    if (r(a++) != 'G' || r(a++) != 'N' || r(a++) != 2) return false;
    if (r(a++) != _nIn || r(a++) != _nOut) return false;
    uint16_t h = r(a++); h |= (uint16_t)r(a++) << 8;
    uint8_t cnt = r(a++), head = r(a++), ranged = r(a++);
    if (h < 1 || h > _maxH) return false;
    a = getFloats(r, a, _outB, _nOut);
    a = getFloats(r, a, _lo, _nIn);
    a = getFloats(r, a, _hi, _nIn);
    a = getFloats(r, a, &_thr, 1);
    a = getFloats(r, a, _w, (uint32_t)h * _stride);
    uint8_t keep = cnt <= _slots ? cnt : _slots;
    uint32_t exStart = a;
    for (uint32_t i = 0; i < (uint32_t)keep * _nIn; i++) {
      uint16_t v = r(a++); v |= (uint16_t)r(a++) << 8;
      _bx[i] = (int16_t)v;
    }
    a = exStart + (uint32_t)cnt * _nIn * 2;
    for (uint8_t k = 0; k < keep; k++) _by[k] = r(a + k);
    _H = h; _count = keep; _ranged = ranged != 0;
    _head = (keep == cnt) ? (uint8_t)(head % _slots) : (uint8_t)(keep % _slots);
    if (keep != cnt) updateNovelty();
    return true;
  }

private:
  static constexpr float PACK = 16000.0f;   // normalized value -> int16

  uint8_t  _nIn = 0, _nOut = 0, _slots = 0, _count = 0, _head = 0, _best = 0;
  uint16_t _stride = 0, _H = 0, _maxH = 0;
  uint32_t _rng = 1;
  bool     _ranged = false, _unknown = true;
  float    _thr = -1, _dist = 0;
  float   *_outB = 0, *_prob = 0, *_xt = 0, *_lo = 0, *_hi = 0, *_act = 0, *_w = 0;
  int16_t *_bx = 0;
  uint8_t *_by = 0;

  // ---- helpers ----
  float rnd() {  // xorshift32 -> [-1, 1]
    _rng ^= _rng << 13; _rng ^= _rng >> 17; _rng ^= _rng << 5;
    return (float)(_rng % 2001) / 1000.0f - 1.0f;
  }

  // Fast tanh (same result on every chip, much faster than tanh() on AVR)
  static float act(float x) {
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
  }

  static int16_t pack(float v) {
    float s = v * PACK;
    if (s > 32767.0f) s = 32767.0f;
    if (s < -32767.0f) s = -32767.0f;
    return (int16_t)(s >= 0 ? s + 0.5f : s - 0.5f);
  }

  void unpack(uint8_t k, float* out) const {
    const int16_t* s = _bx + (uint32_t)k * _nIn;
    for (uint8_t i = 0; i < _nIn; i++) out[i] = s[i] / PACK;
  }

  // New neuron: random input weights, zero output weights -> adding it
  // never changes what the brain already answers.
  void initNeuron(uint16_t j) {
    float* n = _w + (uint32_t)j * _stride;
    for (uint8_t i = 0; i < _nIn; i++) n[i] = rnd() * 1.5f;
    n[_nIn] = rnd() * 0.5f;
    for (uint8_t k = 0; k < _nOut; k++) n[_nIn + 1 + k] = 0;
  }

  // Raw value -> about -1..1 using the learned range of each input.
  void normalize(const float* raw, float* out) const {
    for (uint8_t i = 0; i < _nIn; i++) {
      float span = _hi[i] - _lo[i];
      out[i] = span > 0 ? 2.0f * (raw[i] - _lo[i]) / span - 1.0f : 0.0f;
    }
  }

  // Widen input ranges when a new example falls outside them. Stored examples
  // and first-layer weights are re-mapped exactly, so nothing learned is lost.
  void updateRanges(const float* raw) {
    if (!_ranged) {
      for (uint8_t i = 0; i < _nIn; i++) { _lo[i] = raw[i]; _hi[i] = raw[i]; }
      _ranged = true;
    }
    for (uint8_t i = 0; i < _nIn; i++) {
      float v = raw[i];
      if (v >= _lo[i] && v <= _hi[i] && _hi[i] > _lo[i]) continue;
      if (v >= _lo[i] && v <= _hi[i]) {
        // still a single point: only widen when a different value arrives
        if (v == _lo[i]) continue;
      }
      float nl = v < _lo[i] ? v : _lo[i];
      float nh = v > _hi[i] ? v : _hi[i];
      float pad = 0.05f * (nh - nl);
      nl -= pad; nh += pad;
      remapInput(i, nl, nh);
    }
  }

  void remapInput(uint8_t i, float nl, float nh) {
    float span = _hi[i] - _lo[i], nspan = nh - nl;
    if (span > 0) {
      // x_old = a * x_new + b
      float a = nspan / span;
      float b = (2.0f * (nl - _lo[i]) + nspan) / span - 1.0f;
      for (uint16_t j = 0; j < _maxH; j++) {
        float* n = _w + (uint32_t)j * _stride;
        n[_nIn] += n[i] * b;
        n[i] *= a;
      }
      for (uint8_t k = 0; k < _count; k++) {
        int16_t* s = _bx + (uint32_t)k * _nIn + i;
        float xo = *s / PACK;
        float v = _lo[i] + (xo + 1.0f) * 0.5f * span;
        *s = pack(2.0f * (v - nl) / nspan - 1.0f);
      }
    } else {
      // every stored value of this input was exactly _lo[i]
      int16_t p = pack(2.0f * (_lo[i] - nl) / nspan - 1.0f);
      for (uint8_t k = 0; k < _count; k++) _bx[(uint32_t)k * _nIn + i] = p;
    }
    _lo[i] = nl; _hi[i] = nh;
  }

  uint8_t argmax() const {
    uint8_t b = 0;
    for (uint8_t k = 1; k < _nOut; k++) if (_prob[k] > _prob[b]) b = k;
    return b;
  }

  void forward(const float* x) {
    for (uint8_t k = 0; k < _nOut; k++) _prob[k] = _outB[k];
    for (uint16_t j = 0; j < _H; j++) {
      const float* n = _w + (uint32_t)j * _stride;
      float a = n[_nIn];
      for (uint8_t i = 0; i < _nIn; i++) a += n[i] * x[i];
      a = act(a);
      _act[j] = a;
      const float* wo = n + _nIn + 1;
      for (uint8_t k = 0; k < _nOut; k++) _prob[k] += wo[k] * a;
    }
    float mx = _prob[0];
    for (uint8_t k = 1; k < _nOut; k++) if (_prob[k] > mx) mx = _prob[k];
    float sum = 0;
    for (uint8_t k = 0; k < _nOut; k++) { _prob[k] = (float)exp(_prob[k] - mx); sum += _prob[k]; }
    for (uint8_t k = 0; k < _nOut; k++) _prob[k] /= sum;
  }

  // One gradient-descent step (softmax + cross-entropy, backprop to hidden layer).
  void step(const float* x, uint8_t y) {
    forward(x);
    _prob[y] -= 1.0f;   // output error
    const float lr = learningRate;
    for (uint16_t j = 0; j < _H; j++) {
      float* n = _w + (uint32_t)j * _stride;
      float* wo = n + _nIn + 1;
      float a = _act[j], back = 0;
      for (uint8_t k = 0; k < _nOut; k++) {
        back += wo[k] * _prob[k];
        wo[k] -= lr * _prob[k] * a;
      }
      float dh = back * (1.0f - a * a);
      n[_nIn] -= lr * dh;
      for (uint8_t i = 0; i < _nIn; i++) n[i] -= lr * dh * x[i];
    }
    for (uint8_t k = 0; k < _nOut; k++) _outB[k] -= lr * _prob[k];
  }

  void trainPass() {
    for (uint8_t k = 0; k < _count; k++) {
      unpack(k, _xt);
      step(_xt, _by[k]);
    }
  }

  // Train until the target accuracy is reached (checked every 5 passes).
  void trainToTarget() {
    for (uint16_t e = 0; e < maxEpochs; e++) {
      trainPass();
      if ((e % 5) == 4 && accuracy() >= targetAccuracy) return;
    }
  }

  // Root-mean-square distance (in normalized units) to the nearest remembered
  // example, skipping example `skip` (255 = skip none).
  float nearestDistance(const float* x, uint8_t skip) const {
    float best = 1e30f;
    for (uint8_t k = 0; k < _count; k++) {
      if (k == skip) continue;
      const int16_t* s = _bx + (uint32_t)k * _nIn;
      float d = 0;
      for (uint8_t i = 0; i < _nIn; i++) { float t = x[i] - s[i] / PACK; d += t * t; }
      if (d < best) best = d;
    }
    return (float)sqrt(best / _nIn);
  }

  // Unknown threshold = how far apart remembered examples usually are.
  void updateNovelty() {
    if (_count < 3) { _thr = -1; return; }
    float tmp[8];
    float* x = _nIn <= 8 ? tmp : _xt;  // _xt is free here
    float sum = 0, sum2 = 0;
    for (uint8_t k = 0; k < _count; k++) {
      unpack(k, x);
      float d = nearestDistance(x, k);
      sum += d; sum2 += d * d;
    }
    float mean = sum / _count;
    float var = sum2 / _count - mean * mean;
    float sd = var > 0 ? (float)sqrt(var) : 0;
    float t = noveltyFactor * (mean + 3.0f * sd);
    _thr = t < 0.05f ? 0.05f : t;
  }

  static uint32_t putFloats(WriteFn w, uint32_t a, const float* src, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
      uint8_t b[4]; memcpy(b, &src[i], 4);
      for (uint8_t q = 0; q < 4; q++) w(a++, b[q]);
    }
    return a;
  }

  static uint32_t getFloats(ReadFn r, uint32_t a, float* dst, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
      uint8_t b[4];
      for (uint8_t q = 0; q < 4; q++) b[q] = r(a++);
      memcpy(&dst[i], b, 4);
    }
    return a;
  }
};

#endif
