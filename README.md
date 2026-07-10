# TvtTape プラグイン
D-VHS 機器を制御し、TS ストリームを BonDriver_Pipe.dll へ出力する TVTest プラグインです。

## 構成

- `TvtTape.cpp` / `TvtTape.h`
  - メインプラグインクラス（`CTvtTape`）
  - TvtPlay 風ステータス行 UI、タイマー更新、コマンド分岐
- `VcrDevice.cpp` / `VcrDevice.h`
  - VCR デバイス制御クラス（`CVcrDevice`）
  - デバイス列挙、トランスポート制御、タイムコード取得
- `PipeControl.cpp` / `PipeControl.h`
  - BonDriver_Pipe 制御チャネルアダプタ（`CPipeControl`）
  - `PAUSE`、`PURGE`、`GET_READY_STATE`
- `StatusView.cpp` / `StatusView.h`
  - ステータス項目レイアウト／入力処理
- `DrawUtil.cpp` / `DrawUtil.h`
  - テーマ色単色描画に使う BMP 描画ヘルパ

## 現在の機能

- TVTest プラグイン出力: `TvtTape.tvtp`
- ステータス行の操作ボタン:
  - VCR デバイス選択
  - 電源 ON/OFF トグル
  - 巻き戻し（トグル: REW -> PLAY）
  - 再生／一時停止トグル
  - 停止
  - 早送り（トグル: FF -> PLAY）
  - 録画開始／録画停止トグル
- ステータス表示:
  - 走行状態（再生 / 一時停止 / 停止 / 巻き戻し / 早送り など）
  - タイムコード（`HH:MM:SS`）
- VCR デバイスの手動選択:
  - ステータス行左端のビデオアイコンをクリック
  - `Auto select` またはデバイス番号を選択
  - 選択結果は `TvtTape.ini` に保存
- ボタン BMP の読み込み:
  - まず埋め込みリソース `IDB_TVTAPE_BUTTONS` を使用
  - リソースが無効な場合は `TvtTape.ini` の `UI > IconBitmapPath` を使用
  - どちらも使えない場合はテキスト表示へフォールバック
- BonDriver_Pipe 連携:
  - Pause/Resume 同期
  - 停止時 Purge
- TS 入力断による自動録画停止:
  - 録画中の bitrate 0Mbps を監視
  - 5 秒以上 0 が続くと自動で録画停止

## TODO
- [ ] 早送り再生・巻き戻し再生時にコマ送り画像を表示する？

## 流用・参考元

- TVTest https://github.com/DBCTRADO/TVTest から `TVTestPlugin.h`を使用し、`StatusView.*`、`DrawUtil.*` を参考にしています。

## BMP 要件

- 形式: 非圧縮 BMP
- カラーモデル: 1bit モノクロ、または単色化しやすい 32bit BMP
- レイアウト: 横一列 10 アイコン（順序固定）
  - VCR
  - 電源
  - 巻き戻し（REW）
  - 再生／一時停止（PLAY/PAUSE）
  - 停止（STOP）
  - 早送り（FF）
  - 録画（REC）
- サイズ: 正方形アイコン（推奨 16x16 または 20x20）
- 画像サイズ例:
  - 16x16 の場合: 160x16 BMP
  - 20x20 の場合: 200x20 BMP
- 背景: アイコン色と区別できる単色背景
  - 描画時に TVTest の文字色へ再着色されます
- 参照先:
  - 既定は埋め込みリソース `IDB_TVTAPE_BUTTONS`
  - 外部ファイル運用する場合は `TvtTape.ini` の `UI > IconBitmapPath`

## ビルド

```powershell
msbuild .\TvtTape.vcxproj /p:Configuration=Release /p:Platform=x64
```

`msbuild` が PATH にない場合は Visual Studio Developer Command Prompt から実行してください。
