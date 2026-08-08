/*
 * Vibe Watch - IR Remote Example
 *
 * This example replaces the M5Stack StopWatch touch panel and physical
 * buttons with a standard infrared remote control. It uses the same
 * BLE HID vendor-report protocol as Vibe Watch, so it works with the
 * Vibe Watch host software without changes.
 *
 * Based on:
 *   - esp32_ir-ble_kbd.ino  (IR input handling)
 *   - vibewatch/src/main.cpp (NimBLE HID and Vibe Watch protocol)
 *
 * Dependencies:
 *   - h2zero/NimBLE-Arduino
 *   - crankyoldgit/IRremoteESP8266
 *   - bblanchon/ArduinoJson
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

// -----------------------------------------------------------------------------
// Vibe Watch HID constants and report map
// -----------------------------------------------------------------------------

namespace vibe {

// Keep the advertised device name short. BLE legacy advertising packets are
// limited to 31 bytes total, and a name longer than ~18 ASCII characters can
// prevent the device from being discovered by the host.
constexpr char kDeviceName[] = "VibeWatch IR";
constexpr char kManufacturer[] = "VibeWatch";
constexpr char kModelNumber[] = "VibeWatch";
constexpr char kFirmwareVersion[] = "v1.0";

constexpr std::uint16_t kVendorId = 0x303A;
constexpr std::uint16_t kProductId = 0x8360;
constexpr std::uint16_t kProductVersion = 0x0001;

constexpr std::uint8_t kVendorReportId = 6;
constexpr std::size_t kBleReportLength = 63;
constexpr std::size_t kRpcChunkLength = 61;
constexpr std::uint8_t kChannelJsonRpc = 2;

// Compatible report map: keyboard, consumer, relative pointer, and vendor
// JSON-RPC control plane. Copied from vibewatch/include/vibe_hid.h.
static std::uint8_t kReportMap[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01, 0x05, 0x07, 0x19, 0xE0,
    0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x95, 0x06, 0x75, 0x08, 0x15, 0x00,
    0x25, 0xA4, 0x05, 0x07, 0x19, 0x00, 0x29, 0xA4, 0x81, 0x00, 0xC0,

    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x02, 0x75, 0x10, 0x95, 0x01,
    0x15, 0x00, 0x26, 0xFF, 0x07, 0x19, 0x00, 0x2A, 0xFF, 0x07, 0x81, 0x00,
    0xC0,

    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x03, 0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x05, 0x15, 0x00, 0x25, 0x01, 0x95, 0x05,
    0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x03, 0x81, 0x01, 0x05, 0x01,
    0x09, 0x30, 0x09, 0x31, 0x15, 0x81, 0x25, 0x7F, 0x95, 0x02, 0x75, 0x08,
    0x81, 0x06, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x95, 0x01, 0x75, 0x08,
    0x81, 0x06, 0x05, 0x0C, 0x0A, 0x38, 0x02, 0x15, 0x81, 0x25, 0x7F, 0x95,
    0x01, 0x75, 0x08, 0x81, 0x06, 0xC0, 0xC0,

    0x06, 0x00, 0xFF, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x06, 0x09, 0x02, 0x15,
    0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x3F, 0x81, 0x02, 0x09, 0x03,
    0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x3F, 0x91, 0x02, 0x09,
    0x04, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x3F, 0xB1, 0x02,
    0xC0,
};

}  // namespace vibe

// -----------------------------------------------------------------------------
// Hardware configuration
// -----------------------------------------------------------------------------

constexpr std::uint8_t kIrRecvPin = 22;
constexpr int kAgentCount = 6;
constexpr int kActionCount = 5;

IRrecv irrecv(kIrRecvPin);
decode_results results;

// -----------------------------------------------------------------------------
// IR code mapping (NEC format example)
// Replace these example codes with the codes your remote sends. Open the Serial
// monitor, press a button, and copy the printed hex value into the matching
// #define.
// -----------------------------------------------------------------------------

#define IR_CODE_AGENT_1  0x807F00FF
#define IR_CODE_AGENT_2  0x807F10EF
#define IR_CODE_AGENT_3  0x807F20DF
#define IR_CODE_AGENT_4  0x807F30CF
#define IR_CODE_AGENT_5  0x807F40BF
#define IR_CODE_AGENT_6  0x807F50AF

#define IR_CODE_FAST     0x807F609F
#define IR_CODE_NG       0x807F708F
#define IR_CODE_OK       0x807F807F
#define IR_CODE_PLAN     0x807F906F
#define IR_CODE_AI       0x807FA05F

#define IR_CODE_MIC      0x807FB04F
#define IR_CODE_PREV     0x807FC03F
#define IR_CODE_NEXT     0x807FD02F

// -----------------------------------------------------------------------------
// BLE state
// -----------------------------------------------------------------------------

NimBLEServer* g_server = nullptr;
NimBLEHIDDevice* g_hid = nullptr;
NimBLECharacteristic* g_vendorInput = nullptr;
volatile bool g_connected = false;
bool g_planModeEnabled = false;
int g_selectedAgent = 0;

class HidServerCallbacks : public NimBLEServerCallbacks {
  public:
    void onConnect(NimBLEServer* server, NimBLEConnInfo& connection) override {
      g_connected = true;
      server->updateConnParams(connection.getConnHandle(), 12, 24, 0, 180);
      Serial.printf("BLE connected: %s\n", connection.getAddress().toString().c_str());
    }

    void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int reason) override {
      g_connected = false;
      Serial.printf("BLE disconnected: %d\n", reason);
      NimBLEDevice::startAdvertising();
    }

    void onAuthenticationComplete(NimBLEConnInfo& connection) override {
      if (!connection.isEncrypted()) {
        Serial.println("BLE encryption failed");
        NimBLEDevice::getServer()->disconnect(connection.getConnHandle());
        return;
      }
      Serial.println("BLE pairing authenticated");
    }
};

HidServerCallbacks g_serverCallbacks;

// -----------------------------------------------------------------------------
// Protocol helpers
// -----------------------------------------------------------------------------

void sendKeyEvent(const char* key, bool pressed) {
  if (!g_connected || g_vendorInput == nullptr) {
    return;
  }

  std::uint8_t report[vibe::kBleReportLength] = {};
  report[0] = vibe::kChannelJsonRpc;
  const int written = std::snprintf(
      reinterpret_cast<char*>(&report[2]), vibe::kRpcChunkLength,
      "{\"m\":\"v.oai.hid\",\"p\":{\"k\":\"%s\",\"act\":%u}}\r\n",
      key, pressed ? 1U : 0U);
  if (written < 0 || written >= static_cast<int>(vibe::kRpcChunkLength)) {
    Serial.println("HID event payload overflow");
    return;
  }
  report[1] = static_cast<std::uint8_t>(written);
  g_vendorInput->setValue(report, sizeof(report));
  if (!g_vendorInput->notify()) {
    Serial.printf("HID notify failed: %s\n", key);
    return;
  }
  Serial.printf("HID %s %s\n", key, pressed ? "DOWN" : "UP");
}

void sendAgentEvent(int index, bool pressed) {
  char key[5];
  std::snprintf(key, sizeof(key), "AG%02d", index);
  sendKeyEvent(key, pressed);
}

void sendActionEvent(int index, bool pressed) {
  char key[6];
  std::snprintf(key, sizeof(key), "ACT%02d", index);
  sendKeyEvent(key, pressed);
}

void sendMicEvent(bool pressed) {
  sendActionEvent(10, pressed);
  delay(12);
  sendActionEvent(11, pressed);
}

void sendOuterActionEvent(int index, bool pressed) {
  if (index == 3 && pressed) {
    g_planModeEnabled = !g_planModeEnabled;
    Serial.printf("Plan mode: %s\n", g_planModeEnabled ? "on" : "off");
  }
  sendActionEvent(index == 4 ? 12 : 6 + index, pressed);
}

void triggerAgent(int index) {
  if (index < 0 || index >= kAgentCount) {
    return;
  }
  g_selectedAgent = index;
  sendAgentEvent(index, true);
  delay(50);
  sendAgentEvent(index, false);
}

void triggerAction(int index) {
  if (index < 0 || index >= kActionCount) {
    return;
  }
  sendOuterActionEvent(index, true);
  delay(50);
  sendOuterActionEvent(index, false);
}

void triggerMic() {
  sendMicEvent(true);
  delay(100);
  sendMicEvent(false);
}

// -----------------------------------------------------------------------------
// BLE setup
// -----------------------------------------------------------------------------

void addDeviceInfoCharacteristic(std::uint16_t uuid, const char* value) {
  auto* characteristic =
      g_hid->getDeviceInfoService()->createCharacteristic(uuid, NIMBLE_PROPERTY::READ);
  characteristic->setValue(value);
}

void initializeBle() {
  NimBLEDevice::init(vibe::kDeviceName);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEDevice::setSecurityAuth(true, false, true);

  g_server = NimBLEDevice::createServer();
  g_server->setCallbacks(&g_serverCallbacks);

  g_hid = new NimBLEHIDDevice(g_server);
  g_hid->setManufacturer(vibe::kManufacturer);
  g_hid->setPnp(0x01, vibe::kVendorId, vibe::kProductId, vibe::kProductVersion);
  g_hid->setHidInfo(0x00, 0x01);
  g_hid->setReportMap(vibe::kReportMap, sizeof(vibe::kReportMap));

  char serial[17];
  std::snprintf(serial, sizeof(serial), "%016llX",
                static_cast<unsigned long long>(ESP.getEfuseMac()));
  addDeviceInfoCharacteristic(0x2A24, vibe::kModelNumber);
  addDeviceInfoCharacteristic(0x2A25, serial);
  addDeviceInfoCharacteristic(0x2A26, vibe::kFirmwareVersion);

  auto* keyboardInput = g_hid->getInputReport(1);
  auto* consumerInput = g_hid->getInputReport(2);
  auto* pointerInput = g_hid->getInputReport(3);
  g_vendorInput = g_hid->getInputReport(vibe::kVendorReportId);
  g_hid->getOutputReport(vibe::kVendorReportId);
  g_hid->getFeatureReport(vibe::kVendorReportId);

  const std::uint8_t keyboardIdle[8] = {};
  const std::uint8_t consumerIdle[2] = {};
  const std::uint8_t pointerIdle[5] = {};
  const std::uint8_t vendorIdle[vibe::kBleReportLength] = {};
  keyboardInput->setValue(keyboardIdle, sizeof(keyboardIdle));
  consumerInput->setValue(consumerIdle, sizeof(consumerIdle));
  pointerInput->setValue(pointerIdle, sizeof(pointerIdle));
  g_vendorInput->setValue(vendorIdle, sizeof(vendorIdle));

  if (!g_server->start()) {
    Serial.println("Failed to start BLE GATT server");
    return;
  }

  auto* advertising = NimBLEDevice::getAdvertising();
  advertising->setName(vibe::kDeviceName);
  advertising->setAppearance(HID_KEYBOARD);
  advertising->addServiceUUID(g_hid->getHidService()->getUUID());
  advertising->enableScanResponse(true);
  advertising->start();
  Serial.printf("BLE HID advertising started as %s\n", vibe::kDeviceName);
}

// -----------------------------------------------------------------------------
// Arduino lifecycle
// -----------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  Serial.println("Vibe Watch IR Remote starting...");

  irrecv.enableIRIn();
  initializeBle();
}

void loop() {
  if (irrecv.decode(&results)) {
    Serial.print("Received IR Code: 0x");
    Serial.println(results.value, HEX);

    if (g_connected) {
      switch (results.value) {
        case IR_CODE_AGENT_1: triggerAgent(0); break;
        case IR_CODE_AGENT_2: triggerAgent(1); break;
        case IR_CODE_AGENT_3: triggerAgent(2); break;
        case IR_CODE_AGENT_4: triggerAgent(3); break;
        case IR_CODE_AGENT_5: triggerAgent(4); break;
        case IR_CODE_AGENT_6: triggerAgent(5); break;

        case IR_CODE_FAST:    triggerAction(0); break;
        case IR_CODE_NG:      triggerAction(1); break;
        case IR_CODE_OK:      triggerAction(2); break;
        case IR_CODE_PLAN:    triggerAction(3); break;
        case IR_CODE_AI:      triggerAction(4); break;

        case IR_CODE_MIC:     triggerMic(); break;

        case IR_CODE_PREV:    triggerAgent((g_selectedAgent + kAgentCount - 1) % kAgentCount); break;
        case IR_CODE_NEXT:    triggerAgent((g_selectedAgent + 1) % kAgentCount); break;

        default:
          Serial.println("Unknown IR code. Update the mapping above.");
          break;
      }
    } else {
      Serial.println("BLE not connected");
    }

    irrecv.resume();
  }

  delay(100);
}
