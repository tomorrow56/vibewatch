/*
 * Vibe Watch for M5Dial
 *
 * A port of the Vibe Watch experience to the M5Dial board. It replaces the
 * M5Stack StopWatch touch panel and two physical buttons with the M5Dial's
 * 1.28" round touch screen, rotary encoder, and single push button, while
 * keeping the same NimBLE HID protocol as the original firmware.
 *
 * Dependencies:
 *   - m5stack/M5Dial
 *   - m5stack/M5Unified (pulled in by M5Dial)
 *   - h2zero/NimBLE-Arduino
 *   - bblanchon/ArduinoJson
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <M5Dial.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <Preferences.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

// -----------------------------------------------------------------------------
// Vibe Watch HID constants and report map
// -----------------------------------------------------------------------------

namespace vibe {

constexpr char kDeviceName[] = "VibeDial";
constexpr char kManufacturer[] = "VibeWatch";
constexpr char kModelNumber[] = "VibeWatch";
constexpr char kFirmwareVersion[] = "v1.0";

constexpr std::uint16_t kVendorId = 0x303A;
constexpr std::uint16_t kProductId = 0x8360;
constexpr std::uint16_t kProductVersion = 0x0001;

constexpr std::uint8_t kVendorReportId = 6;
constexpr std::size_t kBleReportLength = 63;
constexpr std::size_t kRpcChunkLength = 61;
constexpr std::size_t kRpcBufferLength = 2048;
constexpr std::uint8_t kChannelJsonRpc = 2;

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
// UI geometry, timing, and persistent-setting keys
// -----------------------------------------------------------------------------

constexpr int kScreenSize = 240;
constexpr int kScreenCenter = kScreenSize / 2;
constexpr int kAgentCount = 6;
constexpr int kActionCount = 5;
constexpr int kOkAction = 1;
constexpr int kNgAction = 2;
constexpr int kAgentOrbitRadius = 72;
constexpr int kAgentButtonRadius = 18;
constexpr int kMicButtonRadius = 26;
constexpr int kLayerToggleRadius = 15;
constexpr int kEncoderDivisor = 2;
constexpr std::uint32_t kShortPressMs = 250;
constexpr std::uint32_t kLayerToggleMs = 600;
constexpr std::uint32_t kUiPeriodMs = 33;
constexpr std::uint32_t kSplashHoldMs = 800;
constexpr std::uint32_t kBatteryUpdatePeriodMs = 30000;
constexpr char kPreferencesNamespace[] = "vibe-dial";
constexpr char kDeviceSlotKey[] = "device-slot";

struct AgentState {
    std::uint32_t color = 0;
    float brightness = 0.0f;
    int effect = 0;
    float speed = 0.0f;
};

struct AmbientState {
    std::uint32_t color = 0x304FFE;
    float brightness = 0.25f;
    int effect = 0;
    float speed = 0.4f;
};

enum class HitType {
    None,
    Agent,
    Action,
    Mic,
    LayerToggle,
};

struct HitResult {
    HitType type;
    int index;
    HitResult(HitType t = HitType::None, int i = -1) : type(t), index(i) {}
};

// -----------------------------------------------------------------------------
// Runtime state
// -----------------------------------------------------------------------------

std::array<AgentState, kAgentCount> g_agents;
AmbientState g_ambient;
String g_focusedApp;

NimBLEServer* g_server = nullptr;
NimBLEHIDDevice* g_hid = nullptr;
NimBLECharacteristic* g_vendorInput = nullptr;
NimBLECharacteristic* g_vendorOutput = nullptr;
QueueHandle_t g_rpcQueue = nullptr;
volatile bool g_connected = false;
String g_rxBuffer;

std::array<int, kAgentCount> g_agentX{};
std::array<int, kAgentCount> g_agentY{};
std::array<int, kActionCount> g_actionX{};
std::array<int, kActionCount> g_actionY{};

int g_selected = 0;
bool g_actionLayer = false;
bool g_planModeEnabled = false;
bool g_uiDirty = true;
std::uint32_t g_lastUiDraw = 0;

int32_t g_encoderAccum = 0;
std::uint32_t g_buttonPressedAt = 0;
bool g_buttonIsPressed = false;
bool g_layerToggleTriggered = false;
bool g_micActive = false;

int g_deviceSlot = 1;
char g_deviceName[24] = {};
std::uint8_t g_batteryLevel = 100;
bool g_isCharging = false;
std::uint32_t g_lastBatteryUpdate = 0;

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

float clamp01(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

std::uint16_t scaledColor(std::uint32_t packed, float brightness) {
    const float scale = clamp01(brightness);
    const auto r = static_cast<std::uint8_t>(((packed >> 16) & 0xFF) * scale);
    const auto g = static_cast<std::uint8_t>(((packed >> 8) & 0xFF) * scale);
    const auto b = static_cast<std::uint8_t>((packed & 0xFF) * scale);
    return M5Dial.Display.color565(r, g, b);
}

void playSe(float frequency = 880.0f, std::uint32_t durationMs = 35) {
    M5Dial.Speaker.tone(static_cast<std::uint16_t>(frequency), durationMs);
}

void loadPreferences() {
    Preferences preferences;
    preferences.begin(kPreferencesNamespace, true);
    g_deviceSlot = preferences.getUChar(kDeviceSlotKey, 1);
    preferences.end();
    if (g_deviceSlot < 1 || g_deviceSlot > 3) {
        g_deviceSlot = 1;
    }
    std::snprintf(g_deviceName, sizeof(g_deviceName), "%s #%d", vibe::kDeviceName, g_deviceSlot);
}

void saveDeviceSlot(int slot) {
    g_deviceSlot = slot;
    Preferences preferences;
    preferences.begin(kPreferencesNamespace, false);
    preferences.putUChar(kDeviceSlotKey, static_cast<std::uint8_t>(slot));
    preferences.end();
    std::snprintf(g_deviceName, sizeof(g_deviceName), "%s #%d", vibe::kDeviceName, g_deviceSlot);
}

// -----------------------------------------------------------------------------
// Host communication
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
            Serial.println("BLE notify failed");
            return;
        }
        offset += chunk;
        if (offset < total) {
            delay(8);
        }
    }
}

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
        g_uiDirty = true;
        Serial.printf("Plan mode: %s\n", g_planModeEnabled ? "on" : "off");
    }
    sendActionEvent(index == 4 ? 12 : 6 + index, pressed);
}

void triggerAgent(int index) {
    if (index < 0 || index >= kAgentCount) {
        return;
    }
    g_selected = index;
    sendAgentEvent(index, true);
    delay(50);
    sendAgentEvent(index, false);
    playSe(820.0f + index * 55.0f, 33);
}

void triggerAction(int index) {
    if (index < 0 || index >= kActionCount) {
        return;
    }
    g_selected = index;
    sendOuterActionEvent(index, true);
    delay(50);
    sendOuterActionEvent(index, false);

    if (index == kOkAction) {
        playSe(659.25f, 34);
        delay(42);
        playSe(987.77f, 86);
    } else if (index == kNgAction) {
        playSe(392.00f, 42);
        delay(50);
        playSe(293.66f, 105);
    } else {
        playSe(900.0f + index * 75.0f, 40);
    }
}

void triggerMic(bool pressed) {
    g_micActive = pressed;
    sendMicEvent(pressed);
    playSe(pressed ? 1040.0f : 620.0f, 45);
}

// -----------------------------------------------------------------------------
// Host -> device RPC
// -----------------------------------------------------------------------------

void applyAgentStatus(JsonVariantConst params) {
    if (!params.is<JsonArrayConst>()) {
        return;
    }
    bool anyAgentChanged = false;
    for (JsonObjectConst item : params.as<JsonArrayConst>()) {
        const int id = item["id"] | -1;
        if (id < 0 || id >= kAgentCount) {
            continue;
        }
        AgentState next;
        next.color = item["c"] | 0U;
        next.brightness = item["b"] | 0.0f;
        next.effect = item["e"] | 0;
        next.speed = item["s"] | 0.0f;
        auto& state = g_agents[id];
        const bool changed = state.color != next.color ||
                             std::abs(state.brightness - next.brightness) > 0.001f ||
                             state.effect != next.effect ||
                             std::abs(state.speed - next.speed) > 0.001f;
        anyAgentChanged = anyAgentChanged || changed;
        state = next;
    }
    if (anyAgentChanged) {
        g_uiDirty = true;
    }
}

void applyAmbientStatus(JsonVariantConst params) {
    JsonObjectConst ambient = params["ambient"].as<JsonObjectConst>();
    if (ambient.isNull()) {
        return;
    }
    g_ambient.color = ambient["c"] | 0U;
    g_ambient.brightness = ambient["b"] | 0.0f;
    g_ambient.effect = ambient["e"] | 0;
    g_ambient.speed = ambient["s"] | 0.0f;
    g_uiDirty = true;
}

void applyFocusedApp(JsonVariantConst params) {
    const char* appName = params["appName"] | "";
    g_focusedApp = appName;
    if (g_focusedApp.length() > 18) {
        g_focusedApp = g_focusedApp.substring(0, 17) + "…";
    }
    g_uiDirty = true;
}

void sendRpcResponse(const char* method, int id) {
    JsonDocument response;
    response["id"] = id;
    response["method"] = method;

    if (std::strcmp(method, "device.status") == 0) {
        JsonObject result = response["result"].to<JsonObject>();
        result["version"] = vibe::kFirmwareVersion;
        result["profile_index"] = 0;
        result["layer_index"] = 1;
        result["battery"] = g_batteryLevel;
        result["is_charging"] = g_isCharging;
    } else if (std::strcmp(method, "sys.version") == 0) {
        response["result"]["version"] = vibe::kFirmwareVersion;
    } else {
        response["result"]["ok"] = 1;
    }

    String json;
    serializeJson(response, json);
    sendFramedJson(json, true);
    Serial.printf("RPC response: %s id=%d\n", method, id);
}

void processRpc(const char* json) {
    JsonDocument request;
    const DeserializationError error = deserializeJson(request, json);
    if (error) {
        Serial.printf("RPC parse failed: %s\n", error.c_str());
        return;
    }

    const char* method = request["method"] | request["m"] | "";
    int id = request["id"] | request["i"] | -1;
    JsonVariantConst params = request["params"];
    if (params.isNull()) {
        params = request["p"];
    }

    if (std::strcmp(method, "v.oai.thstatus") == 0) {
        applyAgentStatus(params);
    } else if (std::strcmp(method, "v.oai.rgbcfg") == 0) {
        applyAmbientStatus(params);
    } else if (std::strcmp(method, "host.focused_app") == 0) {
        applyFocusedApp(params);
    }

    if (id >= 0 && method[0] != '\0') {
        sendRpcResponse(method, id);
    }
}

// -----------------------------------------------------------------------------
// BLE callbacks and setup
// -----------------------------------------------------------------------------

class RpcOutputCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo&) override {
        const NimBLEAttValue value = characteristic->getValue();
        const auto* data = value.data();
        const std::size_t length = value.size();
        if (data == nullptr || length < 2 || data[0] != vibe::kChannelJsonRpc) {
            return;
        }

        const std::size_t chunkLength = data[1];
        if (chunkLength > vibe::kRpcChunkLength || chunkLength > length - 2) {
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
            g_rxBuffer += static_cast<char>(data[i + 2]);
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

class HidServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* server, NimBLEConnInfo& connection) override {
        g_connected = true;
        g_uiDirty = true;
        server->updateConnParams(connection.getConnHandle(), 12, 24, 0, 180);
        Serial.printf("BLE connected: %s\n", connection.getAddress().toString().c_str());
    }

    void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int reason) override {
        g_connected = false;
        g_uiDirty = true;
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

RpcOutputCallbacks g_rpcCallbacks;
HidServerCallbacks g_serverCallbacks;

void addDeviceInfoCharacteristic(std::uint16_t uuid, const char* value) {
    auto* characteristic =
        g_hid->getDeviceInfoService()->createCharacteristic(uuid, NIMBLE_PROPERTY::READ);
    characteristic->setValue(value);
}

void initializeBle() {
    NimBLEDevice::init(g_deviceName);
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

    if (!g_server->start()) {
        Serial.println("Failed to start BLE GATT server");
        return;
    }

    auto* advertising = NimBLEDevice::getAdvertising();
    advertising->setName(g_deviceName);
    advertising->setAppearance(HID_KEYBOARD);
    advertising->addServiceUUID(g_hid->getHidService()->getUUID());
    advertising->enableScanResponse(true);
    advertising->start();
    Serial.printf("BLE HID advertising started as %s\n", g_deviceName);
}

// -----------------------------------------------------------------------------
// Circular layout
// -----------------------------------------------------------------------------

void initializePositions() {
    for (int i = 0; i < kAgentCount; ++i) {
        const float angle = -M_PI_2 + i * 2.0f * M_PI / kAgentCount;
        g_agentX[i] = static_cast<int>(kScreenCenter + kAgentOrbitRadius * std::cos(angle));
        g_agentY[i] = static_cast<int>(kScreenCenter + kAgentOrbitRadius * std::sin(angle));
    }
    for (int i = 0; i < kActionCount; ++i) {
        const float angle = -M_PI_2 + i * 2.0f * M_PI / kActionCount;
        g_actionX[i] = static_cast<int>(kScreenCenter + kAgentOrbitRadius * std::cos(angle));
        g_actionY[i] = static_cast<int>(kScreenCenter + kAgentOrbitRadius * std::sin(angle));
    }
}

// -----------------------------------------------------------------------------
// Rendering
// -----------------------------------------------------------------------------

const char* actionLabel(int index) {
    static const char* kLabels[kActionCount] = {"FST", "NG", "OK", "PLN", "AI"};
    return kLabels[index];
}

void drawConnection(int fromX, int fromY, int toX, int toY, std::uint16_t color) {
    M5Dial.Display.drawLine(fromX, fromY, toX, toY, color);
}

void drawRingItems() {
    const int count = g_actionLayer ? kActionCount : kAgentCount;
    const int* xs = g_actionLayer ? g_actionX.data() : g_agentX.data();
    const int* ys = g_actionLayer ? g_actionY.data() : g_agentY.data();

    for (int i = 0; i < count; ++i) {
        const int x = xs[i];
        const int y = ys[i];
        const bool selected = (i == g_selected);
        std::uint16_t color;

        if (g_actionLayer) {
            const std::uint32_t kActionColors[kActionCount] = {
                0x00C853, 0xFF3D00, 0x2979FF, 0xAA00FF, 0xFFD600,
            };
            color = M5Dial.Display.color565((kActionColors[i] >> 16) & 0xFF,
                                            (kActionColors[i] >> 8) & 0xFF,
                                            kActionColors[i] & 0xFF);
        } else {
            color = scaledColor(g_agents[i].color, g_agents[i].brightness);
            if (g_agents[i].color == 0) {
                color = M5Dial.Display.color565(30, 30, 30);
            }
        }

        M5Dial.Display.fillCircle(x, y, kAgentButtonRadius, color);
        if (selected) {
            M5Dial.Display.drawCircle(x, y, kAgentButtonRadius + 2, 0xFFFFFF);
            M5Dial.Display.drawCircle(x, y, kAgentButtonRadius + 3, 0xFFFFFF);
        } else {
            M5Dial.Display.drawCircle(x, y, kAgentButtonRadius, 0x7BEF);
        }

        M5Dial.Display.setTextColor(WHITE);
        M5Dial.Display.setTextDatum(middle_center);
        M5Dial.Display.setTextSize(1);
        if (g_actionLayer) {
            M5Dial.Display.drawString(actionLabel(i), x, y);
        }
    }
}

void drawCenterMic() {
    const std::uint16_t color = g_micActive ? 0xF800 : 0x304FFE;
    M5Dial.Display.fillCircle(kScreenCenter, kScreenCenter, kMicButtonRadius, color);
    M5Dial.Display.setTextColor(WHITE);
    M5Dial.Display.setTextDatum(middle_center);
    M5Dial.Display.setTextSize(1);
    M5Dial.Display.drawString("MIC", kScreenCenter, kScreenCenter);
}

void drawLayerToggle() {
    const int x = kScreenCenter;
    const int y = kScreenSize - 28;
    const std::uint16_t color = g_actionLayer ? 0xAA00FF : 0x2979FF;
    M5Dial.Display.fillCircle(x, y, kLayerToggleRadius, color);
    M5Dial.Display.setTextColor(WHITE);
    M5Dial.Display.setTextDatum(middle_center);
    M5Dial.Display.setTextSize(1);
    M5Dial.Display.drawString(g_actionLayer ? "AGENT" : "ACT", x, y);
}

void drawStatusBar() {
    M5Dial.Display.setTextColor(0xC618);
    M5Dial.Display.setTextDatum(top_center);
    M5Dial.Display.setTextSize(1);

    const char* mode = g_actionLayer ? "ACTION" : "AGENT";
    const int y = 4;
    M5Dial.Display.drawString(mode, kScreenCenter, y);

    if (!g_connected) {
        M5Dial.Display.setTextColor(0xF800);
        M5Dial.Display.drawString("BLE", kScreenCenter, 18);
    } else if (!g_focusedApp.isEmpty()) {
        M5Dial.Display.setTextColor(0x07E0);
        M5Dial.Display.drawString(g_focusedApp.c_str(), kScreenCenter, 18);
    }
}

void renderUi() {
    M5Dial.Display.clear();
    drawRingItems();
    drawCenterMic();
    drawLayerToggle();
    drawStatusBar();
}

// -----------------------------------------------------------------------------
// Input
// -----------------------------------------------------------------------------

HitResult hitTest(int x, int y) {
    // Layer toggle at the bottom.
    const int toggleDx = x - kScreenCenter;
    const int toggleDy = y - (kScreenSize - 28);
    if (toggleDx * toggleDx + toggleDy * toggleDy <= kLayerToggleRadius * kLayerToggleRadius) {
        return HitResult(HitType::LayerToggle, 0);
    }

    // Center microphone.
    const int centerDx = x - kScreenCenter;
    const int centerDy = y - kScreenCenter;
    if (centerDx * centerDx + centerDy * centerDy <= kMicButtonRadius * kMicButtonRadius) {
        return HitResult(HitType::Mic, 0);
    }

    // Outer ring items.
    const int count = g_actionLayer ? kActionCount : kAgentCount;
    const int* xs = g_actionLayer ? g_actionX.data() : g_agentX.data();
    const int* ys = g_actionLayer ? g_actionY.data() : g_agentY.data();
    for (int i = 0; i < count; ++i) {
        const int dx = x - xs[i];
        const int dy = y - ys[i];
        if (dx * dx + dy * dy <= kAgentButtonRadius * kAgentButtonRadius) {
            return HitResult(g_actionLayer ? HitType::Action : HitType::Agent, i);
        }
    }

    return HitResult(HitType::None, -1);
}

void toggleActionLayer() {
    g_actionLayer = !g_actionLayer;
    g_selected = 0;
    g_encoderAccum = 0;
    M5Dial.Encoder.write(0);
    g_uiDirty = true;
    playSe(g_actionLayer ? 1120.0f : 680.0f, 48);
}

void handleEncoder() {
    const int32_t delta = M5Dial.Encoder.readAndReset();
    if (delta == 0) {
        return;
    }
    g_encoderAccum += delta;

    const int count = g_actionLayer ? kActionCount : kAgentCount;
    if (g_encoderAccum >= kEncoderDivisor) {
        g_selected = (g_selected + 1) % count;
        g_encoderAccum -= kEncoderDivisor;
        g_uiDirty = true;
        playSe(880.0f, 20);
    } else if (g_encoderAccum <= -kEncoderDivisor) {
        g_selected = (g_selected + count - 1) % count;
        g_encoderAccum += kEncoderDivisor;
        g_uiDirty = true;
        playSe(830.0f, 20);
    }
}

void handleButton() {
    if (M5Dial.BtnA.wasPressed()) {
        g_buttonIsPressed = true;
        g_buttonPressedAt = millis();
        g_layerToggleTriggered = false;
    }

    if (g_buttonIsPressed && M5Dial.BtnA.pressedFor(kLayerToggleMs) && !g_layerToggleTriggered) {
        g_layerToggleTriggered = true;
        toggleActionLayer();
    }

    if (M5Dial.BtnA.wasReleased()) {
        if (!g_layerToggleTriggered) {
            const auto duration = millis() - g_buttonPressedAt;
            if (duration < kShortPressMs) {
                if (g_actionLayer) {
                    triggerAction(g_selected);
                } else {
                    triggerAgent(g_selected);
                }
            }
        }
        g_buttonIsPressed = false;
        g_layerToggleTriggered = false;
    }
}

void handleTouch() {
    const auto touch = M5Dial.Touch.getDetail();
    static HitResult s_activeHit;

    if (touch.wasPressed()) {
        s_activeHit = hitTest(touch.x, touch.y);
        switch (s_activeHit.type) {
            case HitType::Agent:
                triggerAgent(s_activeHit.index);
                break;
            case HitType::Action:
                triggerAction(s_activeHit.index);
                break;
            case HitType::Mic:
                triggerMic(true);
                break;
            case HitType::LayerToggle:
                toggleActionLayer();
                break;
            default:
                break;
        }
        g_uiDirty = true;
    }

    if (touch.wasReleased()) {
        if (s_activeHit.type == HitType::Mic) {
            triggerMic(false);
        }
        s_activeHit = HitResult(HitType::None, -1);
        g_uiDirty = true;
    }
}

// -----------------------------------------------------------------------------
// Battery and startup
// -----------------------------------------------------------------------------

void updateBattery(bool notify) {
    // M5Dial power management is available through M5Dial.Power when running
    // from battery. If battery monitoring is not supported, report 100%.
    auto& power = M5Dial.Power;
    g_isCharging = (power.isCharging() == m5::Power_Class::is_charging);

    const int level = power.getBatteryLevel();
    if (level >= 0 && level <= 100) {
        g_batteryLevel = static_cast<std::uint8_t>(level);
    } else {
        g_batteryLevel = 100;
    }

    if (g_hid != nullptr) {
        g_hid->setBatteryLevel(g_batteryLevel, notify && g_connected);
    }
}

void showSplashScreen() {
    M5Dial.Display.clear();
    M5Dial.Display.setTextColor(WHITE);
    M5Dial.Display.setTextDatum(middle_center);
    M5Dial.Display.setTextSize(2);
    M5Dial.Display.drawString("VibeDial", kScreenCenter, kScreenCenter - 20);
    M5Dial.Display.setTextSize(1);
    M5Dial.Display.drawString(vibe::kFirmwareVersion, kScreenCenter, kScreenCenter + 20);

    M5Dial.Speaker.tone(880, 50);
    delay(120);
    M5Dial.Speaker.tone(1320, 80);
    delay(kSplashHoldMs);
}

// -----------------------------------------------------------------------------
// Arduino lifecycle
// -----------------------------------------------------------------------------

void setup() {
    Serial.begin(115200);

    auto cfg = M5.config();
    M5Dial.begin(cfg, true, false);  // enable encoder, disable RFID
    M5Dial.Display.setBrightness(80);
    M5Dial.Display.setRotation(0);

    loadPreferences();
    updateBattery(false);
    showSplashScreen();

    initializePositions();

    g_rpcQueue = xQueueCreate(6, sizeof(char*));
    if (g_rpcQueue == nullptr) {
        Serial.println("Failed to create RPC queue");
        while (true) {
            delay(1000);
        }
    }

    initializeBle();
    g_uiDirty = true;
}

void loop() {
    M5Dial.update();
    handleEncoder();
    handleButton();
    handleTouch();

    char* message = nullptr;
    while (xQueueReceive(g_rpcQueue, &message, 0) == pdTRUE) {
        processRpc(message);
        std::free(message);
        message = nullptr;
    }

    const std::uint32_t now = millis();
    if (now - g_lastBatteryUpdate >= kBatteryUpdatePeriodMs) {
        updateBattery(true);
        g_lastBatteryUpdate = now;
    }

    if (g_uiDirty && now - g_lastUiDraw >= kUiPeriodMs) {
        renderUi();
        g_lastUiDraw = now;
        g_uiDirty = false;
    }

    delay(5);
}
