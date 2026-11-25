# Trainer Fan

ESP32 firmware for manually controlling a three-speed trainer fan from a phone or Mac over Bluetooth Low Energy. The ESP32 exposes the Nordic UART Service with commands for off, low, medium, and high.

The default build runs in **bench mode**. It runs the BLE protocol and speed controller without driving GPIO, so you can try it on a USB-powered ESP32 by itself.

## Build and upload

Requirements are an original ESP32 board, such as an ESP32 DevKit with an ESP32-D0WD, and [Arduino CLI](https://arduino.github.io/arduino-cli/latest/installation/). The sketch targets Arduino-ESP32 4.0.0 and uses its bundled BLE library.

From the repository root:

```sh
export ARDUINO_BOARD_MANAGER_ADDITIONAL_URLS=https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32@4.0.0

arduino-cli compile --fqbn esp32:esp32:esp32 \
  --build-path build/bench firmware/TrainerFan
arduino-cli board list
arduino-cli upload --fqbn esp32:esp32:esp32 \
  --port /dev/cu.usbserial-110 --input-dir build/bench firmware/TrainerFan
```

Replace the example USB port with the one reported for your board. In Arduino IDE, open `firmware/TrainerFan/TrainerFan.ino`, select **ESP32 Dev Module** and your USB port, then upload. The USB serial monitor runs at 115200 baud.

## Connect from your phone

1. Open [LightBlue](https://punchthrough.com/lightblue/) and connect to **Trainer Fan**.
2. Find the Nordic UART Service, UUID `6e400001-b5a3-f393-e0a9-e50e24dcca9e`.
3. Subscribe to notifications on the status characteristic `6e400003-b5a3-f393-e0a9-e50e24dcca9e`. You can also read it directly.
4. On the command characteristic `6e400002-b5a3-f393-e0a9-e50e24dcca9e`, write `SPEED:1` as ASCII using **Write with Response**. Send one command per write. A trailing newline is optional.

| Command | Action |
| --- | --- |
| `SPEED:0` | Turn off and cancel any pending change |
| `SPEED:1` | Select low |
| `SPEED:2` | Select medium |
| `SPEED:3` | Select high |
| `STATUS` | Request the current state |

A status such as `SPEED:0 TARGET:2` means the outputs are off while the controller waits to switch to medium. `SPEED:2 TARGET:2` reports that medium was selected. The reported speed is the firmware’s output state. Invalid commands report `ERR COMMAND`. If its command queue fills, the controller turns off and reports `ERR BUSY`.

The controller starts off and switches off when it detects that the controlling phone has disconnected. Each speed transition leaves all speed outputs off for at least 250 ms after releasing an active speed. The firmware accepts one controller connection at a time.

## Enable relay outputs for a bench test

Keep the fan’s mains wiring disconnected during firmware and relay tests. The speed outputs are GPIO25 for low, GPIO26 for medium, and GPIO27 for high. The optional output build assumes a tested **active-low, 3.3 V-compatible relay input stage**. For a verified active-high stage, add `-DTRAINER_FAN_RELAY_ACTIVE_LOW=0` to the quoted compiler flags below.

```sh
arduino-cli compile --fqbn esp32:esp32:esp32 \
  --build-property 'compiler.cpp.extra_flags=-DTRAINER_FAN_ENABLE_RELAYS=1' \
  --build-path build/relay firmware/TrainerFan
```

Upload using `--input-dir build/relay`. This build requires authenticated BLE pairing for command writes. If the phone prompts for a code, open the USB serial monitor at 115200 baud and enter the displayed six-digit passkey.

Check the relay board’s indicators through every speed change, reset, and phone disconnect. Hardware must hold the relay inputs inactive while the ESP32 starts or loses power. Before connecting an AC fan, have a qualified electrician verify the motor wiring and motor-load switching ratings and provide a hardware interlock that prevents two speed circuits from being energized together.

## Run the host tests

```sh
mkdir -p build
c++ -std=c++17 -Wall -Wextra -Werror -Ifirmware/TrainerFan \
  firmware/TrainerFan/FanControl.cpp tests/FanControlTest.cpp -o build/fan-control-test
./build/fan-control-test
```
