# Vibe Watch IR Remote Example

This example replaces the M5Stack StopWatch touch panel and physical buttons
with a standard infrared remote control. It implements the same BLE HID
vendor-report protocol as Vibe Watch, but advertises as a Codex Micro-compatible
controller so it pairs directly with ChatGPT Desktop.

It is based on:

- `ESP32-NimBLE-Keyboard/examples/esp32_ir-ble_kbd/esp32_ir-ble_kbd.ino`
  (IR input)
- `vibewatch/src/main.cpp` (NimBLE HID and Vibe Watch protocol)

## Requirements

- ESP32 dev board (any board supported by the Arduino ESP32 core)
- Infrared receiver module such as VS1838B or TSOP4838
- Any NEC-format IR remote (or a different protocol supported by
  `IRremoteESP8266`)

## Wiring

| ESP32 Pin | IR Module Pin |
|---|---|
| GPIO 22 | Data (OUT) |
| 3.3 V | VCC |
| GND | GND |

The receive pin can be changed by editing `kIrRecvPin` in the sketch.

## Dependencies

Install the following libraries in the Arduino IDE Library Manager:

- `NimBLE-Arduino` by h2zero
- `IRremoteESP8266` by crankyoldgit
- `ArduinoJson` by Benoit Blanchon

For PlatformIO, add these to `lib_deps`:

```ini
lib_deps =
    h2zero/NimBLE-Arduino @ 2.5.1
    crankyoldgit/IRremoteESP8266 @ ^2.8.0
    bblanchon/ArduinoJson @ 7.4.3
```

## Setup

1. Build and flash the sketch to the ESP32.
2. Open the Serial monitor at 115200 baud.
3. Wait for the `BLE HID advertising started as Codex Micro` message.
4. Pair the device from your computer or phone as **Codex Micro**.
5. Point your remote at the receiver and press each button once. The Serial
   monitor prints the received hex code, for example:

   ```text
   Received IR Code: 0x807F00FF
   ```

6. Copy the printed values into the matching `#define` lines near the top of
   the sketch and re-flash.

## IR Button Mapping

The default mapping uses example NEC codes. Replace them with the codes from
your own remote.

| Button | Function | Example Code |
|---|---|---|
| Agent 1 | Select agent 0 | `0x807F18E7` |
| Agent 2 | Select agent 1 | `0x807F58A7` |
| Agent 3 | Select agent 2 | `0x807FD827` |
| Agent 4 | Select agent 3 | `0x807F28D7` |
| Agent 5 | Select agent 4 | `0x807F6897` |
| Agent 6 | Select agent 5 | `0x807FE817` |
| FAST | Send action ACT06 | `0x807F00FF` |
| OK | Send action ACT07 | `0x807FC03F` |
| NG | Send action ACT08 | `0x807F50AF` |
| PLAN | Toggle plan mode, send ACT09 | `0x807F708F` |
| AI | Send action ACT12 | `0x807FD02F` |
| MIC | Toggle start/stop recording (ACT10) | `0x807F38C7` |
| LEFT | Analog stick left (v.oai.rad, a=0.5) | `0x807F20DF` |
| RIGHT | Analog stick right (v.oai.rad, a=0.0) | `0x807FE01F` |
| DOWN | Analog stick down (v.oai.rad, a=0.25) | `0x807F609F` |
| UP | Analog stick up (v.oai.rad, a=0.75) | `0x807F40BF` |

> **Note about the MIC button:** Because an IR remote cannot detect button release,
> the MIC button is implemented as a toggle. The first press sends `ACT10` DOWN to
> start recording; pressing it again sends `ACT10` UP to stop. IR repeat frames
> are ignored so a long press does not flip the state repeatedly.
>
> **Note about the analog stick:** The IR remote has no real analog stick, so
> directional buttons are mapped to the Codex Micro `v.oai.rad` joystick event.
> The angle values match the Codex Micro protocol: 0.0 = right, 0.25 = down,
> 0.5 = left, 0.75 = up.

## BLE Advertising Name

The advertised device name is set to `Codex Micro` (11 characters) so that
ChatGPT Desktop recognizes it as a Codex Micro controller. BLE legacy
advertising packets are limited to 31 bytes total, and a name longer than
approximately 18 ASCII characters can prevent the device from being discovered
by the host. Keep the name short if you change it.

## Technical Details

For a deep technical explanation of the BLE HID transmission layer,
including the Vendor Report format, the JSON-RPC protocol, and the
mapping from IR codes to host events, see
[TECHNICAL.ja.md](TECHNICAL.ja.md) (Japanese).
