# Vibe Watch for M5Dial (日本語)

このサンプルは、Vibe Watch の操作体験を
[M5Dial](https://docs.m5stack.com/ja/core/M5Dial) ボードに移植したものです。
M5Stack StopWatch のタッチパネルと物理ボタン2個の代わりに、M5Dial の
1.28インチ丸型タッチディスプレイ、ロータリーエンコーダー、単一の押しボタンを
使用しますが、オリジナルのファームウェアと同じ NimBLE HID プロトコルを
維持しているため、ホスト側のソフトウェアは変更不要です。

## ハードウェア

- [M5Dial](https://docs.m5stack.com/ja/core/M5Dial)
  - ESP32-S3FN8、1.28インチ丸型TFT (240 x 240)、ロータリーエンコーダー、
    押しボタン、タッチスクリーン、ブザー

## 操作方法

| 入力 | 機能 |
|---|---|
| **ロータリーエンコーダー** | 次/前のAgentを選択（Actionレイヤーの場合はAction） |
| **エンコーダー押しボタン 短押し** | 選択中の項目を実行 |
| **エンコーダー押しボタン 長押し（600ms以上）** | AgentレイヤーとActionレイヤーを切り替え |
| **タッチリング** | Agent/Actionをタップして直接実行 |
| **タッチ中央** | 押している間だけマイクをオン（ACT10/ACT11） |
| **タッチ下部ボタン** | AgentレイヤーとActionレイヤーを切り替え |

### Actionレイヤー

5つのActionは、オリジナルのVibe Watchと同じレポートIDに割り当てられています。

| 位置 | Action | レポート |
|---|---|---|
| 0 | FAST | ACT06 |
| 1 | NG | ACT07 |
| 2 | OK | ACT08 |
| 3 | PLAN | ACT09（プランモードを切り替え） |
| 4 | AI | ACT12 |

## 依存ライブラリ

Arduino IDE のライブラリマネージャーで以下をインストールしてください。

- `M5Dial`（m5stack 作）
- `NimBLE-Arduino`（h2zero 作）
- `ArduinoJson`（Benoit Blanchon 作）

PlatformIO の場合は、同梱の `platformio.ini` が依存関係を処理します。

```ini
lib_deps =
    m5stack/M5Dial @ ^1.0.3
    h2zero/NimBLE-Arduino @ 2.5.1
    bblanchon/ArduinoJson @ 7.4.3
```

## ビルドに関する注意事項

- `platformio.ini` では `board = m5stack-stamps3` を使用しています。これは
  M5Dial に搭載されている M5StampS3 モジュールに対応するものです。
- USBシリアルポートを使用できるように `ARDUINO_USB_CDC_ON_BOOT=1` が
  有効化されています。
- アドバタイズされるデバイス名は `VibeDial`（保存されたスロットを変更した
  場合は `VibeDial #1` 〜 `#3`）です。BLEレガシーアドバタイジングの
  31バイト制限内に収まっています。

## プロトコル

BLE層はメインのVibe Watchファームウェアおよび `vibewatch_ir_remote`
サンプルと同一です。HIDレポートマップ、Vendor Report JSON-RPCの
フレーミング、ホストイベント、アドバタイジングの制約についての詳細な
説明は `vibewatch_ir_remote/TECHNICAL.ja.md` を参照してください。
