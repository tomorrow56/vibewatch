# Vibe Watch PS3-to-IR Bridge Example

This example lets you control the `vibewatch_ir_remote` example with a
PlayStation 3 DualShock 3 controller instead of an infrared remote. The
ESP32 pairs with the DualShock 3 over classic Bluetooth (SPP) and
re-transmits the same NEC infrared codes that `vibewatch_ir_remote.ino`
already understands, so the Codex Micro / BLE side does not need to change
at all.

## Why two boards?

The DualShock 3 connects using the ESP32's classic Bluetooth (Bluedroid)
stack. That stack cannot run at the same time as NimBLE (used by
`vibewatch_ir_remote.ino` for the BLE HID / Codex Micro connection) on a
single radio -- see
[h2zero/NimBLE-Arduino#876](https://github.com/h2zero/NimBLE-Arduino/issues/876).
Splitting the two roles across an IR link avoids that conflict entirely:

```text
DualShock 3  --(Bluetooth Classic)-->  ESP32 #1 (this sketch)
                                             |
                                        (Infrared, NEC)
                                             v
                                        ESP32 #2 (vibewatch_ir_remote.ino)
                                             |
                                        (BLE HID / Codex Micro)
                                             v
                                        ChatGPT Desktop
```

## Requirements

- ESP32 dev board (this sketch)
- A second ESP32 running `vibewatch_ir_remote.ino` with its IR receiver
- IR LED (plus a small NPN transistor driver, e.g. 2N2222/2N3904, for range)
- A PlayStation 3 DualShock 3 controller
- A tool to write the ESP32's Bluetooth MAC address into the controller's
  pairing memory. [SixaxisPairTool](https://dancingpixelstudios.com/sixaxis-controller/sixaxispairtool/)
  is Windows-only; on macOS/Linux use the `tools/sixaxis_pair.py` script
  included in this example instead (no compiler required, see below), or
  build [sixaxispairer](https://github.com/user-none/sixaxispairer) /
  `sixpair.c` from source.

## Wiring

| ESP32 Pin | IR LED Circuit |
|---|---|
| GPIO 4 | Base of NPN transistor (through a ~470 Ohm resistor) |
| 3.3 V / 5V | IR LED anode (through a current-limiting resistor) |
| GND | Transistor emitter / IR LED cathode side |

The transmit pin can be changed by editing `kIrSendPin` in the sketch. Point
the IR LED at the IR receiver module of the board running
`vibewatch_ir_remote.ino`.

## Dependencies

Install the following libraries in the Arduino IDE Library Manager:

- `PS3 Controller Host` by jvpernis
- `IRremoteESP8266` by crankyoldgit

For PlatformIO, add these to `lib_deps`:

```ini
lib_deps =
    jvpernis/PS3 Controller Host @ ^1.1.0
    crankyoldgit/IRremoteESP8266 @ ^2.8.0
```

## Setup

1. Build and flash this sketch to the ESP32.
2. Open the Serial monitor at 115200 baud. It prints the ESP32's Bluetooth
   MAC address, for example:

   ```text
   ESP32 Bluetooth MAC: 01:02:03:04:05:06
   ```

3. Connect the DualShock 3 to your computer with a USB cable, then write
   that MAC address into the controller's pairing memory, replacing the
   PS3 console's address:

   - **macOS/Linux (no compiler required):**

     Recent macOS Python installs are "externally managed" (PEP 668) and
     reject `pip install` outside of a virtual environment, so create one
     first. The script uses `hidapi` rather than raw USB access, because on
     macOS the DualShock 3's USB interface is already claimed by the
     built-in IOHIDFamily driver -- `libusb`-based tools fail to claim it
     with an "Access denied" error even when run with `sudo`, while
     `hidapi` talks to it through the OS's HID stack instead:

     ```bash
     # One-time setup (not needed again on subsequent runs)
     brew install hidapi      # macOS only; on Linux install libhidapi via your package manager
     cd examples/vibewatch_ps3_ir_bridge
     python3 -m venv .venv
     .venv/bin/pip install hid

     # Command you run every time
     .venv/bin/python3 tools/sixaxis_pair.py AA:BB:CC:DD:EE:FF
     ```

     `brew install hidapi`, `python3 -m venv .venv`, and
     `.venv/bin/pip install hid` only need to be run once. Once the
     `.venv` directory and the `hid` package are installed, subsequent
     runs only need the `.venv/bin/python3 tools/sixaxis_pair.py ...`
     command.

     Replace `AA:BB:CC:DD:EE:FF` with the MAC address printed in step 2.
     Run the script with no arguments first to confirm the controller is
     detected and to see its current pairing. No `sudo` is required.

     > If this repository lives on an exFAT or FAT32 volume (common for
     > external drives), `pip` may print harmless
     > `WARNING: Ignoring invalid distribution -pip` style warnings. These
     > come from macOS creating hidden `._*` AppleDouble sidecar files for
     > metadata that exFAT/FAT32 cannot store natively, and can be safely
     > ignored -- they do not affect the script.

   - **Windows:** use [SixaxisPairTool](https://dancingpixelstudios.com/sixaxis-controller/sixaxispairtool/).

4. Unplug the USB cable and press the PS button on the controller. The
   Serial monitor prints `DualShock 3 connected` once paired.
5. Point the IR LED at a board running `vibewatch_ir_remote.ino` and press
   the mapped buttons; the Serial monitor prints each IR code it transmits.

## Button Mapping

| DualShock 3 Button | Function | IR Code Sent |
|---|---|---|
| D-Pad Up | Select agent 1 | `IR_CODE_AGENT_1` |
| D-Pad Right | Select agent 2 | `IR_CODE_AGENT_2` |
| D-Pad Down | Select agent 3 | `IR_CODE_AGENT_3` |
| D-Pad Left | Select agent 4 | `IR_CODE_AGENT_4` |
| L1 | Select agent 5 | `IR_CODE_AGENT_5` |
| R1 | Select agent 6 | `IR_CODE_AGENT_6` |
| Cross (X) | FAST action | `IR_CODE_FAST` |
| Circle (O) | OK action | `IR_CODE_OK` |
| Square | NG action | `IR_CODE_NG` |
| Triangle | AI action | `IR_CODE_AI` |
| Start | Toggle plan mode | `IR_CODE_PLAN` |
| PS button | Toggle MIC recording | `IR_CODE_MIC` |
| Left stick left/right/up/down | Analog stick nav | `IR_CODE_LEFT` / `IR_CODE_RIGHT` / `IR_CODE_UP` / `IR_CODE_DOWN` |

> **Important:** these IR code values must match the `#define` values in
> `vibewatch_ir_remote.ino`. If you customized the codes on the receiving
> board, update the matching `#define` lines near the top of this sketch as
> well.

Each button press sends exactly one IR frame (edge-triggered), matching a
single tap on the original remote; holding a button does not repeat the
code. The left analog stick is treated the same way: it must return to the
center before the same or a different direction can trigger again.

## Technical Details

For details of the BLE HID / JSON-RPC protocol that the IR codes ultimately
drive, see
[../vibewatch_ir_remote/TECHNICAL.ja.md](../vibewatch_ir_remote/TECHNICAL.ja.md)
(Japanese).
