/*
 * Vibe Watch - PS3 Controller to IR Bridge
 *
 * This example lets you control the `vibewatch_ir_remote` example using a
 * PlayStation 3 DualShock 3 controller instead of an infrared remote. The
 * ESP32 pairs with the DualShock 3 over classic Bluetooth (SPP), reads the
 * button/stick state, and re-transmits the same NEC infrared codes that
 * `vibewatch_ir_remote.ino` already understands. This lets the two sketches
 * be combined without any changes to the BLE / Codex Micro side.
 *
 * Why a bridge instead of a single sketch?
 *   The DualShock 3 connects using the ESP32's classic Bluetooth (Bluedroid)
 *   stack, which cannot run at the same time as NimBLE (used by
 *   `vibewatch_ir_remote.ino` for the BLE HID / Codex Micro connection) on a
 *   single radio. Splitting the two roles across an IR link avoids that
 *   conflict entirely: this board only ever talks classic Bluetooth + IR,
 *   and `vibewatch_ir_remote.ino` only ever talks IR + BLE.
 *
 * Hardware:
 *   - ESP32 dev board
 *   - IR LED (plus a small NPN transistor driver for range) wired to
 *     `kIrSendPin`
 *   - A second ESP32 running `vibewatch_ir_remote.ino` with its IR receiver
 *     pointed at this board's IR LED
 *
 * Dependencies:
 *   - jvpernis/esp32-ps3 (PS3 Controller Host)
 *   - crankyoldgit/IRremoteESP8266
 */

#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <Ps3Controller.h>

#include <cstdint>
#include <cstdlib>

// -----------------------------------------------------------------------------
// Hardware configuration
// -----------------------------------------------------------------------------

constexpr std::uint8_t kIrSendPin = 4;

// MAC address the ESP32 will present to the DualShock 3. The controller only
// connects to whichever host MAC it was last paired to (normally a PS3
// console), so this address must be written into the controller with a tool
// such as SixaxisPairTool or sixaxispairer before it will connect here.
// Leave empty ("") to use the ESP32's own factory Bluetooth MAC address
// instead of spoofing one; print it once via `Ps3.getAddress()` and pair the
// controller to that address.
constexpr char kControllerHostMac[] = "";

IRsend irsend(kIrSendPin);

// -----------------------------------------------------------------------------
// IR code mapping
//
// These values MUST match the #define values in vibewatch_ir_remote.ino so
// the receiving board reacts the same way it would to the original IR
// remote. If you customized the codes over there, copy the same values here.
// -----------------------------------------------------------------------------

#define IR_CODE_AGENT_1  0x807F18E7
#define IR_CODE_AGENT_2  0x807F58A7
#define IR_CODE_AGENT_3  0x807FD827
#define IR_CODE_AGENT_4  0x807F28D7
#define IR_CODE_AGENT_5  0x807F6897
#define IR_CODE_AGENT_6  0x807FE817

#define IR_CODE_FAST     0x807F00FF
#define IR_CODE_NG       0x807F50AF
#define IR_CODE_OK       0x807FC03F
#define IR_CODE_PLAN     0x807F708F
#define IR_CODE_AI       0x807FD02F

#define IR_CODE_MIC      0x807F38C7

#define IR_CODE_LEFT     0x807F20DF
#define IR_CODE_RIGHT    0x807FE01F
#define IR_CODE_DOWN     0x807F609F
#define IR_CODE_UP       0x807F40BF

// -----------------------------------------------------------------------------
// DualShock 3 button mapping
//
// | Button        | IR code sent    |
// |---------------|------------------|
// | D-Pad Up      | Agent 1          |
// | D-Pad Right   | Agent 2          |
// | D-Pad Down    | Agent 3          |
// | D-Pad Left    | Agent 4          |
// | L1            | Agent 5          |
// | R1            | Agent 6          |
// | Cross (X)     | FAST             |
// | Circle (O)    | OK               |
// | Square        | NG               |
// | Triangle      | AI               |
// | Start         | PLAN             |
// | PS button     | MIC (toggle)     |
// | Left stick    | LEFT/RIGHT/UP/DOWN (analog joystick, edge-triggered) |
// -----------------------------------------------------------------------------

// Left-stick deadzone / direction tracking. The DualShock 3 analog stick
// range is roughly -128..127 per axis.
constexpr std::int8_t kStickDeadzone = 64;

enum class StickDirection { kNone, kLeft, kRight, kUp, kDown };
StickDirection g_lastStickDirection = StickDirection::kNone;

