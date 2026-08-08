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
#include <cstdlib>
#include <cstring>

// -----------------------------------------------------------------------------
// Vibe Watch HID constants and report map
// -----------------------------------------------------------------------------

namespace vibe {

// Identify as an OpenAI Codex Micro-compatible controller so ChatGPT
// Desktop recognizes the device. The BLE advertised name must be exactly
// "Codex Micro" and the PnP VID:PID must match 303A:8360 (USB source).
// If you want to use the original Vibe Watch host instead, restore the
// previous "VibeWatch" strings and setPnp(0x01, ...).
constexpr char kDeviceName[] = "Codex Micro";
constexpr char kManufacturer[] = "Work Louder";
constexpr char kModelNumber[] = "Codex Micro";
constexpr char kFirmwareVersion[] = "v1.0";

constexpr std::uint16_t kVendorId = 0x303A;
constexpr std::uint16_t kProductId = 0x8360;
constexpr std::uint16_t kProductVersion = 0x0101;

constexpr std::uint8_t kVendorReportId = 6;
constexpr std::size_t kBleReportLength = 63;
constexpr std::size_t kRpcChunkLength = 61;
constexpr std::size_t kRpcBufferLength = 2048;
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

// Analog stick directions (v.oai.rad). Angles match the Codex Micro protocol:
// 0.0 = right, 0.25 = down, 0.5 = left, 0.75 = up.
// The previous PREV/NEXT buttons are reused as LEFT/RIGHT.
#define IR_CODE_LEFT     0x807F20DF
#define IR_CODE_RIGHT    0x807FE01F
#define IR_CODE_DOWN     0x807F609F
#define IR_CODE_UP       0x807F40BF

// -----------------------------------------------------------------------------
// BLE state
// -----------------------------------------------------------------------------

NimBLEServer* g_server = nullptr;
NimBLEHIDDevice* g_hid = nullptr;
NimBLECharacteristic* g_vendorInput = nullptr;
NimBLECharacteristic* g_vendorOutput = nullptr;
QueueHandle_t g_rpcQueue = nullptr;
volatile bool g_connected = false;
bool g_planModeEnabled = false;
int g_selectedAgent = 0;
String g_rxBuffer;
std::uint8_t g_batteryLevel = 100;
bool g_isCharging = false;
std::uint32_t g_lastIrCode = 0;
bool g_micRecording = false;

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

void sendJoystickEvent(float angle, float distance) {
  if (!g_connected || g_vendorInput == nullptr) {
    return;
  }

  std::uint8_t report[vibe::kBleReportLength] = {};
  report[0] = vibe::kChannelJsonRpc;
  const int written = std::snprintf(
      reinterpret_cast<char*>(&report[2]), vibe::kRpcChunkLength,
      "{\"method\":\"v.oai.rad\",\"params\":{\"a\":%.2f,\"d\":%.2f}}\r\n",
      angle, distance);
  if (written < 0 || written >= static_cast<int>(vibe::kRpcChunkLength)) {
    Serial.println("Joystick payload overflow");
    return;
  }
  report[1] = static_cast<std::uint8_t>(written);
  g_vendorInput->setValue(report, sizeof(report));
  if (!g_vendorInput->notify()) {
    Serial.println("Joystick notify failed");
    return;
  }
  Serial.printf("Joystick a=%.2f d=%.2f\n", angle, distance);
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

void triggerMic(bool fromRepeat) {
  if (fromRepeat) {
    return;
  }
  g_micRecording = !g_micRecording;
  sendMicEvent(g_micRecording);
  Serial.printf("Mic %s\n", g_micRecording ? "recording" : "stopped");
}

void triggerJoystick(float angle) {
  // Press the virtual stick for 100 ms, then release.
  sendJoystickEvent(angle, 1.0f);
  delay(100);
  sendJoystickEvent(angle, 0.0f);
}

// -----------------------------------------------------------------------------
// Host -> device RPC
// -----------------------------------------------------------------------------


void sendFramedJson(String payload, bool appendCrlf) {
  if (!g_connected || g_vendorInput == nullptr) {
    return;
  }
  if (appendCrlf && !payload.endsWith("\r\n")) {
    payload += "\r\n";
  }

  const std::size_t total = payload.length();
  std::size_t offset = 0;
  while (offset < total) {
    const std::size_t chunk = std::min(vibe::kRpcChunkLength, total - offset);
    std::uint8_t report[vibe::kBleReportLength] = {};
    report[0] = vibe::kChannelJsonRpc;
    report[1] = static_cast<std::uint8_t>(chunk);
    std::memcpy(&report[2], payload.c_str() + offset, chunk);
    g_vendorInput->setValue(report, sizeof(report));
    if (!g_vendorInput->notify()) {
      Serial.println("RPC notify failed");
      return;
    }
    offset += chunk;
    if (offset < total) {
      delay(8);
    }
  }
}

void processRpc(const char* json);

class RpcOutputCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo&) override {
    const NimBLEAttValue value = characteristic->getValue();
    const auto* data = value.data();
    std::size_t length = value.size();
    if (data == nullptr || length < 2) {
      return;
    }

