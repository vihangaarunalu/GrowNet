# Contributing to GrowNet

Thanks for your interest! GrowNet is young, so every test, idea and fix helps.

## Easy ways to help

- **Test on a real board.** Run the `HelloGrowNet` example and [share the results](https://github.com/vihangaarunalu/GrowNet/issues/new?template=board_report.yml): board, learning time and prediction time. Real-board numbers are the most useful contribution right now.
- **Report a bug.** Include the board, the sketch (or the smallest part that shows the problem) and the Serial Monitor output.
- **Suggest an example.** A sensor or project you'd like to see.
- **Improve the docs.** Typos and clearer wording are welcome.

## Making a code change

1. Fork the repo and create a branch.
2. Keep `GrowNet.h` and `GrowNetInsight.h` free of Arduino dependencies, so they keep compiling on a PC.
3. Run the PC tests:
   ```bash
   g++ -std=c++11 -Wall -Wextra -O2 -Isrc extras/test/test_grownet.cpp -o test_grownet && ./test_grownet
   g++ -std=c++11 -Wall -Wextra -O2 -Isrc -D__AVR_ATmega328P__ extras/test/test_insight.cpp -o test_insight && ./test_insight
   ```
4. Open a pull request. GitHub automatically compiles every example for Nano, Uno, Mega, ESP32, Raspberry Pi Pico and Nano 33 BLE.

## Guidelines

- The Arduino Nano (2 KB RAM) is the baseline: new features must still fit and run there, and may do more on bigger boards.
- No `malloc`/`new`; memory is planned up front.
- Be honest in docs and messages: say what a feature can't do.

## Be kind

Be respectful and constructive. Everyone was a beginner once.
