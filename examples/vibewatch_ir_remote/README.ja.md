# Vibe Watch IR リモコン例

この例では、M5Stack StopWatch のタッチパネルと物理ボタンの代わりに、一般的な赤外線リモコンを使います。BLE HID ベンダーレポートプロトコルは Vibe Watch と同じですが、ChatGPT Desktop に Codex Micro として認識されるようアドバタイズするため、特別な設定なしでペアリングできます。

ベースになったもの:

- `ESP32-NimBLE-Keyboard/examples/esp32_ir-ble_kbd/esp32_ir-ble_kbd.ino`（IR 入力）
- `vibewatch/src/main.cpp`（NimBLE HID と Vibe Watch プロトコル）

## 必要なもの

- ESP32 開発ボード（Arduino ESP32 コアでサポートされているものなら可）
- VS1838B や TSOP4838 などの赤外線受信モジュール
- NEC 形式の赤外線リモコン（または `IRremoteESP8266` が対応している他のプロトコル）

## 配線

| ESP32 ピン | IR モジュール ピン |
|---|---|
| GPIO 22 | Data（OUT） |
| 3.3 V | VCC |
| GND | GND |

受信ピンはスケッチ内の `kIrRecvPin` を編集することで変更できます。

## 依存ライブラリ

Arduino IDE のライブラリマネージャーで以下をインストールしてください:

- `NimBLE-Arduino` by h2zero
- `IRremoteESP8266` by crankyoldgit
- `ArduinoJson` by Benoit Blanchon

PlatformIO の場合は `lib_deps` に追加してください:

```ini
lib_deps =
    h2zero/NimBLE-Arduino @ 2.5.1
    crankyoldgit/IRremoteESP8266 @ ^2.8.0
    bblanchon/ArduinoJson @ 7.4.3
```

## セットアップ

1. スケッチを ESP32 にビルド＆書き込みます。
2. シリアルモニタを 115200 baud で開きます。
3. `BLE HID advertising started as Codex Micro` と表示されるまで待ちます。
4. パソコンやスマホから **Codex Micro** としてペアリングします。
5. リモコンを受信モジュールに向け、各ボタンを一度ずつ押します。シリアルモニタには受信した 16 進コードが表示されます。例:

   ```text
   Received IR Code: 0x807F00FF
   ```

6. 表示された値をスケッチ上部の対応する `#define` 行にコピーし、再書き込みします。

## IR ボタン割り当て

デフォルトの割り当ては NEC 形式の例です。自分のリモコンのコードに置き換えてください。

| ボタン | 機能 | 例のコード |
|---|---|---|
| Agent 1 | エージェント 0 を選択 | `0x807F18E7` |
| Agent 2 | エージェント 1 を選択 | `0x807F58A7` |
| Agent 3 | エージェント 2 を選択 | `0x807FD827` |
| Agent 4 | エージェント 3 を選択 | `0x807F28D7` |
| Agent 5 | エージェント 4 を選択 | `0x807F6897` |
| Agent 6 | エージェント 5 を選択 | `0x807FE817` |
| FAST | アクション ACT06 を送信 | `0x807F00FF` |
| OK | アクション ACT07 を送信 | `0x807FC03F` |
| NG | アクション ACT08 を送信 | `0x807F50AF` |
| PLAN | プラン モード切り替え、ACT09 を送信 | `0x807F708F` |
| AI | アクション ACT12 を送信 | `0x807FD02F` |
| MIC | 録音 開始/停止 トグル（ACT10） | `0x807F38C7` |
| LEFT | アナログスティック左（v.oai.rad, a=0.5） | `0x807F20DF` |
| RIGHT | アナログスティック右（v.oai.rad, a=0.0） | `0x807FE01F` |
| DOWN | アナログスティック下（v.oai.rad, a=0.25） | `0x807F609F` |
| UP | アナログスティック上（v.oai.rad, a=0.75） | `0x807F40BF` |

> **MIC ボタンについて:** IR リモコンはボタンを離したことを検出できないため、MIC ボタンはトグルとして動作します。初回押下で `ACT10` DOWN を送信して録音を開始し、もう一度押すと `ACT10` UP を送信して録音を停止します。リピートフレームは無視されるため、長押ししても状態が連続して反転しません。
>
> **アナログスティックについて:** IR リモコンには実際のアナログスティックがないため、方向ボタンを Codex Micro の `v.oai.rad` ジョイスティックイベントに割り当てています。角度は Codex Micro プロトコルに合わせて、0.0 = 右、0.25 = 下、0.5 = 左、0.75 = 上 となっています。

## BLE アドバタイジング名

アドバタイジングするデバイス名は `Codex Micro`（11 文字）に設定されています。ChatGPT Desktop はこれを Codex Micro コントローラーとして認識します。BLE のレガシーアドバタイジングパケットは合計 31 バイトに制限されており、名前が約 18 ASCII 文字を超えるとホストから発見できなくなることがあります。変更する場合は短い名前にしてください。

## 技術的な詳細

BLE HID 伝送層、ベンダーレポート形式、JSON-RPC プロトコル、IR コードからホストイベントへのマッピングについての詳細は、[TECHNICAL.ja.md](TECHNICAL.ja.md)（日本語）を参照してください。