    // macOS may prefix the output report with the report ID (0x06). Accept
    // both raw report body and ID-prefixed forms for maximum compatibility.
    std::size_t offset = 0;
    if (length >= 3 && data[0] == vibe::kVendorReportId) {
      offset = 1;
    }
    if (data[offset] != vibe::kChannelJsonRpc) {
      return;
    }

    const std::size_t chunkLength = data[offset + 1];
    if (chunkLength > vibe::kRpcChunkLength || chunkLength > length - offset - 2) {
      Serial.println("Invalid RPC chunk");
      g_rxBuffer = "";
      return;
    }
    if (g_rxBuffer.length() + chunkLength > vibe::kRpcBufferLength) {
      Serial.println("RPC request too large");
      g_rxBuffer = "";
      return;
    }

    for (std::size_t i = 0; i < chunkLength; ++i) {
      g_rxBuffer += static_cast<char>(data[offset + 2 + i]);
    }

    JsonDocument probe;
    const DeserializationError parseResult = deserializeJson(probe, g_rxBuffer);
    if (parseResult == DeserializationError::IncompleteInput) {
      return;
    }
    if (parseResult) {
      Serial.printf("Discarding malformed RPC: %s\n", parseResult.c_str());
      g_rxBuffer = "";
      return;
    }

    auto* message = static_cast<char*>(std::malloc(g_rxBuffer.length() + 1));
    if (message == nullptr) {
      Serial.println("RPC allocation failed");
      g_rxBuffer = "";
      return;
    }
    std::memcpy(message, g_rxBuffer.c_str(), g_rxBuffer.length());
    message[g_rxBuffer.length()] = '\0';
    if (xQueueSend(g_rpcQueue, &message, 0) != pdTRUE) {
      Serial.println("RPC queue full");
      std::free(message);
    }
    g_rxBuffer = "";
  }
};

RpcOutputCallbacks g_rpcCallbacks;

