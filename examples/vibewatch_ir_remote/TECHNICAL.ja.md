# Bluetooth HID 送信処理の技術詳細

このドキュメントでは、`vibewatch_ir_remote.ino` が IR リモコンの入力を
Bluetooth Low Energy（BLE）経由でホストへ送信する仕組みを、プロトコルから
コード実装まで解説します。

## 1. 全体構成

このスケッチは以下の 2 つの層でできています。

- **入力層**: `IRremoteESP8266` による赤外線受信
- **出力層**: `NimBLE-Arduino` による BLE HID（Human Interface Device）

M5Stack StopWatch 版の Vibe Watch と同じホストプロトコルを使うため、
ホスト側ソフトウェアは変更しなくても動作します。ただし、このスケッチでは
ChatGPT Desktop の Codex Micro 機能と接続するため、BLE のデバイス名、
メーカー名、PnP ID を Codex Micro と同一に見せるように設定しています。
M5Unified や円形ディスプレイ、振動モーターなどの HW 依存を外し、
ESP32 + IR 受信モジュールだけで動くように簡略化されています。

## 2. BLE HID over GATT

BLE HID には **HID over GATT Profile（HOGP）** が使われます。
ESP32 は HID デバイスとして振る舞い、ホスト PC やスマートフォンに接続します。

従来の ESP32 では Bluedroid スタックが使われますが、このスケッチでは
**NimBLE** スタックを採用しています。NimBLE はメモリ消費が少なく、
HID デバイスとしての動作も安定しているため、Vibe Watch 本体でも採用されています。

## 3. HID Report Map

HID デバイスは、どんな種類のデータをやり取りするかを **Report Map（HID
Descriptor）** でホストに告げます。Vibe Watch では 4 種類のレポートを
定義しています。

| Report ID | 種別 | 用途 |
|---|---|---|
| 1 | Keyboard | 通常キーボード入力（修飾キー含む） |
| 2 | Consumer | 消費者制御（音量やメディアキーなど） |
| 3 | Relative Pointer | マウスカーソル、ホイール、ボタン |
| 6 | Vendor Defined | Vibe Watch 独自の JSON-RPC 制御面 |

スケッチ内では `vibe::kReportMap` としてコピーして使用しています。
重要なのは **Report ID 6** の **Vendor レポート** で、これを使って
エージェント選択やアクションイベントをホストへ通知します。
このレポート記述子は Codex Micro の BLE 記述子と互換性があり、
ChatGPT Desktop が Codex Micro として認識できるようになっています。

## 4. BLE 接続・Advertising・セキュリティ

### 初期化

`initializeBle()` で NimBLE スタックと HID サービスを構築します。

```cpp
NimBLEDevice::init(vibe::kDeviceName);
NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
NimBLEDevice::setSecurityAuth(true, false, true);

g_server = NimBLEDevice::createServer();
g_hid = new NimBLEHIDDevice(g_server);
```

- `init()` で指定した名前は BLE スタック内部で使用されます。
- `setSecurityAuth(true, false, true)` は「bonding 有効、MITM 不要、
  暗号化要」の設定です。macOS などのホストとペアリングする際に
  自動でキーが保存されます。

### Advertising

```cpp
auto* advertising = NimBLEDevice::getAdvertising();
advertising->setName(vibe::kDeviceName);
advertising->setAppearance(GENERIC_HID);
advertising->addServiceUUID(g_hid->getHidService()->getUUID());
advertising->enableScanResponse(true);
advertising->start();
```

ホストはこの Advertising パケットを見て HID デバイスを認識します。
`vibe::kDeviceName` は `Codex Micro` に設定されているため、
ChatGPT Desktop はこれを Codex Micro コントローラーとして検出します。

### Advertising Name 長の制限

BLE のレガシー Advertising パケットは **最大 31 バイト** です。
flags、appearance、service UUID などの固定部分でおよそ 13 バイトを
使うため、デバイス名は **18 文字以内** に収める必要があります。

このスケッチでは `Codex Micro`（11 文字）を使っており、パケット
オーバーフローを避けています。名前を変更する場合は必ず短く
（推奨 18 文字以下）にしてください。

### PnP ID

