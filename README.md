# SwitchBot SmartLock Opener (M5StickC Plus)

M5StickC Plus のボタン操作で SwitchBot Smart Lock を開閉するプロジェクトです。  
本プロジェクトは **SwitchBot SmartLock Pro** でのみ動作確認しています。

Code authored with assistance from OpenAI Codex.

## 対応機種

- **M5StickC Plus のみ対応**
- SwitchBot SmartLock Pro でのみ動作確認済み

## 必要なもの

- M5StickC Plus
- SwitchBot SmartLock Pro
- SwitchBot Hub（例: Hub Mini 30）
- Wi-Fi（2.4GHz）
- PlatformIO 環境

## セットアップ

1. `src/const.hpp.example` を `src/const.hpp` にコピー
2. `src/const.hpp` を開き、以下の値を設定
   - `WIFI_SSID`
   - `WIFI_PASS`
   - `SWITCHBOT_TOKEN`
   - `SWITCHBOT_SECRET`
   - `SWITCHBOT_DEVICE_ID`

> `const.hpp` は機密情報を含むため `.gitignore` されています。

## ビルド方法

```bash
pio run
```

## 転送（書き込み）方法

```bash
pio run -t upload
```

必要ならシリアルモニターを使って確認できます。

```bash
pio device monitor
```

## 使い方

- **ボタンA（GPIO37）長押し 2 秒**: 解錠（Unlocked / 赤表示）
- **ボタンB（GPIO39）長押し 2 秒**: 施錠（Locked / 緑表示）
- 連続操作防止のため **5 秒のクールダウン**があります
- API 失敗時は表示は変えず、`API ERROR` を数秒表示します
- 画面には `Battery` と `JST` 時刻が表示されます

## 注意点

- 本プロジェクトは **SwitchBot SmartLock Pro でのみ動作確認済み**です  
- **M5StickC Plus のみ対応**しています（Plus2 などは未対応）
- `src/const.hpp.example` から `src/const.hpp` を作成する必要があります
