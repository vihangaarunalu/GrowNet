# GrowNet

**Teach any microcontroller by example, and it learns on the chip itself, even a $3 Arduino Nano. No PC, no cloud.**

<!-- [![CI](https://github.com/vihangaarunalu/GrowNet/actions/workflows/ci.yml/badge.svg)](https://github.com/vihangaarunalu/GrowNet/actions/workflows/ci.yml) -->

<!-- Replace with a GIF of your board learning live: it's the most important thing in this README -->
<!-- ![GrowNet learning live](extras/demo.gif) -->

```cpp
GrowNet net;
net.begin(2, 3, pool, sizeof(pool));   // 2 sensors, 3 situations

net.teach(sensors, NORMAL);            // learns right here, on the chip
uint8_t what = net.predict(sensors);   // NORMAL, FAULT ... or GrowNet::UNKNOWN
```

## What it is

GrowNet is a small, open-source machine learning library for microcontrollers. It lets a device **learn from examples directly on the chip**, even an Arduino Nano with only 2 KB of RAM. There's no PC training, no Python and no cloud.

## Why it's needed

Most microcontroller AI is trained once on a PC and then frozen. It can't adapt to a new machine, a new room or a change over time without a developer retraining it. GrowNet keeps learning **where the data is**, so anyone can teach a device by example. It answers in milliseconds, works offline, and keeps your data on your own device.

## What's new

Compared with existing tools such as Edge Impulse, ST NanoEdge and TensorFlow Lite Micro, GrowNet combines in one small library:

1. **On-device learning on the cheapest 8-bit chips**, not only on larger 32-bit boards
2. **A network that sizes and grows itself** to each chip's memory, with no settings
3. **Built-in "I don't know"**, so it doesn't confidently guess on unfamiliar input
4. **Add sensors later without losing what it learned** (new in v0.2.0)

## Capabilities

| Capability | What it means |
|---|---|
| **Learns on the device** | Show it a situation and it trains itself with backpropagation, on the chip, in seconds |
| **Recognizes situations** | From any sensors, with a confidence score |
| **Says "I don't know"** | Returns `GrowNet::UNKNOWN` for input unlike anything it was taught |
| **Grows by itself** | Starts with 4 neurons and adds more only when needed, without forgetting |
| **Fits any board** | 153 parameters on a Nano, 12,000+ on an ESP32, from the same code |
| **Any sensor, any units** | Learns each input's range itself: ADC counts, °C, lux, amps... |
| **Add sensors later** | `addInput()` brings in a new sensor and keeps everything learned |
| **Remembers** | Saves its brain (weights and memory of examples) to EEPROM |
| **Portable brain** | A saved brain loads on a different board with identical answers |
| **Keeps learning after loading** | Teach more at any time; no retraining from scratch |
| **Fast, offline answers** | Decisions on the device, no network round trip |
| **No dependencies** | One header, plain C++, no `malloc`; also runs on a PC |

## Quick start (no extra hardware)

1. Download this repo as a ZIP. In the Arduino IDE choose **Sketch → Include Library → Add .ZIP Library**.
2. Open **File → Examples → GrowNet → HelloGrowNet**.
3. Upload to any board and open the Serial Monitor at **115200** baud.

You'll see it learn three situations, recognize new readings, and answer "I don't know" for a situation it was never taught.

## Examples

| Example | Hardware | What it shows |
|---|---|---|
| `HelloGrowNet` | None | Learning, testing and "I don't know" with simulated sensors |
| `SerialTeach` | Any 2–3 analog sensors | Teach from the Serial Monitor, add a third sensor later, saved to EEPROM |
| `ButtonTeach` | 2 sensors, 2 buttons, 3 LEDs | A teachable device with no computer at all |
| `VibrationMonitor` | MPU6050 | Learn a machine's normal vibration and flag faults |

## Adding a sensor later

Reserve room when you start, then add the sensor whenever it's connected:

```cpp
net.begin(2, 3, pool, sizeof(pool), seed, 3);   // 2 sensors now, room for 3
// ... teach and use it ...
net.addInput();                                   // the third sensor is now part of the brain
```

The new sensor starts with zero influence, so all answers stay exactly the same. Over the next few `teach()` calls it learns how much the new sensor matters. Old examples, recorded before the sensor existed, assume the first value it reads. Each reserved input costs a little memory per neuron.

## How big can it grow?

Capacity for 2 inputs and 3 situations. The library picks the memory budget for your board automatically; define `GROWNET_POOL_BYTES` before including it to change it.

| Board | Memory used | Max neurons | Max parameters | Examples remembered |
|---|---|---|---|---|
| Nano / Uno | 1,000 B | 25 | 153 | 50 |
| Leonardo | 1,300 B | 33 | 201 | 65 |
| Mega | 5,000 B | 132 | 795 | 250 |
| STM32 | 6,000 B | 167 | 1,005 | 255 |
| SAMD21 | 16,000 B | 524 | 3,147 | 255 |
| ESP8266 | 20,000 B | 667 | 4,005 | 255 |
| ESP32 | 60,000 B | 2,095 | 12,573 | 255 |
| RP2040 / Nano 33 BLE | 100,000 B | 3,524 | 21,147 | 255 |

## How it works

1. **Memory plan:** `begin()` splits one fixed buffer into a workspace, a memory of recent examples, and as many neurons as fit.
2. **Network:** inputs → one hidden layer of tanh neurons → softmax over your situations.
3. **Teaching:** each `teach()` stores the example, then trains with gradient descent until it reaches `targetAccuracy` (default 97%) or `maxEpochs`.
4. **Growing:** if accuracy is still too low, it adds a neuron with zero output weights, so it can't disturb existing answers, then trains again.
5. **Scaling:** when a value falls outside an input's known range, the range widens, and stored examples and first-layer weights are remapped exactly, so nothing learned is lost.
6. **"I don't know":** it compares the distance to the nearest remembered example with how far apart remembered examples usually are. Too far, or too low confidence, gives `UNKNOWN`.

## API

| Call | What it does |
|---|---|
| `begin(nIn, nOut, pool, bytes, seed, maxInputs)` | Set up inside your buffer; returns `false` if it doesn't fit |
| `teach(x, label)` | Learn that raw inputs `x` mean situation `label` |
| `predict(x)` | Situation number, or `GrowNet::UNKNOWN` |
| `addInput()` | Start using one more sensor, keeping what was learned |
| `confidence()`, `probability(k)` | How sure the last prediction was |
| `bestGuess()`, `isUnknown()`, `novelty()` | Details of the last prediction (`novelty() > 1` = unfamiliar) |
| `improve(passes)` | Extra training, e.g. while the device is idle |
| `accuracy()` | Accuracy on the remembered examples |
| `reset()` | Forget everything |
| `save(writeFn)`, `load(readFn)`, `saveSize()` | Store the brain anywhere through two byte callbacks |
| `inputs()`, `neurons()`, `params()`, `maxParams()`, `examples()` | Size information |

Settings: `learningRate`, `targetAccuracy`, `maxEpochs`, `maxGrowPerTeach`, `minConfidence`, `noveltyFactor`, `detectUnknown`.

`GrowNetEEPROM.h` adds one-line `GrowNetEEPROM::save(net)` and `load(net)` for AVR, ESP32, ESP8266 and RP2040.

## Test results

The PC test suite (`extras/test`) uses the exact Nano memory budget:

- Learns 3 situations from raw sensor values: 300/300 correct on new readings
- Answers `UNKNOWN` for a situation never taught (about 16× the usual distance)
- Grows from 4 to 6 neurons on a harder ring-shaped pattern (≈85% on new points)
- Brain moves from a Nano-sized to an ESP32-sized setup with identical answers
- Widening an input range keeps learned answers unchanged
- Adding a sensor keeps old answers unchanged, then learns a new situation that needs it (90/90)
- Whole brain fits in the Nano's 1 KB EEPROM

Timing on real boards is not measured yet. Contributions with measurements are very welcome.

## Do you need this?

| Use GrowNet if… | Don't if… |
|---|---|
| You can show examples but can't easily write the rule | A simple `if` threshold already works |
| Each device or site behaves differently | Every device behaves the same |
| You want learning on the device, with no cloud | Cloud processing is fine for you |
| You use small, cheap boards | You need language, image or audio AI |

## Honest limits

- It recognizes **patterns in numbers**. It's machine learning, not a chatbot: it doesn't understand language or code.
- It only knows situations it was taught. "I don't know" makes that visible, but can't make it perfect.
- On-chip training stays small on a Nano (about 150 parameters).
- It remembers a limited number of recent examples (50 on a Nano). Older ones are replaced.
- Raw audio or images need feature extraction first (see `VibrationMonitor`).
- **Never use it alone for safety-critical decisions** such as fire, gas or medical alarms.

## Roadmap

- [ ] Timing and power benchmarks on real boards
- [ ] One-line `#include` that lets existing Arduino code learn
- [ ] `AI_GUIDE.md` so AI coding tools write GrowNet-ready code
- [ ] Robot "drive to teach" example
- [ ] Sleeping neuron groups, using SD cards or extra memory when attached
- [ ] Desktop and phone app for teaching without code

## Contributing

Issues, measurements on new boards and new examples are all welcome. Please run the PC tests before opening a pull request:

```bash
g++ -std=c++11 -Wall -Wextra -O2 -Isrc extras/test/test_grownet.cpp -o test_grownet && ./test_grownet
```

## License

MIT. See [LICENSE](LICENSE).