```cpp
g_hid->setManufacturer(vibe::kManufacturer);  // "Work Louder"
g_hid->setPnp(0x02, vibe::kVendorId, vibe::kProductId, vibe::kProductVersion);
// vendorId = 0x303A, productId = 0x8360, productVersion = 0x0101
```

Codex Micro は USB Implementer's Forum のベンダー ID ソース（`0x02`）と、
Espressif の VID `0x303A`、Codex Micro の PID `0x8360` を使って識別されます。
これらを一致させることで、ChatGPT Desktop は正規の Codex Micro として
ハンドシェイクを開始します。

## 5. Vendor Report と JSON-RPC

Vibe Watch 独自の制御は **Report ID 6** の 63 バイト Vendor レポートを
使います。フォーマットは以下の通りです。

| バイト | 内容 |
|---|---|
| 0 | チャネル番号（`kChannelJsonRpc = 2`） |
| 1 | ペイロード長（2 バイト目からの有効な JSON 長） |
| 2..62 | UTF-8 JSON 文字列 |

1 つの JSON-RPC メッセージは 61 バイトを超えることができますが、
このスケッチで送るキーイベントは 1 レポートに収まる短い JSON です。

### 送信例

Agent 1 ボタンが押された場合、次のような JSON を送信します。

```text
{"m":"v.oai.hid","p":{"k":"AG00","act":1}}
```

離された場合は `act` が `0` になります。

```text
{"m":"v.oai.hid","p":{"k":"AG00","act":0}}
```

### ホストからのリクエスト

Codex Micro のハンドシェイクでは、ホストがデバイスに対して
`sys.version` や `device.status` などの JSON-RPC リクエストを送ります。
そのため、Vendor レポートの **Output** 側にもコールバックを登録し、
ホストからの書き込みを受け取って応答します。

```cpp
g_vendorOutput = g_hid->getOutputReport(vibe::kVendorReportId);
g_vendorOutput->setCallbacks(&g_rpcCallbacks);
```

`RpcOutputCallbacks::onWrite()` では、チャネル番号（2）と長さバイトを
確認し、複数のフレームに分割された JSON を再構成して `processRpc()` に
渡します。macOS は Output レポートの先頭に Report ID（6）を付加する
ことがあるため、Report ID 付き・なしの両方を受け入れます。

応答する主なメソッドは以下の通りです。

| メソッド | 応答内容 |
|---|---|
| `sys.version` | ファームウェアバージョン |
| `device.status` | バージョン、プロファイル/レイヤー、バッテリー、充電状態 |
| `v.oai.thstatus` | `{"ok":true}` |
| `v.oai.rgbcfg` | `{"ok":true}` |
| `host.focused_app` | `{"ok":true}` |
| `lights.preview` | `{"ok":true}` |

これらの応答がないと、ChatGPT Desktop は「検出したが接続できない」状態で
止まってしまいます。

## 6. イベント送信関数

### `sendKeyEvent`

すべての HID イベントの土台となる関数です。
63 バイトの Vendor レポートを組み立てて `g_vendorInput->notify()` で
ホストへ通知します。

```cpp
std::uint8_t report[vibe::kBleReportLength] = {};
report[0] = vibe::kChannelJsonRpc;
report[1] = static_cast<std::uint8_t>(written);
std::snprintf(
    reinterpret_cast<char*>(&report[2]), vibe::kRpcChunkLength,
    "{\"m\":\"v.oai.hid\",\"p\":{\"k\":\"%s\",\"act\":%u}}\r\n",
    key, pressed ? 1U : 0U);
g_vendorInput->setValue(report, sizeof(report));
g_vendorInput->notify();
```

`g_vendorInput` は `NimBLEHIDDevice::getInputReport(6)` で取得した
Input キャラクタリスティックです。`notify()` を呼ぶと接続中の
ホストへ即座に値が届きます。

### `sendAgentEvent`

```cpp
void sendAgentEvent(int index, bool pressed) {
  char key[5];
  std::snprintf(key, sizeof(key), "AG%02d", index);
  sendKeyEvent(key, pressed);
}
```

`AG00` から `AG05` の 6 エージェントを選択・通知します。

### `sendActionEvent`

```cpp
void sendActionEvent(int index, bool pressed) {
  char key[6];
  std::snprintf(key, sizeof(key), "ACT%02d", index);
  sendKeyEvent(key, pressed);
}
```

