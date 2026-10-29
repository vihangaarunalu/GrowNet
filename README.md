# GrowNet

**A self-growing neural network that learns on the microcontroller itself, even an Arduino Nano with 2 KB of RAM.**

No PC training. No cloud. No Python. You show it examples, it learns on the chip, and your data never leaves the device.

<!-- Replace with a GIF of your board learning live: it's the most important thing in this README -->
<!-- ![GrowNet learning live](extras/demo.gif) -->

```cpp
GrowNet net;
net.begin(2, 3, pool, sizeof(pool));   // 2 inputs, 3 situations

net.teach(sensors, NORMAL);            // learns right here, on the chip
uint8_t what = net.predict(sensors);   // NORMAL, FAULT ... or GrowNet::UNKNOWN
```

## Why it's different

Most microcontroller AI is trained once on a PC and then frozen on the chip. GrowNet keeps learning after it's deployed.

- **Learns on the chip.** Backpropagation runs on the board, so you teach it on site, in seconds.
- **Grows by itself.** It starts with 4 neurons and adds more only when it can't learn well enough, without forgetting what it knows.
- **Fits any chip automatically.** The same code uses 153 parameters on a Nano and 12,000+ on an ESP32.
- **Works with any sensor.** It learns each input's range itself, so you can feed it raw ADC counts, °C, lux, amps, whatever.
- **Says "I don't know."** If an input looks unlike anything it was taught, it returns `GrowNet::UNKNOWN` instead of guessing.
- **Portable brain.** Save to EEPROM, and load the same brain on a different chip. The format is identical everywhere.
- **Fast answers.** Decisions happen on the device with no network round trip.
- **One header, no dependencies.** Plain C++, so it also runs on a PC.

## Quick start (no extra hardware)

1. Download this repo as a ZIP. In the Arduino IDE choose **Sketch → Include Library → Add .ZIP Library**.
2. Open **File → Examples → GrowNet → HelloGrowNet**.
3. Upload to any board and open the Serial Monitor at **115200** baud.

You'll see it learn three situations, recognize new readings, and answer "I don't know" for a situation it was never taught.

## Examples

| Example | Hardware | What it shows |
|---|---|---|
| `HelloGrowNet` | None | Learning, testing and "I don't know" with simulated sensors |
| `SerialTeach` | Any 2 analog sensors | Teach live situations from the Serial Monitor; saved to EEPROM |
| `ButtonTeach` | 2 sensors, 2 buttons, 3 LEDs | A teachable device with no computer at all |
| `VibrationMonitor` | MPU6050 | Learn a machine's normal vibration and flag faults |

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

1. **Memory plan:** `begin()` splits one fixed buffer into a workspace, a memory of recent examples, and as many neurons as fit. It never uses `malloc`.
2. **Network:** inputs → one hidden layer of tanh neurons → softmax over your situations.
3. **Teaching:** each `teach()` stores the example, then trains with gradient descent until it reaches `targetAccuracy` (default 97%) or `maxEpochs`.
4. **Growing:** if accuracy is still too low, it adds a neuron with zero output weights, so the new neuron can't disturb existing answers, then trains again.
5. **Scaling:** when a new value falls outside an input's known range, the range widens. Stored examples and first-layer weights are remapped exactly, so nothing learned is lost.
6. **"I don't know":** it measures how far a new input is from the nearest remembered example, compared with how far apart remembered examples usually are. Too far away, or too low confidence, gives `UNKNOWN`.

## API

| Call | What it does |
|---|---|
| `begin(nIn, nOut, pool, bytes, seed)` | Set up inside your buffer; returns `false` if it doesn't fit |
| `teach(x, label)` | Learn that raw inputs `x` mean situation `label` |
| `predict(x)` | Situation number, or `GrowNet::UNKNOWN` |
| `confidence()`, `probability(k)` | How sure the last prediction was |
| `bestGuess()`, `isUnknown()`, `novelty()` | Details of the last prediction (`novelty() > 1` = unfamiliar) |
| `improve(passes)` | Extra training, e.g. while the device is idle |
| `accuracy()` | Accuracy on the remembered examples |
| `reset()` | Forget everything |
| `save(writeFn)`, `load(readFn)`, `saveSize()` | Store the brain anywhere through two byte callbacks |
| `neurons()`, `params()`, `maxParams()`, `examples()` | Size information |

Settings you can change: `learningRate`, `targetAccuracy`, `maxEpochs`, `maxGrowPerTeach`, `minConfidence`, `noveltyFactor`, `detectUnknown`.

`GrowNetEEPROM.h` adds one-line `GrowNetEEPROM::save(net)` and `load(net)` for AVR, ESP32, ESP8266 and RP2040.

## Test results

The PC test suite (`extras/test`) uses the exact Nano memory budget:

- Learns 3 situations from raw sensor values: 300/300 correct on new readings
- Answers `UNKNOWN` for a situation never taught (about 16× the usual distance)
- Grows from 4 to 6 neurons on a harder ring-shaped pattern (≈85% on new points)
- Brain moves from a Nano-sized to an ESP32-sized setup with identical answers
- Widening an input range keeps learned answers unchanged
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

- It recognizes **patterns in numbers**. It doesn't understand language or code, and it can't run an LLM.
- It only knows situations it was taught. "I don't know" makes that visible, but can't make it perfect.
- On-chip training stays small on a Nano (about 150 parameters).
- It remembers a limited number of recent examples (50 on a Nano). Older ones are replaced.
- Raw audio or images need feature extraction first (see `VibrationMonitor` for an example).
- **Never use it alone for safety-critical decisions** such as fire, gas or medical alarms.

## Roadmap

- [ ] Timing and power benchmarks on real boards
- [ ] Held-out validation and train-in-the-background mode
- [ ] Sleeping expert groups: store many small networks in flash and wake one at a time
- [ ] Load a PC-pretrained brain, then fine-tune on the chip
- [ ] Larger brains in flash / LittleFS on ESP32
- [ ] Desktop and phone app for teaching without code

## Contributing

Issues, measurements on new boards and new examples are all welcome. Please run the PC tests before opening a pull request:

```bash
g++ -std=c++11 -Wall -Wextra -O2 -Isrc extras/test/test_grownet.cpp -o test_grownet && ./test_grownet
```

## License

MIT. See [LICENSE](LICENSE).
