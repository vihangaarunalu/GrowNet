# Changelog

## v0.2.0

- **New:** `addInput()` adds a sensor later without losing what was learned. Reserve room with the new `maxInputs` argument of `begin()`.
- **New:** `inputs()` and `maxInputs()`.
- `SerialTeach` example can add a third sensor (`a` command).
- README: introduction, capabilities table, "adding a sensor later" section.
- Brain save format changed to version 3. Brains saved by v0.1.0 are not loaded; teach again after updating.

## v0.1.0

- First release: on-device learning, automatic sizing and growth, any-sensor input scaling, "I don't know" detection, portable save/load, EEPROM helper, 4 examples, PC tests and CI.