void processRpc(const char* json) {
  JsonDocument request;
  const DeserializationError error = deserializeJson(request, json);
  if (error) {
    Serial.printf("RPC parse failed: %s\n", error.c_str());
    return;
  }

  JsonVariantConst id = request["id"] | request["i"];
  const char* method = request["method"] | request["m"] | "";
  JsonVariantConst params = request["params"];
  if (params.isNull()) {
    params = request["p"];
  }

  if (std::strcmp(method, "sys.version") == 0) {
    JsonDocument response;
    response["id"] = id;
    response["result"]["version"] = vibe::kFirmwareVersion;
    String jsonOut;
    serializeJson(response, jsonOut);
    sendFramedJson(jsonOut, true);
    Serial.printf("RPC response: %s\n", method);
    return;
  }

  if (std::strcmp(method, "device.status") == 0) {
    JsonDocument response;
    response["id"] = id;
    JsonObject result = response["result"].to<JsonObject>();
    result["version"] = vibe::kFirmwareVersion;
    result["profile_index"] = 0;
    result["layer_index"] = 1;
    result["battery"] = g_batteryLevel;
    result["is_charging"] = g_isCharging;
    String jsonOut;
    serializeJson(response, jsonOut);
    sendFramedJson(jsonOut, true);
    Serial.printf("RPC response: %s\n", method);
    return;
  }

  if (std::strcmp(method, "v.oai.thstatus") == 0 ||
      std::strcmp(method, "v.oai.rgbcfg") == 0 ||
      std::strcmp(method, "host.focused_app") == 0 ||
      std::strcmp(method, "lights.preview") == 0) {
    JsonDocument response;
    response["id"] = id;
    response["result"]["ok"] = true;
    String jsonOut;
    serializeJson(response, jsonOut);
    sendFramedJson(jsonOut, true);
    Serial.printf("RPC response: %s\n", method);
    return;
  }

  // Unknown method: return JSON-RPC Method Not Found error so the host does
  // not wait forever for a response.
  JsonDocument response;
  response["id"] = id;
  JsonObject errorObj = response["error"].to<JsonObject>();
  errorObj["code"] = -32601;
  errorObj["message"] = "Method not found";
  String jsonOut;
  serializeJson(response, jsonOut);
  sendFramedJson(jsonOut, true);
  Serial.printf("RPC unknown method: %s\n", method);
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
  // Use USB Implementer's Forum source (0x02) so the host sees the
  // well-known Codex Micro VID:PID pair (0x303A:0x8360).
  g_hid->setPnp(0x02, vibe::kVendorId, vibe::kProductId, vibe::kProductVersion);
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
  g_vendorOutput = g_hid->getOutputReport(vibe::kVendorReportId);
  g_hid->getFeatureReport(vibe::kVendorReportId);

  const std::uint8_t keyboardIdle[8] = {};
  const std::uint8_t consumerIdle[2] = {};
  const std::uint8_t pointerIdle[5] = {};
  const std::uint8_t vendorIdle[vibe::kBleReportLength] = {};
  keyboardInput->setValue(keyboardIdle, sizeof(keyboardIdle));
  consumerInput->setValue(consumerIdle, sizeof(consumerIdle));
  pointerInput->setValue(pointerIdle, sizeof(pointerIdle));
  g_vendorInput->setValue(vendorIdle, sizeof(vendorIdle));
  g_vendorOutput->setCallbacks(&g_rpcCallbacks);

  g_hid->setBatteryLevel(g_batteryLevel);

  if (!g_server->start()) {
    Serial.println("Failed to start BLE GATT server");
    return;
  }

  auto* advertising = NimBLEDevice::getAdvertising();
  advertising->setName(vibe::kDeviceName);
  advertising->setAppearance(GENERIC_HID);
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

  g_rpcQueue = xQueueCreate(6, sizeof(char*));
  if (g_rpcQueue == nullptr) {
    Serial.println("Failed to create RPC queue");
    while (true) {
      delay(1000);
    }
  }

  irrecv.enableIRIn();
  initializeBle();
}

void loop() {
  char* message = nullptr;
  while (xQueueReceive(g_rpcQueue, &message, 0) == pdTRUE) {
    processRpc(message);
    std::free(message);
    message = nullptr;
  }

  if (irrecv.decode(&results)) {
    // Holding an IR remote button emits a repeat code (all Fs in 32 or 64 bit).
    // Treat it as a repeat of the last valid code, but remember whether the
    // current frame is a repeat so the mic toggle can ignore repeated frames.
    const bool isRepeatCode =
        results.value == 0xFFFFFFFF || results.value == 0xFFFFFFFFFFFFFFFF;
    if (isRepeatCode) {
      if (g_lastIrCode == 0) {
        irrecv.resume();
        return;
      }
      results.value = g_lastIrCode;
    } else {
      g_lastIrCode = results.value;
    }

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
        case IR_CODE_OK:      triggerAction(1); break;
        case IR_CODE_NG:      triggerAction(2); break;
        case IR_CODE_PLAN:    triggerAction(3); break;
        case IR_CODE_AI:      triggerAction(4); break;

        case IR_CODE_MIC:     triggerMic(isRepeatCode); break;

        case IR_CODE_RIGHT:   triggerJoystick(0.00f); break;
        case IR_CODE_DOWN:    triggerJoystick(0.25f); break;
        case IR_CODE_LEFT:    triggerJoystick(0.50f); break;
        case IR_CODE_UP:      triggerJoystick(0.75f); break;

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
