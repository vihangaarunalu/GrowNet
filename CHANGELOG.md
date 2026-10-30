# Changelog

## v0.4.0

- **Guided training in GrowNetAuto.** Type `#train` in the Serial Monitor: GrowNet learns how the sketch normally behaves (inputs to on/off outputs) in the background, shows progress, then asks `#yes`/`#no` to print CSV and the links it found.
- After training it warns about unusual situations (outside the trained range or unlike anything seen) and outputs that behave differently from what it learned. `#ok` teaches a situation as normal.
- Model saved to EEPROM and reused after restarts; new inputs are added without losing learning; `#train` again adds new outputs.
- Commands `#csv #links #status #reset #help`. Only lines starting with `#` are read, so the sketch's own Serial input is untouched.
- Training accuracy is measured on fresh readings, not memorised ones.
- GrowNet core: `remember()`, `improveSome()`, `grow()`, `distance()`, `rangeExcess()`, `refresh()`, `keepDiverse` for non-blocking training.

## v0.3.0

- **New: GrowNet Insight.** Add `#include <GrowNetAuto.h>` to any sketch and GrowNet watches its reads and writes, then reports links between signals, what drives each on/off output, stuck or unlinked sensors, unusual values and flickering outputs.
- Scales with the board: about 6 signals on a Nano, 32 on ESP32 / RP2040 / Nano 33 BLE.
- Extras: `name()`, `nameOutput()`, `watch()`, `control()`, `summary()`, `output()`.
- New example `InsightAuto`; new test suite `extras/test/test_insight.cpp`.

## v0.2.0

- **New:** `addInput()` adds a sensor later without losing what was learned. Reserve room with the new `maxInputs` argument of `begin()`.
- **New:** `inputs()` and `maxInputs()`.
- `SerialTeach` example can add a third sensor (`a` command).
- README: introduction, capabilities table, "adding a sensor later" section.
- Brain save format changed to version 3. Brains saved by v0.1.0 are not loaded; teach again after updating.

## v0.1.0

- First release: on-device learning, automatic sizing and growth, any-sensor input scaling, "I don't know" detection, portable save/load, EEPROM helper, 4 examples, PC tests and CI.
