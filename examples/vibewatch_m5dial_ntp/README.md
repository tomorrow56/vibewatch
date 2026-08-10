# vibewatch_m5dial_ntp

M5Dial の NTP 時計スケッチに、Vibe Watch for M5Dial を組み込んだ版です。
M5Dial のボタンで時計画面と Vibe Watch 画面を切り替えます。

## 機能

- NTP で時刻を取得し、M5Dial の RTC に反映
- 円形ディスプレイ向けの時計表示
- Vibe Watch 互換の BLE HID / JSON-RPC 通信
- オリジナル Vibe Watch に寄せた Agent / Action アイコン表示
- M5Dial のロータリーエンコーダー、ボタン、タッチ操作に対応

## 操作

### 時計画面

| 操作 | 動作 |
|---|---|
| M5Dial ボタン短押し | Vibe Watch 画面へ切替 |

### Vibe Watch 画面

| 操作 | 動作 |
|---|---|
| M5Dial ボタン短押し | 時計画面へ戻る |
| M5Dial ボタン長押し 600ms 以上 | Agent / Action レイヤー切替 |
| ロータリーエンコーダー | 選択中の Agent / Action を移動 |
| タッチリング | Agent / Action を直接実行 |
| 中央タッチ | 押している間マイク ON |

## Wi-Fi 設定

[vibewatch_m5dial_ntp.ino](./vibewatch_m5dial_ntp.ino) の以下を自分の Wi-Fi に変更します。

```cpp
const char* ssid       = "your_ssid";
const char* password   = "your_password";
```

## ビルドと書き込み

PlatformIO を使います。

```sh
pio run
pio run -t upload --upload-port /dev/cu.usbmodem11101
```

接続ポートは環境によって変わります。確認する場合:

```sh
pio device list
```

## シリアルログ確認

```sh
pio device monitor --port /dev/cu.usbmodem11101 --baud 115200
```

正常に Wi-Fi と NTP が通ると、起動時に次のようなログが出ます。

```text
Connect to <SSID>. CONNECTED
Connected to NTP Server!
```

## 依存ライブラリ

[platformio.ini](./platformio.ini) で以下を指定しています。

- `m5stack/M5Dial`
- `h2zero/NimBLE-Arduino`
- `bblanchon/ArduinoJson`
- `bodmer/TFT_eSPI`

## 実装メモ

- 時計表示は [vibewatch_m5dial_ntp.ino](./vibewatch_m5dial_ntp.ino) にあります。
- Vibe Watch 機能は [VibeWatchMode.ino](./VibeWatchMode.ino) に分離しています。
- 時計スプライトと Vibe Watch キャンバスを同時に確保すると RAM が足りなくなるため、画面切替時に未使用側の描画バッファを解放しています。
- BLE 初期化は Vibe Watch 初回表示時のみ行い、以後は接続状態を維持します。

## 参照元

- M5Dial NTP watch mod: https://github.com/tomorrow56/M5Dial_NTP/tree/main/M5Dial/M5Dial_watch_mod
- Vibe Watch M5Dial example: https://github.com/tomorrow56/vibewatch/tree/main/examples/vibewatch_m5dial
- Original Vibe Watch: https://github.com/GOROman/vibewatch
- M5Dial documentation: https://docs.m5stack.com/ja/core/M5Dial