// -----------------------------------------------------------------------------
// IR transmit helper
// -----------------------------------------------------------------------------

void sendIrCode(std::uint32_t code, const char* label) {
  irsend.sendNEC(code, 32);
  Serial.printf("IR TX %-6s: 0x%08X\n", label, code);
}

// -----------------------------------------------------------------------------
// Edge-triggered button handling
//
// Each physical press sends exactly one IR frame, mirroring a single tap on
// the original IR remote. Holding a button does not repeat the code; this
// keeps agent/action selection and the MIC toggle behaving the same way they
// do with the real remote.
// -----------------------------------------------------------------------------

void handleButtons() {
  const ps3_button_t& down = Ps3.event.button_down;

  if (down.up)       sendIrCode(IR_CODE_AGENT_1, "AGENT1");
  if (down.right)    sendIrCode(IR_CODE_AGENT_2, "AGENT2");
  if (down.down)     sendIrCode(IR_CODE_AGENT_3, "AGENT3");
  if (down.left)     sendIrCode(IR_CODE_AGENT_4, "AGENT4");
  if (down.l1)       sendIrCode(IR_CODE_AGENT_5, "AGENT5");
  if (down.r1)       sendIrCode(IR_CODE_AGENT_6, "AGENT6");

  if (down.cross)    sendIrCode(IR_CODE_FAST, "FAST");
  if (down.circle)   sendIrCode(IR_CODE_OK, "OK");
  if (down.square)   sendIrCode(IR_CODE_NG, "NG");
  if (down.triangle) sendIrCode(IR_CODE_AI, "AI");
  if (down.start)    sendIrCode(IR_CODE_PLAN, "PLAN");

  if (down.ps)       sendIrCode(IR_CODE_MIC, "MIC");
}

void handleStick() {
  const std::int8_t lx = Ps3.data.analog.stick.lx;
  const std::int8_t ly = Ps3.data.analog.stick.ly;

  StickDirection direction = StickDirection::kNone;
  if (std::abs(lx) >= std::abs(ly)) {
    if (lx <= -kStickDeadzone) direction = StickDirection::kLeft;
    else if (lx >= kStickDeadzone) direction = StickDirection::kRight;
  } else {
    // DualShock 3 reports "up" as a negative Y value.
    if (ly <= -kStickDeadzone) direction = StickDirection::kUp;
    else if (ly >= kStickDeadzone) direction = StickDirection::kDown;
  }

  // Only fire once per push: the stick must return to neutral before the
  // same (or a different) direction can trigger again.
  if (direction != StickDirection::kNone &&
      direction != g_lastStickDirection) {
    switch (direction) {
      case StickDirection::kLeft:  sendIrCode(IR_CODE_LEFT, "LEFT");   break;
      case StickDirection::kRight: sendIrCode(IR_CODE_RIGHT, "RIGHT"); break;
      case StickDirection::kUp:    sendIrCode(IR_CODE_UP, "UP");       break;
      case StickDirection::kDown:  sendIrCode(IR_CODE_DOWN, "DOWN");   break;
      default: break;
    }
  }
  g_lastStickDirection = direction;
}

// -----------------------------------------------------------------------------
// PS3 controller callbacks
// -----------------------------------------------------------------------------

void onPs3Notify() {
  if (!Ps3.isConnected()) {
    return;
  }
  handleButtons();
  handleStick();
}

void onPs3Connect() {
  Serial.println("DualShock 3 connected");
  Ps3.setPlayer(1);
}

void onPs3Disconnect() {
  Serial.println("DualShock 3 disconnected");
  g_lastStickDirection = StickDirection::kNone;
}

// -----------------------------------------------------------------------------
// Arduino lifecycle
// -----------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  Serial.println("Vibe Watch PS3-to-IR Bridge starting...");

  irsend.begin();

  Ps3.attach(onPs3Notify);
  Ps3.attachOnConnect(onPs3Connect);
  Ps3.attachOnDisconnect(onPs3Disconnect);

  if (sizeof(kControllerHostMac) > 1) {
    Ps3.begin(kControllerHostMac);
  } else {
    Ps3.begin();
  }

  Serial.print("ESP32 Bluetooth MAC: ");
  Serial.println(Ps3.getAddress());
  Serial.println("Pair the DualShock 3 to the address above, then press PS.");
}

void loop() {
  // All input handling happens in onPs3Notify(), which the PS3 Controller
  // Host library calls whenever a new report arrives over Bluetooth.
  delay(20);
}
