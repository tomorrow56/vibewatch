# Vibe Watch PS3-to-IR Bridge サンプル (日本語)

このサンプルは、`vibewatch_ir_remote` を赤外線リモコンの代わりに
PlayStation 3 DualShock 3 コントローラで操作できるようにするものです。
ESP32 は DualShock 3 と Bluetooth Classic (SPP) でペアリングし、
`vibewatch_ir_remote.ino` がすでに理解できる NEC 形式の赤外線コードを
そのまま再送信します。そのため Codex Micro / BLE 側は一切変更不要です。

## なぜ2台構成なのか

DualShock 3 は ESP32 の Bluetooth Classic (Bluedroid) スタックを使って
接続します。このスタックは `vibewatch_ir_remote.ino` が BLE HID /
Codex Micro 接続に使っている NimBLE と同一の無線チップ上で同時には
動作できません（参考:
[h2zero/NimBLE-Arduino#876](https://github.com/h2zero/NimBLE-Arduino/issues/876)）。
この2つの役割を赤外線リンクで分離することで、この競合を完全に回避します。

```text
DualShock 3  --(Bluetooth Classic)-->  ESP32 #1（本サンプル）
                                             |
                                        (赤外線, NEC形式)
                                             v
                                        ESP32 #2 (vibewatch_ir_remote.ino)
                                             |
                                        (BLE HID / Codex Micro)
                                             v
                                        ChatGPT Desktop
```

## 必要なもの

- ESP32 開発ボード（本サンプルを書き込む側）
- `vibewatch_ir_remote.ino` を書き込んだ別の ESP32（赤外線受信側）
- 赤外線 LED（到達距離を伸ばすため小型 NPN トランジスタ駆動を推奨。
  例: 2N2222 / 2N3904）
- PlayStation 3 DualShock 3 コントローラ
- ESP32 の Bluetooth MAC アドレスをコントローラのペアリング情報に
  書き込むツール。[SixaxisPairTool](https://dancingpixelstudios.com/sixaxis-controller/sixaxispairtool/)
  は Windows 専用です。macOS/Linux では本サンプルに同梱の
  `tools/sixaxis_pair.py`（コンパイラ不要、下記参照）を使うか、
  [sixaxispairer](https://github.com/user-none/sixaxispairer) や
  `sixpair.c` をソースからビルドしてください。

## 配線

| ESP32 ピン | 赤外線 LED 回路 |
|---|---|
| GPIO 21 | NPN トランジスタのベース（約470Ωの抵抗を経由） |
| 3.3V / 5V | 赤外線 LED アノード（電流制限抵抗を経由） |
| GND | トランジスタのエミッタ / 赤外線 LED カソード側 |

送信ピンはスケッチ内の `kIrSendPin` を編集することで変更できます。
赤外線 LED は `vibewatch_ir_remote.ino` を実行しているボードの
赤外線受信モジュールに向けてください。

## 依存ライブラリ

Arduino IDE のライブラリマネージャーで以下をインストールしてください。

- `PS3 Controller Host`（jvpernis 作）
- `IRremoteESP8266`（crankyoldgit 作）

PlatformIO の場合は `lib_deps` に以下を追加します。

```ini
lib_deps =
    jvpernis/PS3 Controller Host @ ^1.1.0
    crankyoldgit/IRremoteESP8266 @ ^2.8.0
```

> **新しいESP32コアでのArduino IDEビルドエラーについて:** 比較的新しい
> ESP32 Arduinoコア（3.3.10で確認）では、`PS3 Controller Host` v1.1.0
> のビルド時に以下のエラーが出ることがあります。
>
> ```text
> ps3.c:287:5: error: implicit declaration of function 'esp_base_mac_addr_set'
> ```
>
> これは、新しいESP-IDFでMAC関連のAPI宣言が `esp_system.h` から
> `esp_mac.h` に分離されたにもかかわらず、ライブラリ側が
> `esp_mac.h` をincludeしていないために発生します。対処法は、
> `~/Documents/Arduino/libraries/PS3_Controller_Host/src/ps3.c` の
> 冒頭付近（既存の `#include <esp_system.h>` の近く）に
> `#include <esp_mac.h>` を追加することです。この修正はお使いの
> Arduinoライブラリフォルダに対するローカルな変更なので、ライブラリを
> 再インストール・更新すると上書きされて消えます。その場合は同じ1行を
> 再度追加してください。

> **新しいESP32コアでの実行時の不具合（MACアドレスが空になる）:**
> 比較的新しいESP32 Arduinoコア（3.3.10で確認）は、Bluetooth Classicを
> 使うライブラリだと明示的にマークされていないと、起動時に自動でBT
> クラシック用メモリ（約36KB）を解放してしまいます。`PS3 Controller
> Host` はこの仕組みより前に作られたライブラリのため、対策をしないと
> `Ps3.begin()` が何のエラーメッセージも出さずに（Core Debug Levelを
> 上げても）静かに失敗し、`Ps3.getAddress()` が空文字を返します。
> 本サンプルではすでに `esp32-hal-alloc-bt-classic-mem.h` を
> include することでこれを回避済み（このヘッダが無い古いコアでも
> 問題ないよう `__has_include` でガード済み）なので、利用者側での
> 追加対応は不要です。他のスケッチで同じ症状に遭遇した場合の参考として
> ここに記載しています。

## セットアップ手順

1. 本スケッチを ESP32 にビルド・書き込みします。
2. シリアルモニタを 115200 baud で開きます。ESP32 の Bluetooth MAC
   アドレスが表示されます。

   ```text
   ESP32 Bluetooth MAC: 01:02:03:04:05:06
   ```

3. DualShock 3 を USB ケーブルでパソコンに接続し、表示された MAC
   アドレスをコントローラのペアリング情報に書き込んで、PS3 本体の
   アドレスを上書きします。

   - **macOS/Linux（コンパイラ不要）:**

     最近のmacOSのPythonは「externally managed」（PEP 668）となっており、
     仮想環境の外で`pip install`を実行するとエラーになります。そのため
     まず仮想環境（venv）を作成してください。また、このスクリプトは
     生のUSBアクセス（`libusb`）ではなく`hidapi`を使用しています。
     macOSではDualShock 3のUSBインターフェースがOS標準の
     IOHIDFamilyドライバに既に掴まれているため、`libusb`系のツールは
     `sudo`で実行しても「Access denied」エラーになりますが、`hidapi`は
     OSのHIDスタック経由でアクセスするためこの問題を回避できます。

     ```bash
     # 初回セットアップのみ（2回目以降は不要）
     brew install hidapi      # macOSのみ。Linuxはパッケージマネージャでlibhidapiを導入
     cd examples/vibewatch_ps3_ir_bridge
     python3 -m venv .venv
     .venv/bin/pip install hid

     # 実行するたびに使うコマンド
     .venv/bin/python3 tools/sixaxis_pair.py AA:BB:CC:DD:EE:FF
     ```

     `brew install hidapi`・`python3 -m venv .venv`・`.venv/bin/pip
     install hid` は最初の1回だけ実行すれば十分です。`.venv`
     ディレクトリと`hid`パッケージのインストールは既に完了して
     いるので、2回目以降は`.venv/bin/python3 tools/sixaxis_pair.py
     ...`のコマンドだけ実行すれば動作します。

     `AA:BB:CC:DD:EE:FF` は手順2で表示されたMACアドレスに置き換えて
     ください。まず引数なしで実行し、コントローラが認識され現在の
     ペアリング先が表示されることを確認するのがおすすめです。`sudo`は
     不要です。

     > このリポジトリが外付けドライブなど exFAT や FAT32 の
     > ボリューム上にある場合、`pip`が`WARNING: Ignoring invalid
     > distribution -pip`のような警告を出すことがあります。これは
     > exFAT/FAT32がネイティブに扱えないメタデータのために、macOS が
     > `._*` という隠しAppleDoubleファイルを自動生成することが原因で、
     > 動作には影響しないため無視して問題ありません。

   - **Windows:** [SixaxisPairTool](https://dancingpixelstudios.com/sixaxis-controller/sixaxispairtool/)
     を使用してください。

4. USB ケーブルを外し、コントローラの PS ボタンを押します。
   ペアリングが成功するとシリアルモニタに `DualShock 3 connected`
   と表示されます。
5. 赤外線 LED を `vibewatch_ir_remote.ino` を実行しているボードに
   向け、対応するボタンを押します。送信した赤外線コードがシリアル
   モニタに表示されます。

## ボタン対応表

| DualShock 3 ボタン | 機能 | 送信される赤外線コード |
|---|---|---|
| 十字キー 上 | エージェント1選択 | `IR_CODE_AGENT_1` |
| 十字キー 右 | エージェント2選択 | `IR_CODE_AGENT_2` |
| 十字キー 下 | エージェント3選択 | `IR_CODE_AGENT_3` |
| 十字キー 左 | エージェント4選択 | `IR_CODE_AGENT_4` |
| L1 | エージェント5選択 | `IR_CODE_AGENT_5` |
| R1 | エージェント6選択 | `IR_CODE_AGENT_6` |
| ×（クロス） | NG アクション | `IR_CODE_NG` |
| ○（サークル） | OK アクション | `IR_CODE_OK` |
| □（スクエア） | MIC 録音のオン/オフ切り替え | `IR_CODE_MIC` |
| △（トライアングル） | AI アクション | `IR_CODE_AI` |
| Start | AI アクション | `IR_CODE_AI` |
| Select | プランモード切り替え | `IR_CODE_PLAN` |
| PS ボタン | FAST アクション | `IR_CODE_FAST` |
| 左スティック 左/右/上/下 | アナログスティック操作 | `IR_CODE_LEFT` / `IR_CODE_RIGHT` / `IR_CODE_UP` / `IR_CODE_DOWN` |

> **重要:** これらの赤外線コードの値は `vibewatch_ir_remote.ino` の
> `#define` の値と一致している必要があります。受信側でコードを
> カスタマイズした場合は、本スケッチ冒頭の対応する `#define` も
> 合わせて更新してください。

各ボタンは押した瞬間に1回だけ赤外線フレームを送信します（エッジ
トリガー）。これは実物のリモコンを1回押すのと同じ挙動で、ボタンを
押し続けても連続送信はされません。左アナログスティックも同様に、
一度ニュートラル位置に戻らないと同じ方向（または別の方向）への
再トリガーは発生しません。

## 技術詳細

最終的にこれらの赤外線コードが駆動する BLE HID / JSON-RPC
プロトコルの詳細については、
[../vibewatch_ir_remote/TECHNICAL.ja.md](../vibewatch_ir_remote/TECHNICAL.ja.md)
を参照してください。
