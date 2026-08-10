# Vibe Watch for M5Dial

This example ports the Vibe Watch interaction model to the
[M5Dial](https://docs.m5stack.com/ja/core/M5Dial) board. It keeps the same
NimBLE HID vendor-report protocol as the original Vibe Watch firmware, so the
host software does not need any changes.

## Hardware

- [M5Dial](https://docs.m5stack.com/ja/core/M5Dial)
  - ESP32-S3FN8, 1.28" round TFT (240 x 240), rotary encoder, push button,
    touch screen, buzzer

## Controls

| Input | Function |
|---|---|
| **Rotary encoder** | Select the next or previous agent (or action in the action layer) |
| **Encoder push button short press** | Activate the selected item |
| **Encoder push button long press (≥ 600 ms)** | Toggle between Agent layer and Action layer |
| **Touch ring** | Tap an agent or action to activate it directly |
| **Touch center** | Hold-to-talk microphone (ACT10/ACT11) |

### Action layer

The 5 actions are mapped to the same report IDs as the original Vibe Watch:

| Position | Action | Report |
|---|---|---|
| 0 | FAST | ACT06 |
| 1 | NG | ACT07 |
| 2 | OK | ACT08 |
| 3 | PLAN | ACT09 (toggles plan mode) |
| 4 | AI | ACT12 |

## Visual style

The UI mirrors the look of the main Vibe Watch firmware (`src/main.cpp`):
flat-filled circles with a bold accent keyline, vector glyph icons (mic,
lightning bolt, check, X, plan pill, assistant face) instead of plain text
abbreviations, a two-tone selection halo on the active agent, an Orbitron
display font for labels and the status readout, and a boot animation with
an expanding accent ring and six orbiting dots that previews the Agent
layer.

## Dependencies

Install the following libraries in the Arduino IDE Library Manager:

- `M5Dial` by m5stack
- `NimBLE-Arduino` by h2zero
- `ArduinoJson` by Benoit Blanchon

For PlatformIO, the included `platformio.ini` handles the dependencies:

```ini
lib_deps =
    m5stack/M5Dial @ ^1.0.3
    h2zero/NimBLE-Arduino @ 2.5.1
    bblanchon/ArduinoJson @ 7.4.3
```

## Build notes

- The `platformio.ini` uses `board = m5stack-stamps3`, which matches the
  M5StampS3 module on the M5Dial.
- `ARDUINO_USB_CDC_ON_BOOT=1` is enabled so the USB serial port is available.
- The advertised device name is `VibeDial` (or `VibeDial #1` through `#3` if
  you change the saved slot). It stays within the BLE legacy advertising
  31-byte limit.

## Protocol

The BLE layer is identical to the main Vibe Watch firmware and the
`vibewatch_ir_remote` example. See `vibewatch_ir_remote/TECHNICAL.ja.md` for
a detailed explanation of the HID report map, Vendor Report JSON-RPC framing,
host events, and advertising constraints.