`ACT06` から `ACT12` のアクションイベントを送信します。

### `sendOuterActionEvent`

外側 5 ボタン（FAST / NG / OK / PLAN / AI）を Report ID 6 の
アクション ID に変換します。

| 外側インデックス | 送信キー | 備考 |
|---|---|---|
| 0 | ACT06 | FAST |
| 1 | ACT07 | OK（コード内では `IR_CODE_OK`） |
| 2 | ACT08 | NG（コード内では `IR_CODE_NG`） |
| 3 | ACT09 | PLAN、ローカルフラグもトグル |
| 4 | ACT12 | AI |

PLAN だけ特別扱いで、ローカル変数 `g_planModeEnabled` を反転します。
これはホストへ状態を送る前にデバイス側でもモードを覚えておくためです。

### `sendMicEvent` と `triggerMic`

```cpp
void sendMicEvent(bool pressed) {
  sendActionEvent(10, pressed);
}

void triggerMic(bool fromRepeat) {
  if (fromRepeat) return;
  g_micRecording = !g_micRecording;
  sendMicEvent(g_micRecording);
}
```

IR リモコンはボタンを離したことを検出できないため、マイクは
トグルとして動作します。`ACT10` DOWN で録音を開始し、次の押下で
`ACT10` UP で録音を停止します。リピートフレームは無視するので、
長押ししても状態が連続して反転することはありません。

### `sendJoystickEvent`

アナログスティックの方向は `v.oai.rad` メソッドでホストに通知します。

```cpp
void sendJoystickEvent(float angle, float distance) {
  std::snprintf(
      reinterpret_cast<char*>(&report[2]), vibe::kRpcChunkLength,
      "{\"method\":\"v.oai.rad\",\"params\":{\"a\":%.2f,\"d\":%.2f}}\r\n",
      angle, distance);
}
```

角度は Codex Micro プロトコルに合わせて正規化されています。

| 方向 | 角度 |
|---|---|
| 右 | 0.00 |
| 下 | 0.25 |
| 左 | 0.50 |
| 上 | 0.75 |

押下時に `distance = 1.0`、リリース時に `distance = 0.0` を送ります。

## 7. IR 入力から BLE 送信までの流れ

1. `irrecv.decode(&results)` が IR 受信を検出
2. `results.value` が `0xFFFFFFFF` または `0xFFFFFFFFFFFFFFFF` ならリピート
   コードとして扱い、直前の有効コードに置き換える
3. `results.value` に応じて `triggerAgent()` / `triggerAction()` /
   `triggerMic()` / `triggerJoystick()` を呼び出し
4. 各 `triggerXxx()` は「press → 50ms または 100ms 待機 → release」を送信
5. 実際の BLE 送信は `sendKeyEvent()` / `sendJoystickEvent()` →
   `g_vendorInput->notify()`

例えば Agent 1 ボタンを押すと、`AG00` の pressed と released が
連続して送信され、ホスト側では「Agent 0 が選択された」という
短いクリックイベントとして認識されます。

マイクのトグルとアナログスティックの方向は、リピートフレームを
特別に扱います。マイクはリピートを無視し、アナログスティックは
リピートコードを同じ方向の連続入力として扱います。

## 8. 接続状態の管理

```cpp
class HidServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(...) override { g_connected = true; }
  void onDisconnect(...) override {
    g_connected = false;
    NimBLEDevice::startAdvertising();
  }
};
```

接続が切れると自動で Advertising を再開し、次のホストからの
接続を待ちます。IR リモコンからの入力は `g_connected` が `true`
の時だけ BLE 送信され、未接続時は Serial に `BLE not connected` と
表示して無視します。

## 9. ホスト側で受信する際のポイント

ホストアプリケーションは、HID デバイスからの Vendor Report（Report ID 6）
を読み取り、JSON-RPC をパースする必要があります。

- チャネル番号 `2` を確認
- 2 バイト目の長さを見て JSON を取り出す
- `m == "v.oai.hid"` であれば `p.k` と `p.act` を処理
- `k` が `AGxx` ならエージェント、`ACTxx` ならアクション

Vibe Watch 本体と同じプロトコルなので、既存の Vibe Watch ホスト
ソフトウェアにそのまま接続できます。
